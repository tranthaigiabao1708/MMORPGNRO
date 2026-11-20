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

	t.Run("empty_dir", func(t *testing.T) {
		got, err := computeGameDataHashTolerant("", logger)
		if err != nil || got != "" {
			t.Fatalf("expected empty hash and nil err, got (%q, %v)", got, err)
		}
	})

	t.Run("prefers_sources_core_over_core", func(t *testing.T) {
		root := t.TempDir()
		sourcesCore := filepath.Join(root, "sources", "core")
		coreDir := filepath.Join(root, "core")
		if err := os.MkdirAll(sourcesCore, 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.MkdirAll(coreDir, 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(sourcesCore, "a.json"), []byte(`{"v":1}`), 0o644); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(coreDir, "a.json"), []byte(`{"v":2}`), 0o644); err != nil {
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
			t.Fatalf("expected hash %q from sources/core, got %q", want, got)
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

	t.Run("avoids_hashing_when_sources_exists_without_core", func(t *testing.T) {
		root := t.TempDir()
		packsDir := filepath.Join(root, "sources", "packs")
		if err := os.MkdirAll(packsDir, 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(packsDir, "manifest.json"), []byte(`{"id":"pack"}`), 0o644); err != nil {
			t.Fatal(err)
		}

		got, err := computeGameDataHashTolerant(root, logger)
		if err != nil {
			t.Fatalf("unexpected err: %v", err)
		}
		if got != "" {
			t.Fatalf("expected empty hash when sources/ exists without core/, got %q", got)
		}
	})

	t.Run("falls_back_to_dir_when_direct_core_dir", func(t *testing.T) {
		direct := t.TempDir()
		if err := os.WriteFile(filepath.Join(direct, "races.json"), []byte(`{"races":[]}`), 0o644); err != nil {
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
