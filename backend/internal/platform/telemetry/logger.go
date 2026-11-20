package telemetry

import (
	"context"
	"io"
	"log/slog"
	"strings"

	"go.opentelemetry.io/otel/trace"
)

type ctxMetaKey struct{}

// RequestLogMeta carries per-request identifiers attached to structured JSON logs.
type RequestLogMeta struct {
	AccountID    string
	CharacterID  string
	ZoneID       string
	ServerID     string
	FencingEpoch int64
}

// WithRequestLogMeta stores RequestLogMeta inside ctx.
func WithRequestLogMeta(ctx context.Context, meta RequestLogMeta) context.Context {
	return context.WithValue(ctx, ctxMetaKey{}, meta)
}

// ParseLogLevel converts string log levels to slog.Level.
func ParseLogLevel(raw string) slog.Level {
	switch strings.ToLower(strings.TrimSpace(raw)) {
	case "debug":
		return slog.LevelDebug
	case "warn", "warning":
		return slog.LevelWarn
	case "error":
		return slog.LevelError
	default:
		return slog.LevelInfo
	}
}

// TraceContextHandler wraps slog.JSONHandler to inject trace_id, span_id, and character context.
type TraceContextHandler struct {
	inner slog.Handler
}

// NewJSONLogger builds a structured JSON slog.Logger with TraceContextHandler.
func NewJSONLogger(w io.Writer, serviceName string, level slog.Level) *slog.Logger {
	base := slog.NewJSONHandler(w, &slog.HandlerOptions{
		Level:     level,
		AddSource: false,
	}).WithAttrs([]slog.Attr{
		slog.String("service", serviceName),
	})
	return slog.New(&TraceContextHandler{inner: base})
}

func (h *TraceContextHandler) Enabled(ctx context.Context, level slog.Level) bool {
	return h.inner.Enabled(ctx, level)
}

func (h *TraceContextHandler) Handle(ctx context.Context, r slog.Record) error {
	if spanCtx := trace.SpanContextFromContext(ctx); spanCtx.IsValid() {
		r.AddAttrs(
			slog.String("trace_id", spanCtx.TraceID().String()),
			slog.String("span_id", spanCtx.SpanID().String()),
		)
	}
	if meta, ok := ctx.Value(ctxMetaKey{}).(RequestLogMeta); ok {
		if meta.AccountID != "" {
			r.AddAttrs(slog.String("account_id", meta.AccountID))
		}
		if meta.CharacterID != "" {
			r.AddAttrs(slog.String("character_id", meta.CharacterID))
		}
		if meta.ZoneID != "" {
			r.AddAttrs(slog.String("zone_id", meta.ZoneID))
		}
		if meta.ServerID != "" {
			r.AddAttrs(slog.String("server_id", meta.ServerID))
		}
		if meta.FencingEpoch > 0 {
			r.AddAttrs(slog.Int64("fencing_epoch", meta.FencingEpoch))
		}
	}
	return h.inner.Handle(ctx, r)
}

func (h *TraceContextHandler) WithAttrs(attrs []slog.Attr) slog.Handler {
	return &TraceContextHandler{inner: h.inner.WithAttrs(attrs)}
}

func (h *TraceContextHandler) WithGroup(name string) slog.Handler {
	return &TraceContextHandler{inner: h.inner.WithGroup(name)}
}
