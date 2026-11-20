package main

import (
	"io"
	"log/slog"
	"os"
	"path/filepath"
	"testing"

	"github.com/infinite-universe/backend/pkg/gamedata"
)

func TestComputeGameDataHashTolerant(t *testing.T) {
	logger := slog.New(slog.NewJSONHandler(io.Discard, nil))

	t.Run("prefers_sources_core", func(t *testing.T) {
		root := t.TempDir()
		sourcesCore := filepath.Join(root, "sources", "core")
		if err := os.MkdirAll(sourcesCore, 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(sourcesCore, "a.json"), []byte(`{"v":1}`), 0o644); err != nil {
			t.Fatal(err)
		}

		want, err := gamedata.ComputeCanonicalGameDataHash(sourcesCore)
		if err != nil {
			t.Fatal(err)
		}
		got, err := computeGameDataHashTolerant(root, logger)
		if err != nil {
			t.Fatalf("unexpected err: %v", err)
		}
		if got != want {
			t.Fatalf("expected hash %q, got %q", want, got)
		}
	})

	t.Run("avoids_hashing_schemas_when_sources_core_missing", func(t *testing.T) {
		root := t.TempDir()
		schemasDir := filepath.Join(root, "schemas")
		if err := os.MkdirAll(schemasDir, 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(schemasDir, "race.schema.json"), []byte(`{"type":"object"}`), 0o644); err != nil {
			t.Fatal(err)
		}

		got, err := computeGameDataHashTolerant(root, logger)
		if err != nil {
			t.Fatalf("unexpected err: %v", err)
		}
		if got != "" {
			t.Fatalf("expected empty hash when only schemas/ exists, got %q", got)
		}
	})

	t.Run("falls_back_to_dir_when_neither_schemas_nor_sources_exists", func(t *testing.T) {
		direct := t.TempDir()
		if err := os.WriteFile(filepath.Join(direct, "zones.json"), []byte(`{"zones":[]}`), 0o644); err != nil {
			t.Fatal(err)
		}

		want, err := gamedata.ComputeCanonicalGameDataHash(direct)
		if err != nil {
			t.Fatal(err)
		}
		got, err := computeGameDataHashTolerant(direct, logger)
		if err != nil {
			t.Fatalf("unexpected err: %v", err)
		}
		if got != want {
			t.Fatalf("expected hash %q, got %q", want, got)
		}
	})
}
