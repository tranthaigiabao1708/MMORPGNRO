package grpcserver

import (
	"context"
	"log/slog"
	"runtime/debug"
	"strings"
	"time"

	"github.com/infinite-universe/backend/internal/platform/telemetry"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// UnaryServerInterceptor provides panic recovery, Prometheus latency timing, and structured logging.
func UnaryServerInterceptor(logger *slog.Logger, metrics *telemetry.Metrics) grpc.UnaryServerInterceptor {
	return func(
		ctx context.Context,
		req any,
		info *grpc.UnaryServerInfo,
		handler grpc.UnaryHandler,
	) (resp any, err error) {
		start := time.Now()
		defer func() {
			if r := recover(); r != nil {
				logger.ErrorContext(ctx, "panic recovered in grpc unary handler",
					slog.String("method", info.FullMethod),
					slog.Any("panic", r),
					slog.String("stack", string(debug.Stack())),
				)
				err = status.Error(codes.Internal, "internal server error")
			}

			code := status.Code(err).String()
			serviceName, methodName := splitFullMethod(info.FullMethod)
			if metrics != nil {
				metrics.GrpcRequestDurationSeconds.WithLabelValues(serviceName, methodName, code).Observe(time.Since(start).Seconds())
			}
			if err != nil {
				logger.WarnContext(ctx, "grpc request failed",
					slog.String("grpc_service", serviceName),
					slog.String("grpc_method", methodName),
					slog.String("grpc_code", code),
					slog.String("error", err.Error()),
				)
			}
		}()

		return handler(ctx, req)
	}
}

func splitFullMethod(fullMethod string) (string, string) {
	trimmed := strings.TrimPrefix(fullMethod, "/")
	parts := strings.SplitN(trimmed, "/", 2)
	if len(parts) != 2 {
		return "unknown", fullMethod
	}
	return parts[0], parts[1]
}
