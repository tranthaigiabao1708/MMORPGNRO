package gamedata

import (
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"slices"
	"strings"
)

// ErrNoGameDataFiles is returned when no .json files exist inside the target gamedata directory.
var ErrNoGameDataFiles = errors.New("no json files found in gamedata dir")

// ComputeCanonicalGameDataHash scans all .json files under rootDir in sorted relative path order,
// normalizes CRLF -> LF, and computes a deterministic SHA-256 hex digest identical across Windows and Linux.
func ComputeCanonicalGameDataHash(rootDir string) (string, error) {
	var relPaths []string

	err := filepath.WalkDir(rootDir, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() || !strings.HasSuffix(strings.ToLower(d.Name()), ".json") {
			return nil
		}
		rel, err := filepath.Rel(rootDir, path)
		if err != nil {
			return err
		}
		relPaths = append(relPaths, filepath.ToSlash(rel))
		return nil
	})
	if err != nil {
		return "", fmt.Errorf("walk gamedata dir: %w", err)
	}
	if len(relPaths) == 0 {
		return "", fmt.Errorf("%w: %s", ErrNoGameDataFiles, rootDir)
	}

	slices.Sort(relPaths)
	hasher := sha256.New()

	for _, rel := range relPaths {
		raw, err := os.ReadFile(filepath.Join(rootDir, filepath.FromSlash(rel)))
		if err != nil {
			return "", fmt.Errorf("read gamedata %s: %w", rel, err)
		}
		normalized := strings.ReplaceAll(string(raw), "\r\n", "\n")
		_, _ = hasher.Write([]byte("FILE:" + rel + "\n"))
		_, _ = hasher.Write([]byte(normalized))
		_, _ = hasher.Write([]byte("\n---\n"))
	}

	return hex.EncodeToString(hasher.Sum(nil)), nil
}
