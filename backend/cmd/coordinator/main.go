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
	"path/filepath"
	"syscall"
	"time"

	"github.com/infinite-universe/backend/internal/platform/config"
	"github.com/infinite-universe/backend/internal/platform/grpcserver"
	iunats "github.com/infinite-universe/backend/internal/platform/nats"
	iuredis "github.com/infinite-universe/backend/internal/platform/redis"
	"github.com/infinite-universe/backend/internal/platform/telemetry"
	"github.com/infinite-universe/backend/pkg/gamedata"
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
		fmt.Fprintf(os.Stderr, "coordinator fatal: %v\n", err)
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
	cfg, err := config.Load(config.ServiceCoordinator)
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

	gameDataHash, err := computeGameDataHashTolerant(cfg.GameDataDir, logger)
	if err != nil {
		return fmt.Errorf("compute gamedata hash: %w", err)
	}

	grpcSrv, err := grpcserver.New(cfg.GRPCListenAddr, logger, metrics)
	if err != nil {
		return fmt.Errorf("init grpc server: %w", err)
	}

	opsMux := http.NewServeMux()
	opsMux.Handle("/metrics", promhttp.HandlerFor(metrics.Registry, promhttp.HandlerOpts{}))
	opsMux.HandleFunc("/healthz", func(w http.ResponseWriter, r *http.Request) {
		pingCtx, cancel := context.WithTimeout(r.Context(), 1500*time.Millisecond)
		defer cancel()

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
			"gamedata_hash": gameDataHash,
		})
	})

	opsLis, err := net.Listen("tcp", cfg.MetricsListenAddr)
	if err != nil {
		return fmt.Errorf("listen metrics %s: %w", cfg.MetricsListenAddr, err)
	}
	opsSrv := &http.Server{
		Handler:           opsMux,
		ReadHeaderTimeout: 5 * time.Second,
	}

	errCh := make(chan error, 2)
	go func() {
		if err := grpcSrv.Start(); err != nil {
			errCh <- fmt.Errorf("grpc serve: %w", err)
		}
	}()
	go func() {
		logger.Info("ops server listening", slog.String("addr", opsLis.Addr().String()))
		if err := opsSrv.Serve(opsLis); err != nil && !errors.Is(err, http.ErrServerClosed) {
			errCh <- fmt.Errorf("ops serve: %w", err)
		}
	}()

	logger.Info("coordinator started",
		slog.String("env", cfg.Env),
		slog.String("grpc_addr", grpcSrv.Addr()),
		slog.String("metrics_addr", opsLis.Addr().String()),
		slog.String("default_zone", cfg.DefaultJoinZone),
		slog.String("gamedata_hash", gameDataHash),
	)

	select {
	case <-ctx.Done():
		logger.Info("shutdown signal received")
	case err := <-errCh:
		stop()
		shCtx, cancel := context.WithTimeout(context.Background(), 4*time.Second)
		defer cancel()
		_ = opsSrv.Shutdown(shCtx)
		grpcSrv.Shutdown(shCtx)
		return err
	}

	shCtx, cancel := context.WithTimeout(context.Background(), 4*time.Second)
	defer cancel()
	_ = opsSrv.Shutdown(shCtx)
	grpcSrv.Shutdown(shCtx)
	logger.Info("coordinator stopped cleanly")
	return nil
}

func computeGameDataHashTolerant(dir string, logger *slog.Logger) (string, error) {
	if dir == "" {
		logger.Warn("IU_GAMEDATA_DIR not configured; using empty gamedata_hash")
		return "", nil
	}

	target := ""
	for _, candidate := range []string{
		filepath.Join(dir, "sources", "core"),
		filepath.Join(dir, "core"),
	} {
		if info, err := os.Stat(candidate); err == nil && info.IsDir() {
			target = candidate
			break
		}
	}

	if target == "" {
		hasSchemas := dirEntryExists(filepath.Join(dir, "schemas"))
		hasSources := dirEntryExists(filepath.Join(dir, "sources"))
		if hasSchemas || hasSources {
			logger.Warn("gamedata core directory absent; using empty gamedata_hash", slog.String("dir", dir))
			return "", nil
		}
		target = dir
	}

	hash, err := gamedata.ComputeCanonicalGameDataHash(target)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) || errors.Is(err, gamedata.ErrNoGameDataFiles) {
			logger.Warn("gamedata directory absent or empty; using empty gamedata_hash",
				slog.String("dir", target),
				slog.String("reason", err.Error()),
			)
			return "", nil
		}
		return "", err
	}
	return hash, nil
}

func dirEntryExists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

func writeJSON(w http.ResponseWriter, statusCode int, payload any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	_ = json.NewEncoder(w).Encode(payload)
}
