package grpcserver

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"net"

	"github.com/infinite-universe/backend/internal/platform/telemetry"
	"google.golang.org/grpc"
	"google.golang.org/grpc/health"
	healthpb "google.golang.org/grpc/health/grpc_health_v1"
)

// Server manages the gRPC runtime server lifecycle without VS-02 service stubs.
type Server struct {
	addr     string
	listener net.Listener
	grpcSrv  *grpc.Server
	health   *health.Server
	logger   *slog.Logger
}

// New constructs and binds a gRPC server with standard health service and unary interceptor.
func New(addr string, logger *slog.Logger, metrics *telemetry.Metrics) (*Server, error) {
	if addr == "" {
		return nil, fmt.Errorf("grpc listen addr must not be empty")
	}

	lis, err := net.Listen("tcp", addr)
	if err != nil {
		return nil, fmt.Errorf("listen grpc %s: %w", addr, err)
	}

	srv := grpc.NewServer(
		grpc.UnaryInterceptor(UnaryServerInterceptor(logger, metrics)),
	)
	healthSrv := health.NewServer()
	healthSrv.SetServingStatus("", healthpb.HealthCheckResponse_SERVING)
	healthpb.RegisterHealthServer(srv, healthSrv)

	return &Server{
		addr:     lis.Addr().String(),
		listener: lis,
		grpcSrv:  srv,
		health:   healthSrv,
		logger:   logger,
	}, nil
}

// Addr returns the bound listener address.
func (s *Server) Addr() string {
	return s.addr
}

// Start begins serving gRPC requests until Stop/GracefulStop is invoked.
func (s *Server) Start() error {
	s.logger.Info("grpc server listening", slog.String("addr", s.addr))
	err := s.grpcSrv.Serve(s.listener)
	if errors.Is(err, grpc.ErrServerStopped) {
		return nil
	}
	return err
}

// Shutdown attempts GracefulStop within ctx deadline and falls back to immediate Stop.
func (s *Server) Shutdown(ctx context.Context) {
	if s == nil || s.grpcSrv == nil {
		return
	}
	s.health.SetServingStatus("", healthpb.HealthCheckResponse_NOT_SERVING)

	done := make(chan struct{})
	go func() {
		s.grpcSrv.GracefulStop()
		close(done)
	}()

	select {
	case <-done:
	case <-ctx.Done():
		s.logger.Warn("grpc graceful stop timed out; forcing stop")
		s.grpcSrv.Stop()
	}
}
