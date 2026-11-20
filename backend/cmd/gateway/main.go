package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"log/slog"
	"net"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/infinite-universe/backend/internal/httpapi"
	"github.com/infinite-universe/backend/internal/modules/account"
	"github.com/infinite-universe/backend/internal/modules/auth"
	"github.com/infinite-universe/backend/internal/modules/character"
	"github.com/infinite-universe/backend/internal/modules/world"
	"github.com/infinite-universe/backend/internal/platform/config"
	iunats "github.com/infinite-universe/backend/internal/platform/nats"
	"github.com/infinite-universe/backend/internal/platform/postgres"
	iuredis "github.com/infinite-universe/backend/internal/platform/redis"
	"github.com/infinite-universe/backend/internal/platform/telemetry"
	"github.com/jackc/pgx/v5/pgxpool"
	"github.com/prometheus/client_golang/prometheus/promhttp"
	goredis "github.com/redis/go-redis/v9"
)

func main() {
	healthcheckURL := flag.String("healthcheck", "", "URL to probe for HTTP 200 healthcheck")
	flag.Parse()

	if *healthcheckURL != "" {
		os.Exit(runHealthcheck(*healthcheckURL))
	}

	if err := run(); err != nil {
		fmt.Fprintf(os.Stderr, "gateway fatal: %v\n", err)
		os.Exit(1)
	}
}

func runHealthcheck(targetURL string) int {
	client := &http.Client{Timeout: 2 * time.Second}
	resp, err := client.Get(targetURL)
	if err != nil {
		return 1
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return 1
	}
	return 0
}

func run() error {
	cfg, err := config.Load(config.ServiceGateway)
	if err != nil {
		return fmt.Errorf("load config: %w", err)
	}

	logger := telemetry.NewJSONLogger(os.Stdout, cfg.ServiceName, telemetry.ParseLogLevel(cfg.LogLevel))
	metrics := telemetry.NewMetrics(cfg.ServiceName)

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	shutdownTracer, err := telemetry.InitTracer(ctx, cfg.ServiceName, cfg.OTLPEndpoint)
	if err != nil {
		return fmt.Errorf("init tracer: %w", err)
	}
	defer func() {
		shCtx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()
		_ = shutdownTracer(shCtx)
	}()

	var rdb *goredis.Client
	if cfg.RedisAddr != "" {
		rdb, err = iuredis.NewClient(ctx, cfg.RedisAddr)
		if err != nil {
			if !cfg.AllowOptionalDeps {
				return fmt.Errorf("connect redis: %w", err)
			}
			logger.Warn("redis connection failed; continuing due to IU_ALLOW_OPTIONAL_DEPS=true", slog.String("error", err.Error()))
		} else {
			defer func() { _ = rdb.Close() }()
		}
	} else {
		if !cfg.AllowOptionalDeps {
			return fmt.Errorf("IU_REDIS_ADDR is required when IU_ALLOW_OPTIONAL_DEPS=false")
		}
		logger.Warn("IU_REDIS_ADDR is empty; skipping redis initialization due to IU_ALLOW_OPTIONAL_DEPS=true")
	}

	var natsClient *iunats.Client
	if cfg.NATSURL != "" {
		natsClient, err = iunats.Connect(ctx, cfg.NATSURL, cfg.ServiceName, cfg.NATSStreamReplicas())
		if err != nil {
			if !cfg.AllowOptionalDeps {
				return fmt.Errorf("connect nats: %w", err)
			}
			logger.Warn("nats connection failed; continuing due to IU_ALLOW_OPTIONAL_DEPS=true", slog.String("error", err.Error()))
		} else {
			defer natsClient.Close()
		}
	} else {
		if !cfg.AllowOptionalDeps {
			return fmt.Errorf("IU_NATS_URL is required when IU_ALLOW_OPTIONAL_DEPS=false")
		}
		logger.Warn("IU_NATS_URL is empty; skipping nats initialization due to IU_ALLOW_OPTIONAL_DEPS=true")
	}

	var pgPool *pgxpool.Pool
	if cfg.PostgresDSN != "" {
		pgPool, err = postgres.NewPool(ctx, cfg.PostgresDSN)
		if err != nil {
			return fmt.Errorf("connect postgres: %w", err)
		}
		defer pgPool.Close()
		logger.Info("postgres pool initialized", slog.String("service", cfg.ServiceName))
	}

	healthHandler := func(w http.ResponseWriter, r *http.Request) {
		pingCtx, cancel := context.WithTimeout(r.Context(), 1500*time.Millisecond)
		defer cancel()

		if pgPool != nil {
			if err := pgPool.Ping(pingCtx); err != nil {
				writeJSON(w, http.StatusServiceUnavailable, map[string]string{
					"status": "degraded",
					"error":  fmt.Sprintf("postgres ping failed: %v", err),
				})
				return
			}
		}
		if rdb != nil {
			if err := rdb.Ping(pingCtx).Err(); err != nil {
				writeJSON(w, http.StatusServiceUnavailable, map[string]string{
					"status": "degraded",
					"error":  fmt.Sprintf("redis ping failed: %v", err),
				})
				return
			}
		}
		if natsClient != nil {
			if err := natsClient.Ping(pingCtx); err != nil {
				writeJSON(w, http.StatusServiceUnavailable, map[string]string{
					"status": "degraded",
					"error":  fmt.Sprintf("nats ping failed: %v", err),
				})
				return
			}
		}

		writeJSON(w, http.StatusOK, map[string]string{
			"status":        "ok",
			"gamedata_hash": "",
			"step":          "step3_persistence_ready",
		})
	}

	edgeMux := http.NewServeMux()
	edgeMux.HandleFunc("/v1/healthz", healthHandler)

	if pgPool != nil {
		authMgr, err := auth.NewManager(cfg.JWTPrivateKeySeedHex, cfg.JWTPublicKeyHex, cfg.InternalServerHMACSecret, rdb)
		if err != nil {
			return fmt.Errorf("init auth manager: %w", err)
		}
		accountSvc := account.NewService(pgPool)
		characterSvc := character.NewService(pgPool)
		worldCoord := world.NewCoordinator(rdb, cfg.DefaultJoinZone, cfg.DedicatedServerPublicAddr)
		apiHandler := httpapi.NewHandler(accountSvc, authMgr, characterSvc, worldCoord, logger)
		apiHandler.RegisterRoutes(edgeMux)
		logger.Info("step3 edge & internal persistence routes registered",
			slog.String("dedicated_server_addr", cfg.DedicatedServerPublicAddr),
			slog.String("default_zone", cfg.DefaultJoinZone),
		)
	}

	edgeLis, err := net.Listen("tcp", cfg.HTTPListenAddr)
	if err != nil {
		return fmt.Errorf("listen http %s: %w", cfg.HTTPListenAddr, err)
	}
	edgeSrv := &http.Server{
		Handler:           edgeMux,
		ReadHeaderTimeout: 5 * time.Second,
	}

	opsMux := http.NewServeMux()
	opsMux.Handle("/metrics", promhttp.HandlerFor(metrics.Registry, promhttp.HandlerOpts{}))
	opsMux.HandleFunc("/healthz", healthHandler)

	opsLis, err := net.Listen("tcp", cfg.MetricsListenAddr)
	if err != nil {
		_ = edgeLis.Close()
		return fmt.Errorf("listen metrics %s: %w", cfg.MetricsListenAddr, err)
	}
	opsSrv := &http.Server{
		Handler:           opsMux,
		ReadHeaderTimeout: 5 * time.Second,
	}

	errCh := make(chan error, 2)
	go func() {
		logger.Info("gateway edge listening", slog.String("addr", edgeLis.Addr().String()))
		if err := edgeSrv.Serve(edgeLis); err != nil && !errors.Is(err, http.ErrServerClosed) {
			errCh <- fmt.Errorf("edge serve: %w", err)
		}
	}()
	go func() {
		logger.Info("ops server listening", slog.String("addr", opsLis.Addr().String()))
		if err := opsSrv.Serve(opsLis); err != nil && !errors.Is(err, http.ErrServerClosed) {
			errCh <- fmt.Errorf("ops serve: %w", err)
		}
	}()

	logger.Info("gateway started",
		slog.String("env", cfg.Env),
		slog.String("http_addr", edgeLis.Addr().String()),
		slog.String("metrics_addr", opsLis.Addr().String()),
		slog.String("gameapi_grpc_addr", cfg.GameAPIGRPCAddr),
		slog.String("coordinator_grpc_addr", cfg.CoordinatorGRPCAddr),
	)

	select {
	case <-ctx.Done():
		logger.Info("shutdown signal received")
	case err := <-errCh:
		stop()
		shCtx, cancel := context.WithTimeout(context.Background(), 4*time.Second)
		defer cancel()
		_ = opsSrv.Shutdown(shCtx)
		_ = edgeSrv.Shutdown(shCtx)
		return err
	}

	shCtx, cancel := context.WithTimeout(context.Background(), 4*time.Second)
	defer cancel()
	_ = opsSrv.Shutdown(shCtx)
	_ = edgeSrv.Shutdown(shCtx)
	logger.Info("gateway stopped cleanly")
	return nil
}

func writeJSON(w http.ResponseWriter, statusCode int, payload any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	_ = json.NewEncoder(w).Encode(payload)
}
