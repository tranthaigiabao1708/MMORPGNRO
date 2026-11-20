package config

import (
	"strings"
	"testing"
)

func TestLoadRejectsUnknownService(t *testing.T) {
	_, err := Load("unknown-svc")
	if err == nil {
		t.Fatal("expected error for unknown service name, got nil")
	}
	if !strings.Contains(err.Error(), `unknown service "unknown-svc"`) {
		t.Fatalf("unexpected error: %v", err)
	}
}

func TestLoadStrictDependencyValidation(t *testing.T) {
	for _, svc := range []string{ServiceGateway, ServiceGameAPI, ServiceCoordinator} {
		t.Run(svc+"_empty_redis_rejected", func(t *testing.T) {
			t.Setenv("IU_ALLOW_OPTIONAL_DEPS", "false")
			t.Setenv("IU_REDIS_ADDR", "")
			t.Setenv("IU_NATS_URL", "nats://127.0.0.1:4222")
			t.Setenv("IU_PG_DSN", "postgres://iu:pw@127.0.0.1:5433/iu")

			_, err := Load(svc)
			if err == nil || !strings.Contains(err.Error(), "IU_REDIS_ADDR is required") {
				t.Fatalf("expected IU_REDIS_ADDR required error for %s, got %v", svc, err)
			}
		})

		t.Run(svc+"_empty_nats_rejected", func(t *testing.T) {
			t.Setenv("IU_ALLOW_OPTIONAL_DEPS", "false")
			t.Setenv("IU_REDIS_ADDR", "127.0.0.1:6379")
			t.Setenv("IU_NATS_URL", "")
			t.Setenv("IU_PG_DSN", "postgres://iu:pw@127.0.0.1:5433/iu")

			_, err := Load(svc)
			if err == nil || !strings.Contains(err.Error(), "IU_NATS_URL is required") {
				t.Fatalf("expected IU_NATS_URL required error for %s, got %v", svc, err)
			}
		})

		t.Run(svc+"_optional_deps_allows_empty", func(t *testing.T) {
			t.Setenv("IU_ALLOW_OPTIONAL_DEPS", "true")
			t.Setenv("IU_REDIS_ADDR", "")
			t.Setenv("IU_NATS_URL", "")
			t.Setenv("IU_PG_DSN", "")

			cfg, err := Load(svc)
			if err != nil {
				t.Fatalf("expected Load(%s) with IU_ALLOW_OPTIONAL_DEPS=true to succeed, got %v", svc, err)
			}
			if cfg.ServiceName != svc {
				t.Fatalf("expected ServiceName=%s, got %s", svc, cfg.ServiceName)
			}
		})
	}

	t.Run("gameapi_empty_pg_dsn_rejected", func(t *testing.T) {
		t.Setenv("IU_ALLOW_OPTIONAL_DEPS", "false")
		t.Setenv("IU_REDIS_ADDR", "127.0.0.1:6379")
		t.Setenv("IU_NATS_URL", "nats://127.0.0.1:4222")
		t.Setenv("IU_PG_DSN", "")

		_, err := Load(ServiceGameAPI)
		if err == nil || !strings.Contains(err.Error(), "IU_PG_DSN is required") {
			t.Fatalf("expected IU_PG_DSN required error for gameapi, got %v", err)
		}
	})
}
