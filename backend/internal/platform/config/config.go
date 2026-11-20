package config

import (
	"fmt"
	"os"
	"strconv"
	"strings"
)

const (
	ServiceGateway     = "gateway"
	ServiceGameAPI     = "gameapi"
	ServiceCoordinator = "coordinator"
)

// Config holds runtime configuration loaded from IU_* environment variables.
type Config struct {
	ServiceName                string
	Env                        string
	LogLevel                   string
	HTTPListenAddr             string
	GRPCListenAddr             string
	MetricsListenAddr          string
	PostgresDSN                string
	RedisAddr                  string
	NATSURL                    string
	GameDataDir                string
	GameAPIGRPCAddr            string
	CoordinatorGRPCAddr        string
	JWTPublicKeyHex            string
	JWTPrivateKeySeedHex       string
	InternalServerHMACSecret   string
	DedicatedServerPublicAddr  string
	AllowPlaintextInternalGRPC bool
	AllowOptionalDeps          bool
	ZoneSoftCapCCU             int
	ZoneHardCapCCU             int
	DefaultJoinZone            string
	VerticalSliceMode          bool
	OTLPEndpoint               string
}

// Load parses configuration for the given service role and validates required fields.
func Load(serviceName string) (Config, error) {
	defaultGRPC := ":50051"
	defaultMetrics := ":9091"
	switch serviceName {
	case ServiceGateway:
		defaultGRPC = ""
		defaultMetrics = ":9090"
	case ServiceGameAPI:
		defaultGRPC = ":50051"
		defaultMetrics = ":9091"
	case ServiceCoordinator:
		defaultGRPC = ":50052"
		defaultMetrics = ":9092"
	default:
		return Config{}, fmt.Errorf("unknown service %q", serviceName)
	}

	softCap, err := envInt("IU_ZONE_SOFT_CAP_CCU", 60)
	if err != nil {
		return Config{}, err
	}
	hardCap, err := envInt("IU_ZONE_HARD_CAP_CCU", 80)
	if err != nil {
		return Config{}, err
	}
	allowPlaintext, err := envBool("IU_ALLOW_PLAINTEXT_INTERNAL_GRPC", false)
	if err != nil {
		return Config{}, err
	}
	allowOptional, err := envBool("IU_ALLOW_OPTIONAL_DEPS", false)
	if err != nil {
		return Config{}, err
	}
	vsMode, err := envBool("VERTICAL_SLICE_MODE", true)
	if err != nil {
		return Config{}, err
	}

	cfg := Config{
		ServiceName:                serviceName,
		Env:                        envOrDefault("IU_ENV", "local"),
		LogLevel:                   envOrDefault("IU_LOG_LEVEL", "info"),
		HTTPListenAddr:             envOrDefault("IU_HTTP_LISTEN_ADDR", ":8080"),
		GRPCListenAddr:             envOrDefault("IU_GRPC_LISTEN_ADDR", defaultGRPC),
		MetricsListenAddr:          envOrDefault("IU_METRICS_LISTEN_ADDR", defaultMetrics),
		PostgresDSN:                envOrDefault("IU_PG_DSN", ""),
		RedisAddr:                  envOrDefault("IU_REDIS_ADDR", "127.0.0.1:6379"),
		NATSURL:                    envOrDefault("IU_NATS_URL", "nats://127.0.0.1:4222"),
		GameDataDir:                envOrDefault("IU_GAMEDATA_DIR", ""),
		GameAPIGRPCAddr:            envOrDefault("IU_GAMEAPI_GRPC_ADDR", "127.0.0.1:50051"),
		CoordinatorGRPCAddr:        envOrDefault("IU_COORDINATOR_GRPC_ADDR", "127.0.0.1:50052"),
		JWTPublicKeyHex:            envOrDefault("IU_JWT_PUBLIC_KEY_HEX", ""),
		JWTPrivateKeySeedHex:       envOrDefault("IU_JWT_PRIVATE_KEY_SEED_HEX", ""),
		InternalServerHMACSecret:   envOrDefault("IU_SERVER_HMAC_SECRET", "iu_step3_internal_server_hmac_2026"),
		DedicatedServerPublicAddr:  envOrDefault("IU_DEDICATED_SERVER_ADDR", "127.0.0.1:7777"),
		AllowPlaintextInternalGRPC: allowPlaintext,
		AllowOptionalDeps:          allowOptional,
		ZoneSoftCapCCU:             softCap,
		ZoneHardCapCCU:             hardCap,
		DefaultJoinZone:            envOrDefault("IU_DEFAULT_JOIN_ZONE", "zone.p01_highland_01"),
		VerticalSliceMode:          vsMode,
		OTLPEndpoint:               envOrDefault("IU_OTEL_EXPORTER_OTLP_ENDPOINT", ""),
	}

	if err := cfg.Validate(); err != nil {
		return Config{}, err
	}
	return cfg, nil
}

// Validate verifies service-specific configuration invariants.
func (c Config) Validate() error {
	if c.MetricsListenAddr == "" {
		return fmt.Errorf("IU_METRICS_LISTEN_ADDR must not be empty")
	}
	if c.ZoneSoftCapCCU <= 0 || c.ZoneHardCapCCU < c.ZoneSoftCapCCU {
		return fmt.Errorf("invalid zone cap configuration: soft=%d hard=%d", c.ZoneSoftCapCCU, c.ZoneHardCapCCU)
	}
	if !c.AllowOptionalDeps {
		if c.RedisAddr == "" {
			return fmt.Errorf("IU_REDIS_ADDR is required for %s (unless IU_ALLOW_OPTIONAL_DEPS=true)", c.ServiceName)
		}
		if c.NATSURL == "" {
			return fmt.Errorf("IU_NATS_URL is required for %s (unless IU_ALLOW_OPTIONAL_DEPS=true)", c.ServiceName)
		}
	}
	switch c.ServiceName {
	case ServiceGameAPI:
		if c.GRPCListenAddr == "" {
			return fmt.Errorf("IU_GRPC_LISTEN_ADDR must not be empty for %s", c.ServiceName)
		}
		if c.PostgresDSN == "" && !c.AllowOptionalDeps {
			return fmt.Errorf("IU_PG_DSN is required for %s (unless IU_ALLOW_OPTIONAL_DEPS=true)", c.ServiceName)
		}
	case ServiceCoordinator:
		if c.GRPCListenAddr == "" {
			return fmt.Errorf("IU_GRPC_LISTEN_ADDR must not be empty for %s", c.ServiceName)
		}
	case ServiceGateway:
		if c.HTTPListenAddr == "" {
			return fmt.Errorf("IU_HTTP_LISTEN_ADDR must not be empty for %s", c.ServiceName)
		}
	default:
		return fmt.Errorf("unknown service %q", c.ServiceName)
	}
	return nil
}

// NATSStreamReplicas returns 1 for local single-node development and 3 otherwise.
func (c Config) NATSStreamReplicas() int {
	if strings.EqualFold(c.Env, "local") {
		return 1
	}
	return 3
}

func envOrDefault(key, def string) string {
	if v, ok := os.LookupEnv(key); ok {
		return strings.TrimSpace(v)
	}
	return def
}

func envInt(key string, def int) (int, error) {
	v, ok := os.LookupEnv(key)
	if !ok || strings.TrimSpace(v) == "" {
		return def, nil
	}
	n, err := strconv.Atoi(strings.TrimSpace(v))
	if err != nil {
		return 0, fmt.Errorf("parse %s: %w", key, err)
	}
	return n, nil
}

func envBool(key string, def bool) (bool, error) {
	v, ok := os.LookupEnv(key)
	if !ok || strings.TrimSpace(v) == "" {
		return def, nil
	}
	b, err := strconv.ParseBool(strings.TrimSpace(v))
	if err != nil {
		return false, fmt.Errorf("parse %s: %w", key, err)
	}
	return b, nil
}
