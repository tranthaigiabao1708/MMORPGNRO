package telemetry

import (
	"time"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/collectors"
)

// Metrics centralizes all Prometheus collectors for Infinite Universe services.
type Metrics struct {
	Registry                     *prometheus.Registry
	ZoneCCU                      *prometheus.GaugeVec
	ZoneServerTickMs             *prometheus.HistogramVec
	GrpcRequestDurationSeconds   *prometheus.HistogramVec
	LootDropsTotal               *prometheus.CounterVec
	CurrencyFlowTotal            *prometheus.CounterVec
	FencingConflictRejectedTotal *prometheus.CounterVec
	DbTxDurationSeconds          *prometheus.HistogramVec
	EnhancementAttemptsTotal     *prometheus.CounterVec
	TradeSessionsTotal           *prometheus.CounterVec
}

// NewMetrics creates a dedicated Prometheus registry with standard Go/Process and 9 iu_* collectors.
func NewMetrics(serviceName string) *Metrics {
	reg := prometheus.NewRegistry()
	reg.MustRegister(
		collectors.NewGoCollector(),
		collectors.NewProcessCollector(collectors.ProcessCollectorOpts{}),
	)

	constLabels := prometheus.Labels{"app": serviceName}

	m := &Metrics{
		Registry: reg,
		ZoneCCU: prometheus.NewGaugeVec(prometheus.GaugeOpts{
			Name:        "iu_zone_ccu",
			Help:        "Concurrent players (CCU) per Zone and Channel.",
			ConstLabels: constLabels,
		}, []string{"zone_id", "channel_id"}),

		ZoneServerTickMs: prometheus.NewHistogramVec(prometheus.HistogramOpts{
			Name:        "iu_zone_server_tick_ms",
			Help:        "UE5 Dedicated Server frame simulation duration (ms) at 30Hz (33.33ms budget).",
			ConstLabels: constLabels,
			Buckets:     []float64{4, 8, 12, 16, 20, 25, 30, 33.33, 40, 50, 75, 100},
		}, []string{"zone_id", "channel_id"}),

		GrpcRequestDurationSeconds: prometheus.NewHistogramVec(prometheus.HistogramOpts{
			Name:        "iu_grpc_request_duration_seconds",
			Help:        "Internal gRPC RPC latency in seconds.",
			ConstLabels: constLabels,
			Buckets:     []float64{0.001, 0.0025, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0},
		}, []string{"grpc_service", "grpc_method", "grpc_code"}),

		LootDropsTotal: prometheus.NewCounterVec(prometheus.CounterOpts{
			Name:        "iu_loot_drops_total",
			Help:        "Total dropped items by zone, source type, and rarity.",
			ConstLabels: constLabels,
		}, []string{"zone_id", "source_type", "rarity"}),

		CurrencyFlowTotal: prometheus.NewCounterVec(prometheus.CounterOpts{
			Name:        "iu_currency_flow_total",
			Help:        "Total currency minted and sunk by currency type, direction, and source.",
			ConstLabels: constLabels,
		}, []string{"currency", "direction", "source"}),

		FencingConflictRejectedTotal: prometheus.NewCounterVec(prometheus.CounterOpts{
			Name:        "iu_fencing_conflict_rejected_total",
			Help:        "Write operations rejected by PostgreSQL/Redis due to stale fencing_epoch.",
			ConstLabels: constLabels,
		}, []string{"zone_id", "operation"}),

		DbTxDurationSeconds: prometheus.NewHistogramVec(prometheus.HistogramOpts{
			Name:        "iu_db_tx_duration_seconds",
			Help:        "ACID PostgreSQL transaction execution duration by tx_name and status.",
			ConstLabels: constLabels,
			Buckets:     []float64{0.0005, 0.001, 0.0025, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25},
		}, []string{"tx_name", "status"}),

		EnhancementAttemptsTotal: prometheus.NewCounterVec(prometheus.CounterOpts{
			Name:        "iu_enhancement_attempts_total",
			Help:        "Equipment enhancement attempts by target_level and outcome.",
			ConstLabels: constLabels,
		}, []string{"target_level", "outcome"}),

		TradeSessionsTotal: prometheus.NewCounterVec(prometheus.CounterOpts{
			Name:        "iu_trade_sessions_total",
			Help:        "Player-to-player 2-phase trade outcomes.",
			ConstLabels: constLabels,
		}, []string{"outcome"}),
	}

	reg.MustRegister(
		m.ZoneCCU,
		m.ZoneServerTickMs,
		m.GrpcRequestDurationSeconds,
		m.LootDropsTotal,
		m.CurrencyFlowTotal,
		m.FencingConflictRejectedTotal,
		m.DbTxDurationSeconds,
		m.EnhancementAttemptsTotal,
		m.TradeSessionsTotal,
	)
	return m
}

// ObserveTxDuration implements postgres.TxObserver.
func (m *Metrics) ObserveTxDuration(txName string, status string, duration time.Duration) {
	m.DbTxDurationSeconds.WithLabelValues(txName, status).Observe(duration.Seconds())
}
