package e2e_test

import (
	"bufio"
	"bytes"
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/csv"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

var (
	cachedBinDirOnce sync.Once
	cachedBinDir     string

	cachedDataCompilerOnce sync.Once
	cachedDataCompilerPath string
	cachedDataCompilerErr  error
)

func TestMain(m *testing.M) {
	code := m.Run()
	if cachedBinDir != "" {
		_ = os.RemoveAll(cachedBinDir)
	}
	os.Exit(code)
}

func getSharedBinDir() (string, error) {
	var mkErr error
	cachedBinDirOnce.Do(func() {
		dir, err := os.MkdirTemp("", "iu-e2e-bins-*")
		if err != nil {
			mkErr = err
			return
		}
		cachedBinDir = dir
	})
	if mkErr != nil {
		return "", mkErr
	}
	if cachedBinDir == "" {
		return "", errors.New("shared binary directory not initialized")
	}
	return cachedBinDir, nil
}

type e2eConfig struct {
	repoRoot              string
	backendDir            string
	deployDir             string
	gamedataDir           string
	composeFile           string
	pgContainer           string
	redisContainer        string
	natsContainer         string
	pgHostPort            string
	pgUser                string
	pgPassword            string
	pgDB                  string
	redisAddr             string
	gatewayHTTPURL        string
	gatewayMetricsURL     string
	gameapiMetricsURL     string
	coordinatorMetricsURL string
	natsMonitorURL        string
	prometheusURL         string
	grafanaURL            string
	skipDocker            bool
}

func envOrDefault(key, def string) string {
	if v := strings.TrimSpace(os.Getenv(key)); v != "" {
		return v
	}
	return def
}

func findRepoRoot() (string, error) {
	if v := strings.TrimSpace(os.Getenv("IU_E2E_REPO_ROOT")); v != "" {
		abs, err := filepath.Abs(v)
		if err == nil {
			if _, statErr := os.Stat(filepath.Join(abs, "README.md")); statErr == nil {
				return abs, nil
			}
		}
	}
	wd, err := os.Getwd()
	if err != nil {
		return "", err
	}
	cur := wd
	for {
		if _, err := os.Stat(filepath.Join(cur, "README.md")); err == nil {
			if _, err2 := os.Stat(filepath.Join(cur, "tests", "e2e", "go.mod")); err2 == nil {
				return cur, nil
			}
		}
		parent := filepath.Dir(cur)
		if parent == cur {
			break
		}
		cur = parent
	}
	return "", fmt.Errorf("could not locate repo root with README.md and tests/e2e/go.mod starting from %q", wd)
}

func loadConfig(t *testing.T) e2eConfig {
	t.Helper()
	root, err := findRepoRoot()
	if err != nil {
		t.Fatalf("loadConfig: %v", err)
	}
	composeRel := envOrDefault("IU_E2E_COMPOSE_FILE", filepath.Join("deploy", "docker-compose.local.yml"))
	composePath := composeRel
	if !filepath.IsAbs(composePath) {
		composePath = filepath.Join(root, composeRel)
	}
	pgPort := envOrDefault("IU_E2E_PG_HOST_PORT", "5433")

	return e2eConfig{
		repoRoot:              root,
		backendDir:            filepath.Join(root, "backend"),
		deployDir:             filepath.Join(root, "deploy"),
		gamedataDir:           filepath.Join(root, "gamedata"),
		composeFile:           composePath,
		pgContainer:           envOrDefault("IU_E2E_PG_CONTAINER", "iu-postgres"),
		redisContainer:        envOrDefault("IU_E2E_REDIS_CONTAINER", "iu-redis"),
		natsContainer:         envOrDefault("IU_E2E_NATS_CONTAINER", "iu-nats"),
		pgHostPort:            pgPort,
		pgUser:                envOrDefault("IU_E2E_PG_USER", "iu_admin"),
		pgPassword:            envOrDefault("IU_E2E_PG_PASSWORD", "iu_dev_secret_2026"),
		pgDB:                  envOrDefault("IU_E2E_PG_DB", "infinite_universe"),
		redisAddr:             envOrDefault("IU_E2E_REDIS_ADDR", "127.0.0.1:6379"),
		gatewayHTTPURL:        envOrDefault("IU_E2E_GATEWAY_HTTP_URL", "http://127.0.0.1:8080"),
		gatewayMetricsURL:     envOrDefault("IU_E2E_GATEWAY_METRICS_URL", "http://127.0.0.1:9090"),
		gameapiMetricsURL:     envOrDefault("IU_E2E_GAMEAPI_METRICS_URL", "http://127.0.0.1:9091"),
		coordinatorMetricsURL: envOrDefault("IU_E2E_COORDINATOR_METRICS_URL", "http://127.0.0.1:9092"),
		natsMonitorURL:        envOrDefault("IU_E2E_NATS_MONITOR_URL", "http://127.0.0.1:8222"),
		prometheusURL:         envOrDefault("IU_E2E_PROMETHEUS_URL", "http://127.0.0.1:9095"),
		grafanaURL:            envOrDefault("IU_E2E_GRAFANA_URL", "http://127.0.0.1:3000"),
		skipDocker:            strings.TrimSpace(os.Getenv("IU_E2E_SKIP_DOCKER")) == "1",
	}
}

func (c e2eConfig) hostPGDSN() string {
	return fmt.Sprintf("postgres://%s:%s@127.0.0.1:%s/%s?sslmode=disable",
		c.pgUser, c.pgPassword, c.pgHostPort, c.pgDB)
}

func (c e2eConfig) dockerPGDSN() string {
	return fmt.Sprintf("postgres://%s:%s@postgres:5432/%s?sslmode=disable",
		c.pgUser, c.pgPassword, c.pgDB)
}

func (c e2eConfig) dockerPGDSNForDB(dbName string) string {
	return fmt.Sprintf("postgres://%s:%s@postgres:5432/%s?sslmode=disable",
		c.pgUser, c.pgPassword, dbName)
}

func requireDirExists(t *testing.T, path, desc string) {
	t.Helper()
	info, err := os.Stat(path)
	if err != nil {
		t.Fatalf("required %s directory missing (%s): %v", desc, path, err)
	}
	if !info.IsDir() {
		t.Fatalf("required %s path is not a directory: %s", desc, path)
	}
}

func requireFileExists(t *testing.T, path, desc string) {
	t.Helper()
	info, err := os.Stat(path)
	if err != nil {
		t.Fatalf("required %s file missing (%s): %v", desc, path, err)
	}
	if info.IsDir() {
		t.Fatalf("required %s path is a directory, expected file: %s", desc, path)
	}
}

func skipIfDockerDisabled(t *testing.T, cfg e2eConfig) {
	t.Helper()
	if cfg.skipDocker {
		t.Skip("skipping live Docker/runtime check because IU_E2E_SKIP_DOCKER=1")
	}
	stdout, stderr, code, err := runCmd(t, 15*time.Second, "docker", "info", "--format", "{{.ServerVersion}}")
	if err != nil || code != 0 || strings.TrimSpace(stdout) == "" {
		t.Fatalf("Docker daemon unavailable (set -SkipDocker / IU_E2E_SKIP_DOCKER=1 to skip runtime checks): code=%d err=%v stdout=%q stderr=%q",
			code, err, strings.TrimSpace(stdout), strings.TrimSpace(stderr))
	}
}

type cmdResult struct {
	stdout   string
	stderr   string
	exitCode int
	err      error
}

func runCmd(t *testing.T, timeout time.Duration, name string, args ...string) (string, string, int, error) {
	t.Helper()
	res := runCmdFull(t, "", nil, timeout, "", name, args...)
	return res.stdout, res.stderr, res.exitCode, res.err
}

func runCmdInDir(t *testing.T, dir string, envOverrides map[string]string, timeout time.Duration, name string, args ...string) (string, string, int, error) {
	t.Helper()
	res := runCmdFull(t, dir, envOverrides, timeout, "", name, args...)
	return res.stdout, res.stderr, res.exitCode, res.err
}

func runCmdWithStdin(t *testing.T, dir string, envOverrides map[string]string, timeout time.Duration, stdinText, name string, args ...string) (string, string, int, error) {
	t.Helper()
	res := runCmdFull(t, dir, envOverrides, timeout, stdinText, name, args...)
	return res.stdout, res.stderr, res.exitCode, res.err
}

func runCmdFull(t *testing.T, dir string, envOverrides map[string]string, timeout time.Duration, stdinText, name string, args ...string) cmdResult {
	t.Helper()
	if timeout <= 0 {
		timeout = 60 * time.Second
	}
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	cmd := exec.CommandContext(ctx, name, args...)
	if dir != "" {
		cmd.Dir = dir
	}
	if len(envOverrides) > 0 {
		env := os.Environ()
		for k, v := range envOverrides {
			env = setOrReplaceEnv(env, k, v)
		}
		cmd.Env = env
	}
	if stdinText != "" {
		cmd.Stdin = strings.NewReader(stdinText)
	}
	var stdoutBuf, stderrBuf bytes.Buffer
	cmd.Stdout = &stdoutBuf
	cmd.Stderr = &stderrBuf

	err := cmd.Run()
	exitCode := 0
	if err != nil {
		var exitErr *exec.ExitError
		if errors.As(err, &exitErr) {
			exitCode = exitErr.ExitCode()
		} else if errors.Is(ctx.Err(), context.DeadlineExceeded) {
			exitCode = -1
			err = fmt.Errorf("command %q timed out after %s: %w", name, timeout, err)
		} else {
			exitCode = -1
		}
	}
	return cmdResult{
		stdout:   stdoutBuf.String(),
		stderr:   stderrBuf.String(),
		exitCode: exitCode,
		err:      err,
	}
}

func setOrReplaceEnv(env []string, key, val string) []string {
	prefix := key + "="
	upperPrefix := strings.ToUpper(prefix)
	out := make([]string, 0, len(env)+1)
	replaced := false
	for _, entry := range env {
		if strings.HasPrefix(strings.ToUpper(entry), upperPrefix) {
			if !replaced {
				out = append(out, prefix+val)
				replaced = true
			}
			continue
		}
		out = append(out, entry)
	}
	if !replaced {
		out = append(out, prefix+val)
	}
	return out
}

var shortTagSeq atomic.Uint64

// shortRunTag returns a compact 8-character lowercase alphanumeric tag ([0-9a-z]{8})
// combining an atomic sequence counter, crypto/rand entropy, and timestamp bits so
// prefixed SQL accounts.username (<=32) and characters.character_name (<=20)
// always satisfy PostgreSQL CHECK constraints.
func shortRunTag(t *testing.T) string {
	t.Helper()
	const alphabet = "0123456789abcdefghijklmnopqrstuvwxyz"
	seq := shortTagSeq.Add(1)
	var rnd [4]byte
	if _, err := rand.Read(rnd[:]); err != nil {
		nano := uint64(time.Now().UnixNano())
		rnd[0] = byte(nano)
		rnd[1] = byte(nano >> 8)
		rnd[2] = byte(nano >> 16)
		rnd[3] = byte(nano >> 24)
	}
	nano := uint64(time.Now().UnixNano())
	randVal := uint64(rnd[0]) | uint64(rnd[1])<<8 | uint64(rnd[2])<<16 | uint64(rnd[3])<<24
	randVal ^= (nano & 0xffffffff)

	var buf [8]byte
	s := seq % (36 * 36 * 36)
	buf[2] = alphabet[s%36]
	s /= 36
	buf[1] = alphabet[s%36]
	s /= 36
	buf[0] = alphabet[s%36]

	r := randVal % (36 * 36 * 36 * 36 * 36)
	for i := 7; i >= 3; i-- {
		buf[i] = alphabet[r%36]
		r /= 36
	}
	return string(buf[:])
}

func uniqueRunID(t *testing.T) string {
	t.Helper()
	var buf [4]byte
	if _, err := rand.Read(buf[:]); err != nil {
		return fmt.Sprintf("e2e_%d", time.Now().UnixNano())
	}
	return fmt.Sprintf("e2e_%d_%s", time.Now().UnixNano(), hex.EncodeToString(buf[:]))
}

func psqlExec(t *testing.T, cfg e2eConfig, dbName, sqlText string) (string, string, int, error) {
	t.Helper()
	if dbName == "" {
		dbName = cfg.pgDB
	}
	return runCmdWithStdin(
		t, "", nil, 30*time.Second, sqlText,
		"docker", "exec", "-i", cfg.pgContainer,
		"psql", "-U", cfg.pgUser, "-d", dbName, "-v", "ON_ERROR_STOP=1", "-Atq",
	)
}

func psqlExecOrFatal(t *testing.T, cfg e2eConfig, dbName, sqlText string) string {
	t.Helper()
	stdout, stderr, code, err := psqlExec(t, cfg, dbName, sqlText)
	if err != nil || code != 0 {
		t.Fatalf("psqlExec failed (db=%s code=%d err=%v):\nSQL:\n%s\nSTDOUT:\n%s\nSTDERR:\n%s",
			dbName, code, err, sqlText, stdout, stderr)
	}
	return strings.TrimSpace(stdout)
}

func psqlTxRollback(t *testing.T, cfg e2eConfig, dbName, sqlBody string) string {
	t.Helper()
	wrapped := "BEGIN;\n" + sqlBody + "\nROLLBACK;\n"
	return psqlExecOrFatal(t, cfg, dbName, wrapped)
}

func withTempPGDatabase(t *testing.T, cfg e2eConfig, applyUpMigration bool) string {
	t.Helper()
	dbName := "iu_" + uniqueRunID(t)
	if len(dbName) > 60 {
		dbName = dbName[:60]
	}
	psqlExecOrFatal(t, cfg, cfg.pgDB, fmt.Sprintf(`CREATE DATABASE "%s";`, dbName))
	t.Cleanup(func() {
		_, _, _, _ = runCmdWithStdin(
			t, "", nil, 20*time.Second,
			fmt.Sprintf(`DROP DATABASE IF EXISTS "%s" WITH (FORCE);`, dbName),
			"docker", "exec", "-i", cfg.pgContainer,
			"psql", "-U", cfg.pgUser, "-d", cfg.pgDB, "-v", "ON_ERROR_STOP=1", "-Atq",
		)
	})
	if applyUpMigration {
		upPath := filepath.Join(cfg.backendDir, "migrations", "000001_init_core_schema.up.sql")
		raw, err := os.ReadFile(upPath)
		if err != nil {
			t.Fatalf("read up migration %s: %v", upPath, err)
		}
		psqlExecOrFatal(t, cfg, dbName, string(raw))
	}
	return dbName
}

func dialTCPCheck(t *testing.T, addr string, timeout time.Duration) error {
	t.Helper()
	conn, err := net.DialTimeout("tcp", addr, timeout)
	if err != nil {
		return err
	}
	_ = conn.Close()
	return nil
}

func redisExec(t *testing.T, cfg e2eConfig, args ...string) (string, string, int, error) {
	t.Helper()
	fullArgs := append([]string{"exec", cfg.redisContainer, "redis-cli", "--raw"}, args...)
	return runCmd(t, 15*time.Second, "docker", fullArgs...)
}

func redisExecOrFatal(t *testing.T, cfg e2eConfig, args ...string) string {
	t.Helper()
	stdout, stderr, code, err := redisExec(t, cfg, args...)
	if err != nil || code != 0 {
		t.Fatalf("redisExec %v failed (code=%d err=%v): stdout=%q stderr=%q",
			args, code, err, stdout, stderr)
	}
	return strings.TrimRight(stdout, "\r\n")
}

func redisScanMatch(t *testing.T, cfg e2eConfig, pattern string) []string {
	t.Helper()
	cursor := "0"
	var keys []string
	seen := make(map[string]bool)
	for {
		out := redisExecOrFatal(t, cfg, "SCAN", cursor, "MATCH", pattern, "COUNT", "1000")
		normalized := strings.ReplaceAll(strings.TrimRight(out, "\r\n"), "\r\n", "\n")
		if normalized == "" {
			t.Fatalf("unexpected empty Redis SCAN response for pattern %q", pattern)
		}
		lines := strings.Split(normalized, "\n")
		nextCursor := strings.TrimSpace(lines[0])
		for _, line := range lines[1:] {
			k := strings.TrimSpace(line)
			if k != "" && !seen[k] {
				seen[k] = true
				keys = append(keys, k)
			}
		}
		if nextCursor == "0" {
			break
		}
		cursor = nextCursor
	}
	return keys
}

func respPing(t *testing.T, addr string) {
	t.Helper()
	conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
	if err != nil {
		t.Fatalf("TCP dial to Redis %s failed: %v", addr, err)
	}
	defer func() { _ = conn.Close() }()
	_ = conn.SetDeadline(time.Now().Add(5 * time.Second))
	if _, err := conn.Write([]byte("*1\r\n$4\r\nPING\r\n")); err != nil {
		t.Fatalf("write RESP PING to %s failed: %v", addr, err)
	}
	line, err := bufio.NewReader(conn).ReadString('\n')
	if err != nil {
		t.Fatalf("read RESP PING response from %s failed: %v", addr, err)
	}
	if strings.TrimSpace(line) != "+PONG" {
		t.Fatalf("unexpected RESP PING response from %s: %q (want +PONG)", addr, line)
	}
}

func httpDoRaw(t *testing.T, method, url string, headers map[string]string) (int, string, []byte, error) {
	t.Helper()
	client := &http.Client{Timeout: 8 * time.Second}
	req, err := http.NewRequest(method, url, nil)
	if err != nil {
		return 0, "", nil, err
	}
	for k, v := range headers {
		req.Header.Set(k, v)
	}
	resp, err := client.Do(req)
	if err != nil {
		return 0, "", nil, err
	}
	defer func() { _ = resp.Body.Close() }()
	body, readErr := io.ReadAll(resp.Body)
	return resp.StatusCode, resp.Header.Get("Content-Type"), body, readErr
}

func httpGetText(t *testing.T, url string) string {
	t.Helper()
	deadline := time.Now().Add(10 * time.Second)
	var lastErr error
	var lastStatus int
	var lastBody []byte
	for time.Now().Before(deadline) {
		status, _, body, err := httpDoRaw(t, http.MethodGet, url, nil)
		if err == nil && status == http.StatusOK {
			return string(body)
		}
		lastErr = err
		lastStatus = status
		lastBody = body
		time.Sleep(250 * time.Millisecond)
	}
	t.Fatalf("GET %s failed: status=%d err=%v body=%q", url, lastStatus, lastErr, string(lastBody))
	return ""
}

func httpGetJSON(t *testing.T, url string, target any) string {
	t.Helper()
	deadline := time.Now().Add(10 * time.Second)
	var lastErr error
	var lastStatus int
	var lastCT string
	var lastBody []byte
	for time.Now().Before(deadline) {
		status, ct, body, err := httpDoRaw(t, http.MethodGet, url, nil)
		if err == nil && status == http.StatusOK {
			if err := json.Unmarshal(body, target); err != nil {
				t.Fatalf("GET %s returned invalid JSON (%v): %s", url, err, string(body))
			}
			return ct
		}
		lastErr = err
		lastStatus = status
		lastCT = ct
		lastBody = body
		time.Sleep(250 * time.Millisecond)
	}
	t.Fatalf("GET JSON %s failed: status=%d ct=%q err=%v body=%q", url, lastStatus, lastCT, lastErr, string(lastBody))
	return ""
}

func httpGetJSONWithBasicAuth(t *testing.T, url, user, pass string, target any) int {
	t.Helper()
	client := &http.Client{Timeout: 8 * time.Second}
	req, err := http.NewRequest(http.MethodGet, url, nil)
	if err != nil {
		t.Fatalf("build request %s: %v", url, err)
	}
	if user != "" || pass != "" {
		req.SetBasicAuth(user, pass)
	}
	resp, err := client.Do(req)
	if err != nil {
		t.Fatalf("GET %s failed: %v", url, err)
	}
	defer func() { _ = resp.Body.Close() }()
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatalf("read response %s: %v", url, err)
	}
	if resp.StatusCode == http.StatusOK && target != nil {
		if err := json.Unmarshal(body, target); err != nil {
			t.Fatalf("decode JSON from %s: %v\nbody: %s", url, err, string(body))
		}
	}
	return resp.StatusCode
}

func copyDir(t *testing.T, src, dst string) {
	t.Helper()
	if err := copyDirErr(src, dst); err != nil {
		t.Fatalf("copyDir(%q -> %q) failed: %v", src, dst, err)
	}
}

func copyDirErr(src, dst string) error {
	return filepath.WalkDir(src, func(path string, d fs.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		rel, err := filepath.Rel(src, path)
		if err != nil {
			return err
		}
		target := filepath.Join(dst, rel)
		if d.IsDir() {
			return os.MkdirAll(target, 0o755)
		}
		raw, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
			return err
		}
		return os.WriteFile(target, raw, 0o644)
	})
}

func copyGamedataToTemp(t *testing.T, cfg e2eConfig) (tmpRoot, tmpSources, tmpSchemas, tmpOut string) {
	t.Helper()
	srcDir := filepath.Join(cfg.gamedataDir, "sources")
	schemaDir := filepath.Join(cfg.gamedataDir, "schemas")
	requireDirExists(t, srcDir, "gamedata/sources")
	requireDirExists(t, schemaDir, "gamedata/schemas")

	tmpRoot = t.TempDir()
	tmpSources = filepath.Join(tmpRoot, "sources")
	tmpSchemas = filepath.Join(tmpRoot, "schemas")
	tmpOut = filepath.Join(tmpRoot, "build")

	copyDir(t, srcDir, tmpSources)
	copyDir(t, schemaDir, tmpSchemas)
	return tmpRoot, tmpSources, tmpSchemas, tmpOut
}

var idFieldCandidates = []string{
	"skill_id", "race_id", "form_id", "item_id", "set_id",
	"affix_id", "entity_id", "loot_table_id", "zone_id", "planet_id",
	"canonical_id", "id",
}

func mutateRecordInJSONFile(t *testing.T, filePath, targetID string, mutator func(obj map[string]any) error) {
	t.Helper()
	raw, err := os.ReadFile(filePath)
	if err != nil {
		t.Fatalf("read JSON file %s: %v", filePath, err)
	}
	var root any
	if err := json.Unmarshal(raw, &root); err != nil {
		t.Fatalf("unmarshal JSON file %s: %v", filePath, err)
	}
	matched, err := walkAndMutateJSON(root, targetID, mutator)
	if err != nil {
		t.Fatalf("mutate record %q in %s: %v", targetID, filePath, err)
	}
	if !matched {
		t.Fatalf("record %q not found in %s", targetID, filePath)
	}
	out, err := json.MarshalIndent(root, "", "  ")
	if err != nil {
		t.Fatalf("marshal JSON file %s: %v", filePath, err)
	}
	out = append(out, '\n')
	if err := os.WriteFile(filePath, out, 0o644); err != nil {
		t.Fatalf("write mutated JSON file %s: %v", filePath, err)
	}
}

func walkAndMutateJSON(node any, targetID string, mutator func(obj map[string]any) error) (bool, error) {
	if matched, err := walkMutateByIDField(node, targetID, mutator); err != nil || matched {
		return matched, err
	}
	return walkMutateByObjectKey(node, targetID, mutator)
}

func walkMutateByIDField(node any, targetID string, mutator func(obj map[string]any) error) (bool, error) {
	switch v := node.(type) {
	case map[string]any:
		for _, field := range idFieldCandidates {
			if rawID, ok := v[field]; ok {
				if strID, ok := rawID.(string); ok && strID == targetID {
					if err := mutator(v); err != nil {
						return true, err
					}
					return true, nil
				}
			}
		}
		keys := make([]string, 0, len(v))
		for k := range v {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		for _, k := range keys {
			matched, err := walkMutateByIDField(v[k], targetID, mutator)
			if err != nil || matched {
				return matched, err
			}
		}
	case []any:
		for _, elem := range v {
			matched, err := walkMutateByIDField(elem, targetID, mutator)
			if err != nil || matched {
				return matched, err
			}
		}
	}
	return false, nil
}

func walkMutateByObjectKey(node any, targetID string, mutator func(obj map[string]any) error) (bool, error) {
	switch v := node.(type) {
	case map[string]any:
		if child, ok := v[targetID]; ok {
			if childMap, ok := child.(map[string]any); ok {
				if err := mutator(childMap); err != nil {
					return true, err
				}
				return true, nil
			}
		}
		keys := make([]string, 0, len(v))
		for k := range v {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		for _, k := range keys {
			matched, err := walkMutateByObjectKey(v[k], targetID, mutator)
			if err != nil || matched {
				return matched, err
			}
		}
	case []any:
		for _, elem := range v {
			matched, err := walkMutateByObjectKey(elem, targetID, mutator)
			if err != nil || matched {
				return matched, err
			}
		}
	}
	return false, nil
}

func deleteRecordInJSONFile(t *testing.T, filePath, targetID string) {
	t.Helper()
	raw, err := os.ReadFile(filePath)
	if err != nil {
		t.Fatalf("read JSON file %s: %v", filePath, err)
	}
	var root any
	if err := json.Unmarshal(raw, &root); err != nil {
		t.Fatalf("unmarshal JSON file %s: %v", filePath, err)
	}
	newRoot, deleted := walkAndDeleteJSON(root, targetID)
	if !deleted {
		t.Fatalf("record %q not found for deletion in %s", targetID, filePath)
	}
	out, err := json.MarshalIndent(newRoot, "", "  ")
	if err != nil {
		t.Fatalf("marshal JSON file %s: %v", filePath, err)
	}
	out = append(out, '\n')
	if err := os.WriteFile(filePath, out, 0o644); err != nil {
		t.Fatalf("write JSON file %s: %v", filePath, err)
	}
}

func mapHasMatchingIDField(m map[string]any, targetID string) bool {
	for _, field := range idFieldCandidates {
		if strID, ok := m[field].(string); ok && strID == targetID {
			return true
		}
	}
	return false
}

func walkAndDeleteJSON(node any, targetID string) (any, bool) {
	if updated, deleted := walkDeleteByIDField(node, targetID); deleted {
		return updated, true
	}
	if updated, deleted := walkDeleteByObjectKey(node, targetID); deleted {
		return updated, true
	}
	return walkDeleteByAnyKey(node, targetID)
}

func walkDeleteByIDField(node any, targetID string) (any, bool) {
	switch v := node.(type) {
	case map[string]any:
		keys := make([]string, 0, len(v))
		for k := range v {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		for _, k := range keys {
			if childMap, ok := v[k].(map[string]any); ok && mapHasMatchingIDField(childMap, targetID) {
				delete(v, k)
				return v, true
			}
		}
		for _, k := range keys {
			updated, deleted := walkDeleteByIDField(v[k], targetID)
			if deleted {
				v[k] = updated
				return v, true
			}
		}
		return v, false
	case []any:
		filtered := make([]any, 0, len(v))
		deleted := false
		for _, elem := range v {
			if m, ok := elem.(map[string]any); ok && mapHasMatchingIDField(m, targetID) {
				deleted = true
				continue
			}
			updatedElem, elemDeleted := walkDeleteByIDField(elem, targetID)
			if elemDeleted {
				deleted = true
			}
			filtered = append(filtered, updatedElem)
		}
		return filtered, deleted
	default:
		return node, false
	}
}

func walkDeleteByObjectKey(node any, targetID string) (any, bool) {
	switch v := node.(type) {
	case map[string]any:
		if child, ok := v[targetID]; ok {
			if _, isObj := child.(map[string]any); isObj {
				delete(v, targetID)
				return v, true
			}
		}
		keys := make([]string, 0, len(v))
		for k := range v {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		for _, k := range keys {
			updated, deleted := walkDeleteByObjectKey(v[k], targetID)
			if deleted {
				v[k] = updated
				return v, true
			}
		}
		return v, false
	case []any:
		filtered := make([]any, 0, len(v))
		deleted := false
		for _, elem := range v {
			updatedElem, elemDeleted := walkDeleteByObjectKey(elem, targetID)
			if elemDeleted {
				deleted = true
			}
			filtered = append(filtered, updatedElem)
		}
		return filtered, deleted
	default:
		return node, false
	}
}

func walkDeleteByAnyKey(node any, targetID string) (any, bool) {
	switch v := node.(type) {
	case map[string]any:
		if _, ok := v[targetID]; ok {
			delete(v, targetID)
			return v, true
		}
		keys := make([]string, 0, len(v))
		for k := range v {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		for _, k := range keys {
			updated, deleted := walkDeleteByAnyKey(v[k], targetID)
			if deleted {
				v[k] = updated
				return v, true
			}
		}
		return v, false
	case []any:
		filtered := make([]any, 0, len(v))
		deleted := false
		for _, elem := range v {
			updatedElem, elemDeleted := walkDeleteByAnyKey(elem, targetID)
			if elemDeleted {
				deleted = true
			}
			filtered = append(filtered, updatedElem)
		}
		return filtered, deleted
	default:
		return node, false
	}
}

func resolveSchemaFile(t *testing.T, schemasDir, logicalName string) string {
	t.Helper()
	path, err := resolveSchemaFileErr(schemasDir, logicalName)
	if err != nil {
		t.Fatalf("resolveSchemaFile(%q): %v", logicalName, err)
	}
	return path
}

func resolveSchemaFileErr(schemasDir, logicalName string) (string, error) {
	base := strings.TrimSuffix(strings.TrimSuffix(logicalName, ".schema.json"), "_definition")
	candidates := []string{
		base + "_definition.schema.json",
		base + ".schema.json",
		base + "s.schema.json",
		base + "s_definition.schema.json",
	}
	switch base {
	case "monster_boss":
		candidates = append(candidates, "monsters.schema.json", "monster.schema.json", "monster_definition.schema.json")
	case "affix":
		candidates = append(candidates, "affix_pool.schema.json", "affix_pools.schema.json", "affix_pool_definition.schema.json")
	case "loot_table":
		candidates = append(candidates, "loot_tables.schema.json")
	case "enhance_curve":
		candidates = append(candidates, "enhance_curves.schema.json")
	case "item_set":
		candidates = append(candidates, "item_sets.schema.json")
	case "common_defs":
		candidates = append(candidates, "common.schema.json")
	case "global_constants":
		candidates = append(candidates, "global_combat_constants.schema.json")
	}

	for _, cand := range candidates {
		full := filepath.Join(schemasDir, cand)
		if info, err := os.Stat(full); err == nil && !info.IsDir() && info.Size() > 0 {
			return full, nil
		}
	}
	return "", fmt.Errorf("schema for logical family %q not found in %s (tried %v)", logicalName, schemasDir, candidates)
}

func computeCoreGameDataHash(t *testing.T, coreDir string) string {
	t.Helper()
	h, err := computeCoreGameDataHashErr(coreDir)
	if err != nil {
		t.Fatalf("computeCoreGameDataHash(%q): %v", coreDir, err)
	}
	return h
}

func computeCoreGameDataHashErr(coreDir string) (string, error) {
	var relPaths []string
	err := filepath.WalkDir(coreDir, func(path string, d fs.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if d.IsDir() || !strings.HasSuffix(strings.ToLower(d.Name()), ".json") {
			return nil
		}
		rel, err := filepath.Rel(coreDir, path)
		if err != nil {
			return err
		}
		relPaths = append(relPaths, filepath.ToSlash(rel))
		return nil
	})
	if err != nil {
		return "", err
	}
	if len(relPaths) == 0 {
		return "", fmt.Errorf("no .json files found under %s", coreDir)
	}
	sort.Strings(relPaths)
	hasher := sha256.New()
	for _, rel := range relPaths {
		raw, err := os.ReadFile(filepath.Join(coreDir, filepath.FromSlash(rel)))
		if err != nil {
			return "", err
		}
		normalized := strings.ReplaceAll(string(raw), "\r\n", "\n")
		_, _ = hasher.Write([]byte("FILE:" + rel + "\n"))
		_, _ = hasher.Write([]byte(normalized))
		_, _ = hasher.Write([]byte("\n---\n"))
	}
	return hex.EncodeToString(hasher.Sum(nil)), nil
}

func parseUE5CSV(t *testing.T, path string) ([]string, []map[string]string) {
	t.Helper()
	headers, rows, err := parseUE5CSVErr(path)
	if err != nil {
		t.Fatalf("parseUE5CSV(%q): %v", path, err)
	}
	return headers, rows
}

func parseUE5CSVErr(path string) ([]string, []map[string]string, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, nil, err
	}
	// Strip UTF-8 BOM if present
	raw = bytes.TrimPrefix(raw, []byte{0xEF, 0xBB, 0xBF})
	r := csv.NewReader(bytes.NewReader(raw))
	records, err := r.ReadAll()
	if err != nil {
		return nil, nil, err
	}
	if len(records) < 2 {
		return nil, nil, fmt.Errorf("CSV %s must have header + at least 1 row (got %d lines)", path, len(records))
	}
	headers := records[0]
	if len(headers) == 0 || (headers[0] != "Name" && headers[0] != "RowName") {
		return nil, nil, fmt.Errorf("CSV %s first column header must be Name or RowName (got %v)", path, headers)
	}
	rows := make([]map[string]string, 0, len(records)-1)
	for i := 1; i < len(records); i++ {
		row := make(map[string]string, len(headers))
		for j, h := range headers {
			if j < len(records[i]) {
				row[h] = records[i][j]
			}
		}
		rows = append(rows, row)
	}
	return headers, rows, nil
}

func parseUE5JSON(t *testing.T, path string) ([]map[string]any, []string) {
	t.Helper()
	rows, names, err := parseUE5JSONErr(path)
	if err != nil {
		t.Fatalf("parseUE5JSON(%q): %v", path, err)
	}
	return rows, names
}

func parseUE5JSONErr(path string) ([]map[string]any, []string, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, nil, err
	}
	// Strip UTF-8 BOM if present
	raw = bytes.TrimPrefix(raw, []byte{0xEF, 0xBB, 0xBF})
	var rows []map[string]any
	if err := json.Unmarshal(raw, &rows); err != nil {
		return nil, nil, fmt.Errorf("expected JSON array in %s: %w", path, err)
	}
	if len(rows) == 0 {
		return nil, nil, fmt.Errorf("UE5 JSON %s is empty", path)
	}
	names := make([]string, 0, len(rows))
	for i, row := range rows {
		nameVal, ok := row["Name"].(string)
		if !ok || strings.TrimSpace(nameVal) == "" {
			if rn, ok2 := row["RowName"].(string); ok2 && strings.TrimSpace(rn) != "" {
				nameVal = rn
			} else {
				return nil, nil, fmt.Errorf("row %d in %s missing non-empty Name/RowName field", i, path)
			}
		}
		names = append(names, nameVal)
	}
	return rows, names, nil
}

func findVS02Artifacts(repoRoot, backendDir string) ([]string, error) {
	var hits []string
	seenHits := make(map[string]bool)
	addHit := func(msg string) {
		if !seenHits[msg] {
			seenHits[msg] = true
			hits = append(hits, msg)
		}
	}

	forbiddenPaths := []string{
		filepath.Join(backendDir, "api", "proto"),
		filepath.Join(backendDir, "proto", "iu", "backend", "v1"),
		filepath.Join(backendDir, "buf.yaml"),
		filepath.Join(backendDir, "buf.gen.yaml"),
		filepath.Join(backendDir, "gen", "go"),
		filepath.Join(repoRoot, "api", "proto"),
		filepath.Join(repoRoot, "buf.yaml"),
		filepath.Join(repoRoot, "buf.gen.yaml"),
		filepath.Join(repoRoot, "gen", "go"),
	}
	for _, p := range forbiddenPaths {
		if _, err := os.Stat(p); err == nil {
			addHit("forbidden path exists: " + p)
		}
	}

	protoScriptGlob, _ := filepath.Glob(filepath.Join(backendDir, "scripts", "gen_proto.*"))
	for _, p := range protoScriptGlob {
		addHit("forbidden proto script exists: " + p)
	}

	skipRepoDirs := map[string]bool{
		".git":                      true,
		"vendor":                    true,
		"node_modules":              true,
		"01_GDD":                    true,
		"02_TECHNICAL_ARCHITECTURE": true,
		"03_UNREAL_GAME_CLIENT":     true,
		"04_MULTIPLAYER_BACKEND":    true,
		"05_GAME_CONTENT":           true,
		"06_DEVELOPMENT_ROADMAP":    true,
		"07_STEP1_CORE_PROTOTYPE":   true,
	}
	e2eTestsDir := filepath.Clean(filepath.Join(repoRoot, "tests", "e2e"))

	if _, err := os.Stat(repoRoot); err == nil {
		err = filepath.WalkDir(repoRoot, func(path string, d fs.DirEntry, walkErr error) error {
			if walkErr != nil {
				return walkErr
			}
			if d.IsDir() {
				if skipRepoDirs[d.Name()] || filepath.Clean(path) == e2eTestsDir {
					return filepath.SkipDir
				}
				return nil
			}
			lower := strings.ToLower(d.Name())
			if strings.HasSuffix(lower, ".proto") || lower == "buf.yaml" || lower == "buf.gen.yaml" {
				addHit("forbidden VS-02 file: " + path)
			}
			return nil
		})
		if err != nil {
			return hits, err
		}
	}

	if _, err := os.Stat(backendDir); err == nil {
		err = filepath.WalkDir(backendDir, func(path string, d fs.DirEntry, walkErr error) error {
			if walkErr != nil {
				return walkErr
			}
			if d.IsDir() {
				if d.Name() == ".git" || d.Name() == "vendor" || d.Name() == "node_modules" {
					return filepath.SkipDir
				}
				return nil
			}
			lower := strings.ToLower(d.Name())
			if strings.HasSuffix(lower, ".proto") || lower == "buf.yaml" || lower == "buf.gen.yaml" {
				addHit("forbidden VS-02 file: " + path)
				return nil
			}
			if strings.HasSuffix(lower, ".go") || lower == "go.mod" || strings.HasPrefix(lower, "dockerfile") ||
				strings.HasSuffix(lower, ".sh") || strings.HasSuffix(lower, ".ps1") ||
				strings.HasSuffix(lower, ".yml") || strings.HasSuffix(lower, ".yaml") {
				raw, readErr := os.ReadFile(path)
				if readErr != nil {
					return readErr
				}
				txt := string(raw)
				if strings.Contains(txt, "iu.backend.v1") || strings.Contains(txt, "gen/go/iu/backend") {
					addHit("forbidden VS-02 reference in " + path)
				}
			}
			return nil
		})
		if err != nil {
			return hits, err
		}
	}
	return hits, nil
}

func assertNoVS02Artifacts(t *testing.T, repoRoot, backendDir string) {
	t.Helper()
	hits, err := findVS02Artifacts(repoRoot, backendDir)
	if err != nil {
		t.Fatalf("scan for VS-02 artifacts failed: %v", err)
	}
	if len(hits) > 0 {
		t.Fatalf("VS-02 Protobuf/gRPC artifacts MUST NOT exist, found %d violation(s):\n%s",
			len(hits), strings.Join(hits, "\n"))
	}
}

func compileDataCompiler(t *testing.T, cfg e2eConfig) string {
	t.Helper()
	requireDirExists(t, filepath.Join(cfg.backendDir, "cmd", "datacompiler"), "backend/cmd/datacompiler")
	cachedDataCompilerOnce.Do(func() {
		binDir, err := getSharedBinDir()
		if err != nil {
			cachedDataCompilerErr = err
			return
		}
		outExe := filepath.Join(binDir, "datacompiler.exe")
		cmd := exec.Command("go", "build", "-o", outExe, "./cmd/datacompiler")
		cmd.Dir = cfg.backendDir
		var stderr bytes.Buffer
		cmd.Stderr = &stderr
		if err := cmd.Run(); err != nil {
			cachedDataCompilerErr = fmt.Errorf("go build ./cmd/datacompiler failed: %v\nstderr: %s", err, stderr.String())
			return
		}
		cachedDataCompilerPath = outExe
	})
	if cachedDataCompilerErr != nil {
		t.Fatalf("%v", cachedDataCompilerErr)
	}
	return cachedDataCompilerPath
}

func compileBackendBinary(t *testing.T, cfg e2eConfig, pkgPath, exeName string) string {
	t.Helper()
	requireDirExists(t, cfg.backendDir, "backend")
	outPath := filepath.Join(t.TempDir(), exeName+".exe")
	stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, nil, 120*time.Second, "go", "build", "-o", outPath, pkgPath)
	if err != nil || code != 0 {
		t.Fatalf("go build %s failed (code=%d err=%v)\nstdout:\n%s\nstderr:\n%s", pkgPath, code, err, stdout, stderr)
	}
	return outPath
}

func runDataCompiler(t *testing.T, binPath string, args ...string) (string, string, int) {
	t.Helper()
	stdout, stderr, code, _ := runCmd(t, 30*time.Second, binPath, args...)
	return stdout, stderr, code
}

func getFreeLocalAddr(t *testing.T) string {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("allocate ephemeral port: %v", err)
	}
	addr := ln.Addr().String()
	_ = ln.Close()
	return addr
}

func readJSONMap(t *testing.T, path string) map[string]any {
	t.Helper()
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read JSON %s: %v", path, err)
	}
	var m map[string]any
	if err := json.Unmarshal(raw, &m); err != nil {
		t.Fatalf("decode JSON object %s: %v", path, err)
	}
	return m
}

func readJSONAny(t *testing.T, path string) any {
	t.Helper()
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read JSON %s: %v", path, err)
	}
	var v any
	if err := json.Unmarshal(raw, &v); err != nil {
		t.Fatalf("decode JSON %s: %v", path, err)
	}
	return v
}

func extractAllCanonicalIDsFromCore(t *testing.T, coreDir string) []string {
	t.Helper()
	seen := make(map[string]struct{})
	idRegex := regexp.MustCompile(`^(planet|zone|subzone|race|trait|skill|form|mob|boss|item|token|set|affix)\.[a-z0-9_]+$`)

	err := filepath.WalkDir(coreDir, func(path string, d fs.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if d.IsDir() || !strings.HasSuffix(strings.ToLower(d.Name()), ".json") {
			return nil
		}
		raw, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		var data any
		if err := json.Unmarshal(raw, &data); err != nil {
			return fmt.Errorf("parse %s: %w", path, err)
		}
		collectCanonicalIDs(data, idRegex, seen)
		return nil
	})
	if err != nil {
		t.Fatalf("walk core dir %s: %v", coreDir, err)
	}
	ids := make([]string, 0, len(seen))
	for id := range seen {
		ids = append(ids, id)
	}
	sort.Strings(ids)
	return ids
}

func collectCanonicalIDs(node any, idRegex *regexp.Regexp, seen map[string]struct{}) {
	switch v := node.(type) {
	case map[string]any:
		for _, field := range []string{
			"race_id", "passive_id", "skill_id", "form_id",
			"entity_id", "item_id", "set_id", "affix_id",
			"zone_id", "planet_id", "subzone_id", "canonical_id",
		} {
			if s, ok := v[field].(string); ok && idRegex.MatchString(s) {
				seen[s] = struct{}{}
			}
		}
		for k, val := range v {
			if idRegex.MatchString(k) {
				seen[k] = struct{}{}
			}
			collectCanonicalIDs(val, idRegex, seen)
		}
	case []any:
		for _, elem := range v {
			collectCanonicalIDs(elem, idRegex, seen)
		}
	}
}
