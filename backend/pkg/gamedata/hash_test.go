package gamedata_test

import (
	"errors"
	"os"
	"path/filepath"
	"testing"

	"github.com/infinite-universe/backend/pkg/gamedata"
)

func TestComputeCanonicalGameDataHash_DeterministicCRLFNormalization(t *testing.T) {
	dirLF := t.TempDir()
	dirCRLF := t.TempDir()

	if err := os.MkdirAll(filepath.Join(dirLF, "sub"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(dirCRLF, "sub"), 0o755); err != nil {
		t.Fatal(err)
	}

	writePair := func(rel, content string) {
		t.Helper()
		if err := os.WriteFile(filepath.Join(dirLF, filepath.FromSlash(rel)), []byte(content), 0o644); err != nil {
			t.Fatal(err)
		}
		crlfContent := ""
		for i := 0; i < len(content); i++ {
			if content[i] == '\n' {
				crlfContent += "\r\n"
			} else {
				crlfContent += string(content[i])
			}
		}
		if err := os.WriteFile(filepath.Join(dirCRLF, filepath.FromSlash(rel)), []byte(crlfContent), 0o644); err != nil {
			t.Fatal(err)
		}
	}

	writePair("b.json", "{\n  \"id\": \"zone.p01_highland_01\"\n}\n")
	writePair("sub/a.JSON", "{\n  \"id\": \"race.zenith_warrior\"\n}\n")
	// Non-.json files must be ignored.
	if err := os.WriteFile(filepath.Join(dirLF, "notes.txt"), []byte("ignore me"), 0o644); err != nil {
		t.Fatal(err)
	}

	h1, err := gamedata.ComputeCanonicalGameDataHash(dirLF)
	if err != nil {
		t.Fatalf("hash LF dir: %v", err)
	}
	h2, err := gamedata.ComputeCanonicalGameDataHash(dirCRLF)
	if err != nil {
		t.Fatalf("hash CRLF dir: %v", err)
	}

	if len(h1) != 64 {
		t.Fatalf("expected 64-char sha256 hex digest, got %d (%q)", len(h1), h1)
	}
	if h1 != h2 {
		t.Fatalf("CRLF normalization mismatch: LF=%s CRLF=%s", h1, h2)
	}

	// Mutating a file must change the hash.
	if err := os.WriteFile(filepath.Join(dirLF, "b.json"), []byte("{\"id\":\"changed\"}\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	h3, err := gamedata.ComputeCanonicalGameDataHash(dirLF)
	if err != nil {
		t.Fatal(err)
	}
	if h3 == h1 {
		t.Fatal("expected hash to change when json file content changes")
	}
}

func TestComputeCanonicalGameDataHash_Errors(t *testing.T) {
	emptyDir := t.TempDir()
	if err := os.WriteFile(filepath.Join(emptyDir, "readme.md"), []byte("# empty"), 0o644); err != nil {
		t.Fatal(err)
	}

	_, err := gamedata.ComputeCanonicalGameDataHash(emptyDir)
	if !errors.Is(err, gamedata.ErrNoGameDataFiles) {
		t.Fatalf("expected ErrNoGameDataFiles, got %v", err)
	}

	_, err = gamedata.ComputeCanonicalGameDataHash(filepath.Join(emptyDir, "does_not_exist"))
	if !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("expected os.ErrNotExist, got %v", err)
	}
}
