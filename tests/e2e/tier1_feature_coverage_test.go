package e2e_test

import (
	"encoding/json"
	"fmt"
	"io/fs"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"testing"
	"time"
)

// ============================================================================
// Feature #1: Go Backend Scaffold & VS-02 Exclusion (6 subtests)
// ============================================================================

func TestTier1_F1_GoBackendScaffold(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T1_F1_01_GoModModuleAndVersion", func(t *testing.T) {
		goModPath := filepath.Join(cfg.backendDir, "go.mod")
		requireFileExists(t, goModPath, "backend/go.mod")
		raw, err := os.ReadFile(goModPath)
		if err != nil {
			t.Fatalf("read %s: %v", goModPath, err)
		}
		txt := string(raw)
		if !strings.Contains(txt, "module github.com/infinite-universe/backend") {
			t.Fatalf("backend/go.mod missing canonical module path github.com/infinite-universe/backend")
		}
		goVerRe := regexp.MustCompile(`(?m)^go 1\.(2[3-9]|[3-9][0-9])(\.\d+)?\r?$`)
		if !goVerRe.MatchString(txt) {
			t.Fatalf("backend/go.mod must declare Go 1.23+, got:\n%s", txt)
		}
		for _, dep := range []string{
			"github.com/jackc/pgx/v5",
			"github.com/redis/go-redis/v9",
			"github.com/nats-io/nats.go",
			"github.com/prometheus/client_golang",
		} {
			if !strings.Contains(txt, dep) {
				t.Fatalf("backend/go.mod missing required platform dependency %q", dep)
			}
		}
	})

	t.Run("T1_F1_02_ThreeCmdEntrypointsExistAndBuild", func(t *testing.T) {
		requireDirExists(t, cfg.backendDir, "backend")
		for _, svc := range []string{"gateway", "gameapi", "coordinator"} {
			mainPath := filepath.Join(cfg.backendDir, "cmd", svc, "main.go")
			requireFileExists(t, mainPath, "cmd/"+svc+"/main.go")
		}
		stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, nil, 120*time.Second, "go", "build", "./...")
		if err != nil || code != 0 {
			t.Fatalf("go build ./... failed (code=%d err=%v)\nstdout:\n%s\nstderr:\n%s", code, err, stdout, stderr)
		}
		for _, svc := range []string{"gateway", "gameapi", "coordinator"} {
			binPath := compileBackendBinary(t, cfg, "./cmd/"+svc, svc)
			requireFileExists(t, binPath, svc+" binary")
		}
	})

	t.Run("T1_F1_03_HealthcheckFlagSucceedsOn200OK", func(t *testing.T) {
		srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			if r.URL.Path != "/healthz" {
				http.NotFound(w, r)
				return
			}
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusOK)
			_, _ = w.Write([]byte(`{"status":"ok","gamedata_hash":""}`))
		}))
		defer srv.Close()

		for _, svc := range []string{"gateway", "gameapi", "coordinator"} {
			binPath := compileBackendBinary(t, cfg, "./cmd/"+svc, svc)
			stdout, stderr, code, err := runCmd(t, 5*time.Second, binPath, "-healthcheck="+srv.URL+"/healthz")
			if err != nil || code != 0 {
				t.Fatalf("%s -healthcheck failed on 200 OK (code=%d err=%v stdout=%q stderr=%q)",
					svc, code, err, stdout, stderr)
			}
		}
	})

	t.Run("T1_F1_04_PkgGameDataHashExistsAndDeterministic", func(t *testing.T) {
		hashPath := filepath.Join(cfg.backendDir, "pkg", "gamedata", "hash.go")
		requireFileExists(t, hashPath, "backend/pkg/gamedata/hash.go")

		stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, nil, 30*time.Second, "go", "doc", "./pkg/gamedata", "ComputeCanonicalGameDataHash")
		if err != nil || code != 0 {
			t.Fatalf("go doc ./pkg/gamedata ComputeCanonicalGameDataHash failed (code=%d err=%v)\nstdout:%s\nstderr:%s", code, err, stdout, stderr)
		}
		if !strings.Contains(stdout, "ComputeCanonicalGameDataHash") {
			t.Fatalf("unexpected go doc output: %s", stdout)
		}

		// Execute ComputeCanonicalGameDataHash via an isolated temp module without modifying backend/
		probeDir := t.TempDir()
		goModContent := fmt.Sprintf("module e2eprobe\n\ngo 1.23\n\nrequire github.com/infinite-universe/backend v0.0.0\n\nreplace github.com/infinite-universe/backend => %s\n",
			filepath.ToSlash(cfg.backendDir))
		probeMain := `package main

import (
	"fmt"
	"os"
	"github.com/infinite-universe/backend/pkg/gamedata"
)

func main() {
	h1, err := gamedata.ComputeCanonicalGameDataHash(os.Args[1])
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	h2, err := gamedata.ComputeCanonicalGameDataHash(os.Args[2])
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	fmt.Println(h1)
	fmt.Println(h2)
}
`
		if err := os.WriteFile(filepath.Join(probeDir, "go.mod"), []byte(goModContent), 0o644); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(probeDir, "main.go"), []byte(probeMain), 0o644); err != nil {
			t.Fatal(err)
		}

		dirLF := filepath.Join(probeDir, "lf")
		dirCRLF := filepath.Join(probeDir, "crlf")
		_ = os.MkdirAll(dirLF, 0o755)
		_ = os.MkdirAll(dirCRLF, 0o755)
		sampleA := "{\n  \"id\": \"skill.ki_volley_01\"\n}\n"
		sampleB := "{\n  \"id\": \"race.zenith_warrior\"\n}\n"
		_ = os.WriteFile(filepath.Join(dirLF, "a.json"), []byte(sampleA), 0o644)
		_ = os.WriteFile(filepath.Join(dirLF, "b.json"), []byte(sampleB), 0o644)
		_ = os.WriteFile(filepath.Join(dirCRLF, "a.json"), []byte(strings.ReplaceAll(sampleA, "\n", "\r\n")), 0o644)
		_ = os.WriteFile(filepath.Join(dirCRLF, "b.json"), []byte(strings.ReplaceAll(sampleB, "\n", "\r\n")), 0o644)

		pOut, pErr, pCode, err := runCmdInDir(t, probeDir, nil, 60*time.Second, "go", "run", "-mod=mod", ".", dirLF, dirCRLF)
		if err != nil || pCode != 0 {
			t.Fatalf("ComputeCanonicalGameDataHash probe failed (code=%d err=%v): stdout=%q stderr=%q", pCode, err, pOut, pErr)
		}
		lines := strings.Fields(strings.TrimSpace(pOut))
		if len(lines) != 2 || len(lines[0]) != 64 || lines[0] != lines[1] {
			t.Fatalf("expected two identical 64-char SHA-256 hashes, got %q", pOut)
		}
	})

	t.Run("T1_F1_05_PlatformAndModuleBoundariesExist", func(t *testing.T) {
		platformPkgs := []string{"config", "postgres", "redis", "rediskeys", "nats", "telemetry", "grpcserver"}
		for _, p := range platformPkgs {
			requireDirExists(t, filepath.Join(cfg.backendDir, "internal", "platform", p), "internal/platform/"+p)
		}
		modulePkgs := []string{
			"auth", "account", "character", "progression", "inventory",
			"loot", "economy", "quest", "party", "guild", "social", "world", "analytics",
		}
		for _, m := range modulePkgs {
			requireDirExists(t, filepath.Join(cfg.backendDir, "internal", "modules", m), "internal/modules/"+m)
		}
		stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, nil, 30*time.Second, "go", "list", "./internal/...")
		if err != nil || code != 0 {
			t.Fatalf("go list ./internal/... failed (code=%d err=%v)\nstdout:%s\nstderr:%s", code, err, stdout, stderr)
		}
	})

	t.Run("T1_F1_06_VS02ArtifactsAbsent", func(t *testing.T) {
		requireDirExists(t, cfg.backendDir, "backend")
		assertNoVS02Artifacts(t, cfg.repoRoot, cfg.backendDir)
	})
}

// ============================================================================
// Feature #2: Redis Keyspace (6 subtests)
// ============================================================================

func TestTier1_F2_RedisKeyspace(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T1_F2_01_RedisKeysUnitTestsPass", func(t *testing.T) {
		rkDir := filepath.Join(cfg.backendDir, "internal", "platform", "rediskeys")
		requireDirExists(t, rkDir, "internal/platform/rediskeys")
		stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, nil, 60*time.Second, "go", "test", "-v", "-count=1", "./internal/platform/rediskeys/...")
		if err != nil || code != 0 || !strings.Contains(stdout, "PASS") {
			t.Fatalf("rediskeys unit tests failed (code=%d err=%v)\nstdout:\n%s\nstderr:\n%s", code, err, stdout, stderr)
		}
	})

	t.Run("T1_F2_02_CanonicalPrefixesDocumentedAndExported", func(t *testing.T) {
		rkDir := filepath.Join(cfg.backendDir, "internal", "platform", "rediskeys")
		requireDirExists(t, rkDir, "internal/platform/rediskeys")
		stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, nil, 30*time.Second, "go", "doc", "./internal/platform/rediskeys")
		if err != nil || code != 0 {
			t.Fatalf("go doc ./internal/platform/rediskeys failed (code=%d err=%v): %s", code, err, stderr)
		}
		var combined strings.Builder
		combined.WriteString(stdout)
		entries, err := os.ReadDir(rkDir)
		if err != nil {
			t.Fatal(err)
		}
		for _, e := range entries {
			if !e.IsDir() && strings.HasSuffix(e.Name(), ".go") {
				b, rErr := os.ReadFile(filepath.Join(rkDir, e.Name()))
				if rErr == nil {
					combined.Write(b)
				}
			}
		}
		all := combined.String()
		for _, prefix := range []string{"session:ticket:", "char:lease:", "zone:registry:"} {
			if !strings.Contains(all, prefix) {
				t.Fatalf("rediskeys missing canonical prefix %q", prefix)
			}
		}
	})

	t.Run("T1_F2_03_LiveRedisPingAndReadiness", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		out := redisExecOrFatal(t, cfg, "PING")
		if strings.TrimSpace(out) != "PONG" {
			t.Fatalf("expected PONG from redis-cli PING, got %q", out)
		}
		respPing(t, cfg.redisAddr)
	})

	t.Run("T1_F2_04_LiveSessionTicketSetGetTTLDel", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		key := "session:ticket:" + uniqueRunID(t)
		t.Cleanup(func() { _, _, _, _ = redisExec(t, cfg, "DEL", key) })

		val := `{"account_id":"a1","character_id":"c1"}`
		if setOut := redisExecOrFatal(t, cfg, "SET", key, val, "EX", "30"); setOut != "OK" {
			t.Fatalf("SET failed: %q", setOut)
		}
		if got := redisExecOrFatal(t, cfg, "GET", key); got != val {
			t.Fatalf("GET %s = %q, want %q", key, got, val)
		}
		ttlStr := redisExecOrFatal(t, cfg, "TTL", key)
		ttl, err := strconv.Atoi(ttlStr)
		if err != nil || ttl < 1 || ttl > 30 {
			t.Fatalf("expected TTL in [1..30], got %q", ttlStr)
		}
		if delOut := redisExecOrFatal(t, cfg, "DEL", key); delOut != "1" {
			t.Fatalf("DEL %s = %q, want 1", key, delOut)
		}
		if existsOut := redisExecOrFatal(t, cfg, "EXISTS", key); existsOut != "0" {
			t.Fatalf("EXISTS %s after DEL = %q, want 0", key, existsOut)
		}
	})

	t.Run("T1_F2_05_LiveCharLeaseHashSetGetTTLDel", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		key := "char:lease:" + uniqueRunID(t)
		t.Cleanup(func() { _, _, _, _ = redisExec(t, cfg, "DEL", key) })

		redisExecOrFatal(t, cfg, "HSET", key,
			"owner_server_id", "srv-1",
			"zone_id", "zone.p01_highland_01",
			"channel_id", "1",
			"fencing_epoch", "1",
			"state", "ACTIVE",
		)
		redisExecOrFatal(t, cfg, "EXPIRE", key, "15")
		hget := redisExecOrFatal(t, cfg, "HGETALL", key)
		for _, want := range []string{"owner_server_id", "srv-1", "fencing_epoch", "ACTIVE"} {
			if !strings.Contains(hget, want) {
				t.Fatalf("HGETALL %s missing %q in %q", key, want, hget)
			}
		}
		ttl, err := strconv.Atoi(redisExecOrFatal(t, cfg, "TTL", key))
		if err != nil || ttl < 1 || ttl > 15 {
			t.Fatalf("expected TTL in [1..15], got %d (%v)", ttl, err)
		}
		if delOut := redisExecOrFatal(t, cfg, "DEL", key); delOut != "1" {
			t.Fatalf("DEL %s = %q, want 1", key, delOut)
		}
	})

	t.Run("T1_F2_06_LiveZoneRegistryHashSetGetTTLDel", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		key := "zone:registry:zone.p01_highland_01:" + uniqueRunID(t)
		t.Cleanup(func() { _, _, _, _ = redisExec(t, cfg, "DEL", key) })

		redisExecOrFatal(t, cfg, "HSET", key,
			"zone_id", "zone.p01_highland_01",
			"channel_id", "1",
			"connected_ccu", "0",
			"status", "READY",
		)
		redisExecOrFatal(t, cfg, "EXPIRE", key, "10")
		hget := redisExecOrFatal(t, cfg, "HGETALL", key)
		for _, want := range []string{"zone.p01_highland_01", "connected_ccu", "READY"} {
			if !strings.Contains(hget, want) {
				t.Fatalf("HGETALL %s missing %q in %q", key, want, hget)
			}
		}
		ttl, err := strconv.Atoi(redisExecOrFatal(t, cfg, "TTL", key))
		if err != nil || ttl < 1 || ttl > 10 {
			t.Fatalf("expected TTL in [1..10], got %d (%v)", ttl, err)
		}
		if delOut := redisExecOrFatal(t, cfg, "DEL", key); delOut != "1" {
			t.Fatalf("DEL %s = %q, want 1", key, delOut)
		}
	})
}

// ============================================================================
// Feature #3: 19-Table PostgreSQL 16 Schema (6 subtests)
// ============================================================================

var canonical19Tables = []string{
	"accounts",
	"boss_kill_history",
	"boss_pity_tokens",
	"character_attributes",
	"character_equipment",
	"character_potential",
	"character_quests",
	"character_save_snapshots",
	"character_skills",
	"character_transformations",
	"character_wallets",
	"characters",
	"currency_ledger",
	"guild_members",
	"guilds",
	"inventory_items",
	"item_definitions",
	"loot_history",
	"trade_transactions",
}

func TestTier1_F3_Postgres19TableSchema(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T1_F3_01_MigrationFilesExist", func(t *testing.T) {
		upPath := filepath.Join(cfg.backendDir, "migrations", "000001_init_core_schema.up.sql")
		downPath := filepath.Join(cfg.backendDir, "migrations", "000001_init_core_schema.down.sql")
		requireFileExists(t, upPath, "000001_init_core_schema.up.sql")
		requireFileExists(t, downPath, "000001_init_core_schema.down.sql")
		upInfo, _ := os.Stat(upPath)
		downInfo, _ := os.Stat(downPath)
		if upInfo.Size() == 0 || downInfo.Size() == 0 {
			t.Fatalf("migration files must be non-empty (up=%d down=%d)", upInfo.Size(), downInfo.Size())
		}
	})

	t.Run("T1_F3_02_All19DomainTablesExist", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		out := psqlExecOrFatal(t, cfg, cfg.pgDB, `
SELECT table_name
FROM information_schema.tables
WHERE table_schema = 'public'
  AND table_type = 'BASE TABLE'
  AND table_name <> 'schema_migrations'
ORDER BY table_name;
`)
		got := nonEmptyLines(out)
		if len(got) != 19 {
			t.Fatalf("expected exactly 19 tables, got %d: %v", len(got), got)
		}
		for i, want := range canonical19Tables {
			if got[i] != want {
				t.Fatalf("table list mismatch at index %d: got %q, want %q (full=%v)", i, got[i], want, got)
			}
		}
	})

	t.Run("T1_F3_03_ExtensionsPgcryptoAndCitextInstalled", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		out := psqlExecOrFatal(t, cfg, cfg.pgDB, `SELECT extname FROM pg_extension WHERE extname IN ('pgcrypto', 'citext') ORDER BY extname;`)
		got := nonEmptyLines(out)
		if len(got) != 2 || got[0] != "citext" || got[1] != "pgcrypto" {
			t.Fatalf("expected [citext pgcrypto], got %v", got)
		}
	})

	t.Run("T1_F3_04_CharactersFencingEpochDefaultZero", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		meta := psqlExecOrFatal(t, cfg, cfg.pgDB, `
SELECT data_type || '|' || is_nullable || '|' || COALESCE(column_default, '')
FROM information_schema.columns
WHERE table_schema = 'public' AND table_name = 'characters' AND column_name = 'fencing_epoch';
`)
		if !strings.HasPrefix(meta, "bigint|NO|") || !strings.Contains(meta, "0") {
			t.Fatalf("unexpected characters.fencing_epoch metadata: %q", meta)
		}
		tag := shortRunTag(t)
		res := psqlTxRollback(t, cfg, cfg.pgDB, fmt.Sprintf(`
WITH acc AS (
  INSERT INTO accounts (username, email, password_hash)
  VALUES ('u_%s', 'u_%s@test.local', 'hash')
  RETURNING id
)
INSERT INTO characters (account_id, character_name, race_id)
SELECT id, 'c_%s', 'race.zenith_warrior' FROM acc
RETURNING fencing_epoch;
`, tag, tag, tag))
		if strings.TrimSpace(res) != "0" {
			t.Fatalf("expected default fencing_epoch=0, got %q", res)
		}
	})

	t.Run("T1_F3_05_CharacterWalletsStandardCoinDefaultZero", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		meta := psqlExecOrFatal(t, cfg, cfg.pgDB, `
SELECT data_type || '|' || is_nullable || '|' || COALESCE(column_default, '')
FROM information_schema.columns
WHERE table_schema = 'public' AND table_name = 'character_wallets' AND column_name = 'standard_coin';
`)
		if !strings.HasPrefix(meta, "bigint|NO|") || !strings.Contains(meta, "0") {
			t.Fatalf("unexpected character_wallets.standard_coin metadata: %q", meta)
		}
		tag := shortRunTag(t)
		res := psqlTxRollback(t, cfg, cfg.pgDB, fmt.Sprintf(`
WITH acc AS (
  INSERT INTO accounts (username, email, password_hash)
  VALUES ('w_%s', 'w_%s@test.local', 'hash')
  RETURNING id
), ch AS (
  INSERT INTO characters (account_id, character_name, race_id)
  SELECT id, 'wc_%s', 'race.zenith_warrior' FROM acc
  RETURNING id
)
INSERT INTO character_wallets (character_id)
SELECT id FROM ch
RETURNING standard_coin || '|' || mastery_point;
`, tag, tag, tag))
		if strings.TrimSpace(res) != "0|0" {
			t.Fatalf("expected default standard_coin=0 and mastery_point=0, got %q", res)
		}
	})

	t.Run("T1_F3_06_TriggersAndFunctionsInstalled", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		funcs := nonEmptyLines(psqlExecOrFatal(t, cfg, cfg.pgDB, `
SELECT proname FROM pg_proc
WHERE proname IN ('fn_prevent_ledger_mutation', 'fn_touch_updated_at')
ORDER BY proname;
`))
		if len(funcs) != 2 || funcs[0] != "fn_prevent_ledger_mutation" || funcs[1] != "fn_touch_updated_at" {
			t.Fatalf("expected both trigger functions installed, got %v", funcs)
		}

		trigs := psqlExecOrFatal(t, cfg, cfg.pgDB, `
SELECT trigger_name || '|' || event_object_table || '|' || action_timing || '|' || string_agg(event_manipulation, ',' ORDER BY event_manipulation)
FROM information_schema.triggers
WHERE trigger_schema = 'public'
GROUP BY trigger_name, event_object_table, action_timing
ORDER BY trigger_name;
`)
		if !strings.Contains(trigs, "trg_currency_ledger_immutable|currency_ledger|BEFORE|DELETE,UPDATE") {
			t.Fatalf("missing trg_currency_ledger_immutable in triggers:\n%s", trigs)
		}
		for _, tbl := range []string{
			"accounts", "boss_pity_tokens", "character_attributes", "character_potential",
			"character_quests", "character_skills", "character_transformations",
			"character_wallets", "characters", "guilds", "inventory_items",
		} {
			if !strings.Contains(trigs, "|"+tbl+"|BEFORE|UPDATE") {
				t.Fatalf("missing BEFORE UPDATE touch trigger for table %q in:\n%s", tbl, trigs)
			}
		}
	})
}

// ============================================================================
// Feature #4: Platform DB Integration Tests (5 subtests)
// ============================================================================

func TestTier1_F4_PlatformDBIntegration(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T1_F4_01_GoTestInternalPlatformPassesLivePG16", func(t *testing.T) {
		requireDirExists(t, filepath.Join(cfg.backendDir, "internal", "platform"), "internal/platform")
		skipIfDockerDisabled(t, cfg)

		env := map[string]string{
			"IU_TEST_PG_DSN":     cfg.hostPGDSN(),
			"IU_TEST_REDIS_ADDR": cfg.redisAddr,
		}
		stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, env, 180*time.Second, "go", "test", "-v", "-count=1", "./internal/platform/...")
		if err != nil || code != 0 {
			t.Fatalf("go test ./internal/platform/... failed (code=%d err=%v)\nstdout:\n%s\nstderr:\n%s", code, err, stdout, stderr)
		}
		if !strings.Contains(stdout, "PASS") || strings.Contains(stdout, "FAIL") {
			t.Fatalf("unexpected platform test output:\n%s", stdout)
		}
		if strings.Contains(stdout, "--- SKIP:") {
			t.Fatalf("./internal/platform/... skipped tests despite live PG16/Redis:\n%s", stdout)
		}
		if !strings.Contains(stdout, "internal/platform/postgres") || strings.Contains(stdout, "internal/platform/postgres\t[no test files]") {
			t.Fatalf("expected internal/platform/postgres integration tests to execute:\n%s", stdout)
		}
	})

	t.Run("T1_F4_02_ValidWalletInsertAndCreditDebitUpdate", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		tag := shortRunTag(t)
		res := psqlTxRollback(t, cfg, cfg.pgDB, fmt.Sprintf(`
CREATE TEMP TABLE _t1f4_ctx (cid uuid) ON COMMIT DROP;
WITH acc AS (
  INSERT INTO accounts (username, email, password_hash)
  VALUES ('u_%s', 'u_%s@test.local', 'hash') RETURNING id
), ch AS (
  INSERT INTO characters (account_id, character_name, race_id)
  SELECT id, 'c_%s', 'race.zenith_warrior' FROM acc RETURNING id
)
INSERT INTO _t1f4_ctx SELECT id FROM ch;

INSERT INTO character_wallets (character_id, standard_coin)
SELECT cid, 100 FROM _t1f4_ctx;

UPDATE character_wallets SET standard_coin = standard_coin + 250
WHERE character_id = (SELECT cid FROM _t1f4_ctx)
RETURNING standard_coin;

UPDATE character_wallets SET standard_coin = standard_coin - 150
WHERE character_id = (SELECT cid FROM _t1f4_ctx)
RETURNING standard_coin;

UPDATE character_wallets SET standard_coin = standard_coin - 200
WHERE character_id = (SELECT cid FROM _t1f4_ctx)
RETURNING standard_coin;
`, tag, tag, tag))
		lines := nonEmptyLines(res)
		if len(lines) < 3 || lines[len(lines)-3] != "350" || lines[len(lines)-2] != "200" || lines[len(lines)-1] != "0" {
			t.Fatalf("expected wallet progression [350, 200, 0], got %v", lines)
		}
	})

	t.Run("T1_F4_03_TxManagerCommitPersistsAllTables", func(t *testing.T) {
		requireDirExists(t, filepath.Join(cfg.backendDir, "internal", "platform", "postgres"), "internal/platform/postgres")
		stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, nil, 30*time.Second, "go", "doc", "./internal/platform/postgres", "TxManager")
		if err != nil || code != 0 {
			t.Fatalf("go doc TxManager failed (code=%d err=%v): %s", code, err, stderr)
		}
		for _, sym := range []string{"TxManager", "NewTxManager", "WithinTx", "Conn"} {
			if !strings.Contains(stdout, sym) {
				t.Fatalf("internal/platform/postgres TxManager missing symbol %q in:\n%s", sym, stdout)
			}
		}

		skipIfDockerDisabled(t, cfg)
		dbName := withTempPGDatabase(t, cfg, true)
		tag := shortRunTag(t)
		psqlExecOrFatal(t, cfg, dbName, fmt.Sprintf(`
BEGIN;
WITH acc AS (
  INSERT INTO accounts (username, email, password_hash)
  VALUES ('tx_%s', 'tx_%s@test.local', 'hash') RETURNING id
), ch AS (
  INSERT INTO characters (account_id, character_name, race_id)
  SELECT id, 'txc_%s', 'race.zenith_warrior' FROM acc RETURNING id
)
INSERT INTO character_wallets (character_id, standard_coin)
SELECT id, 777 FROM ch;
COMMIT;
`, tag, tag, tag))

		countOut := psqlExecOrFatal(t, cfg, dbName, fmt.Sprintf(`
SELECT count(*) FROM character_wallets w
JOIN characters c ON c.id = w.character_id
JOIN accounts a ON a.id = c.account_id
WHERE a.username = 'tx_%s' AND w.standard_coin = 777;
`, tag))
		if strings.TrimSpace(countOut) != "1" {
			t.Fatalf("expected committed multi-table row visible in new session, got count=%q", countOut)
		}
	})

	t.Run("T1_F4_04_ValidCurrencyLedgerAppendInsert", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		tag := shortRunTag(t)
		res := psqlTxRollback(t, cfg, cfg.pgDB, fmt.Sprintf(`
WITH acc AS (
  INSERT INTO accounts (username, email, password_hash)
  VALUES ('lg_%s', 'lg_%s@test.local', 'hash') RETURNING id
), ch AS (
  INSERT INTO characters (account_id, character_name, race_id)
  SELECT id, 'lgc_%s', 'race.zenith_warrior' FROM acc RETURNING id
)
INSERT INTO currency_ledger (
  idempotency_key, character_id, currency_type, delta_amount, balance_after,
  reason_code, reference_type, reference_id, fencing_epoch, server_id
)
SELECT 'idem:%s', id, 'currency.standard_coin', 500, 500, 'MOB_DROP', 'BOSS_KILL', 'kill-01', 1, 'gameapi-local'
FROM ch
RETURNING ledger_id || '|' || balance_after;
`, tag, tag, tag, tag))
		parts := strings.Split(strings.TrimSpace(res), "|")
		if len(parts) != 2 || parts[1] != "500" {
			t.Fatalf("unexpected ledger insert result %q", res)
		}
		if idNum, err := strconv.ParseInt(parts[0], 10, 64); err != nil || idNum <= 0 {
			t.Fatalf("expected positive ledger_id, got %q", parts[0])
		}
	})

	t.Run("T1_F4_05_PostgresPoolAndReadinessQuery", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		if err := dialTCPCheck(t, "127.0.0.1:"+cfg.pgHostPort, 5*time.Second); err != nil {
			t.Fatalf("TCP dial to PostgreSQL host port 127.0.0.1:%s failed: %v", cfg.pgHostPort, err)
		}
		out := psqlExecOrFatal(t, cfg, cfg.pgDB, `SELECT current_database() || '|' || current_user || '|' || version();`)
		if !strings.HasPrefix(out, cfg.pgDB+"|"+cfg.pgUser+"|") || !strings.Contains(out, "PostgreSQL 16") {
			t.Fatalf("unexpected PostgreSQL readiness info: %q", out)
		}
	})
}

// ============================================================================
// Feature #5: JSON Schemas (6 subtests)
// ============================================================================

var all15LogicalSchemas = []string{
	"common_defs", "race", "skill", "transformation", "item", "affix",
	"monster_boss", "loot_table", "presentation_pack", "enhance_curve",
	"zone", "item_set", "exp_table", "pity_shop", "global_constants",
}

func TestTier1_F5_JSONSchemas(t *testing.T) {
	cfg := loadConfig(t)
	schemasDir := filepath.Join(cfg.gamedataDir, "schemas")

	t.Run("T1_F5_01_All15SchemasPresent", func(t *testing.T) {
		requireDirExists(t, schemasDir, "gamedata/schemas")
		for _, logical := range all15LogicalSchemas {
			p := resolveSchemaFile(t, schemasDir, logical)
			requireFileExists(t, p, logical)
		}
	})

	t.Run("T1_F5_02_Draft2020_12Metadata", func(t *testing.T) {
		requireDirExists(t, schemasDir, "gamedata/schemas")
		entries, err := os.ReadDir(schemasDir)
		if err != nil {
			t.Fatal(err)
		}
		count := 0
		for _, e := range entries {
			if e.IsDir() || !strings.HasSuffix(e.Name(), ".schema.json") {
				continue
			}
			count++
			m := readJSONMap(t, filepath.Join(schemasDir, e.Name()))
			if s, _ := m["$schema"].(string); s != "https://json-schema.org/draft/2020-12/schema" {
				t.Fatalf("%s: expected Draft 2020-12 $schema, got %q", e.Name(), s)
			}
			if id, _ := m["$id"].(string); !strings.HasPrefix(id, "https://") {
				t.Fatalf("%s: expected $id starting with https://, got %q", e.Name(), id)
			}
			title, _ := m["title"].(string)
			_, hasDefs := m["$defs"]
			if strings.TrimSpace(title) == "" && !hasDefs {
				t.Fatalf("%s: expected non-empty title or $defs", e.Name())
			}
		}
		if count == 0 {
			t.Fatalf("no *.schema.json files found in %s", schemasDir)
		}
	})

	t.Run("T1_F5_03_StrictAdditionalPropertiesFalse", func(t *testing.T) {
		requireDirExists(t, schemasDir, "gamedata/schemas")
		entries, err := os.ReadDir(schemasDir)
		if err != nil {
			t.Fatal(err)
		}
		for _, e := range entries {
			if e.IsDir() || !strings.HasSuffix(e.Name(), ".schema.json") {
				continue
			}
			m := readJSONMap(t, filepath.Join(schemasDir, e.Name()))
			assertObjectSchemasHaveAdditionalPropertiesFalse(t, e.Name(), "$", m)
		}
	})

	t.Run("T1_F5_04_CanonicalIDAndTagRegexes", func(t *testing.T) {
		requireDirExists(t, schemasDir, "gamedata/schemas")
		allPatterns := collectSchemaPatterns(t, schemasDir)
		expectedPatterns := map[string]string{
			"race_id":       `^race\.[a-z0-9_]+$`,
			"gameplay_tag":  `^Character\.Race\.[A-Za-z0-9]+$`,
			"skill_id":      `^skill\.[a-z0-9_]+$`,
			"ability_tag":   `^Ability\.[A-Za-z0-9.]+$`,
			"form_id":       `^form\.[a-z0-9_]+$`,
			"state_tag":     `^State\.Form\.[A-Za-z0-9]+$`,
			"item_id":       `^(item|token)\.[a-z0-9_]+$`,
			"affix_id":      `^affix\.[a-z0-9_]+$`,
			"entity_id":     `^(mob|boss)\.[a-z0-9_]+$`,
			"loot_table_id": `^loot\.[a-z0-9_]+$`,
		}
		for prop, wantPat := range expectedPatterns {
			found := false
			for _, got := range allPatterns[prop] {
				if got == wantPat {
					found = true
					break
				}
			}
			if !found {
				t.Fatalf("schema property %q missing expected pattern %q (found=%v)", prop, wantPat, allPatterns[prop])
			}
		}
	})

	t.Run("T1_F5_05_EnumsAndNumericBounds", func(t *testing.T) {
		requireDirExists(t, schemasDir, "gamedata/schemas")
		allProps := collectSchemaPropertyNodes(t, schemasDir)

		assertEnumContains := func(prop string, want []string) {
			nodes := allProps[prop]
			if len(nodes) == 0 {
				t.Fatalf("property %q not found in any schema", prop)
			}
			for _, n := range nodes {
				if rawEnum, ok := n["enum"].([]any); ok {
					gotSet := make(map[string]bool)
					for _, e := range rawEnum {
						if s, ok := e.(string); ok {
							gotSet[s] = true
						}
					}
					allMatch := len(gotSet) == len(want)
					if allMatch {
						for _, w := range want {
							if !gotSet[w] {
								allMatch = false
								break
							}
						}
					}
					if allMatch {
						return
					}
				}
			}
			t.Fatalf("property %q enum mismatch (want exact set %v)", prop, want)
		}

		assertEnumContains("scope_status", []string{"VERTICAL_SLICE_FULL", "MVP_STUB_IN_VS", "POST_MVP"})
		assertEnumContains("item_type", []string{"EQUIPMENT", "CONSUMABLE", "ENHANCE_MATERIAL", "BOSS_TOKEN", "QUEST_ITEM"})
		assertEnumContains("rarity", []string{"rarity.common", "rarity.uncommon", "rarity.rare", "rarity.epic", "rarity.legendary", "rarity.mythic"})
		assertEnumContains("slot", []string{"slot.head", "slot.body", "slot.hands", "slot.legs", "slot.feet", "slot.acc_1", "slot.acc_2", "slot.special"})
		assertEnumContains("entry_type", []string{"ITEM", "SUB_TABLE", "NOTHING"})

		assertBounds := func(prop string, minWant, maxWant float64) {
			for _, n := range allProps[prop] {
				mn, ok1 := asFloat64(n["minimum"])
				mx, ok2 := asFloat64(n["maximum"])
				if ok1 && ok2 && mn == minWant && mx == maxWant {
					return
				}
			}
			t.Fatalf("property %q missing numeric bounds [%v, %v]", prop, minWant, maxWant)
		}
		assertBounds("crit_rate", 0, 1)
		assertBounds("unlock_level", 1, 100)
		assertBounds("weight", 1, 100000)
	})

	t.Run("T1_F5_06_PresentationAndProgressionSchemas", func(t *testing.T) {
		requireDirExists(t, schemasDir, "gamedata/schemas")
		for _, logical := range []string{
			"presentation_pack", "enhance_curve", "zone", "item_set",
			"exp_table", "pity_shop", "global_constants",
		} {
			p := resolveSchemaFile(t, schemasDir, logical)
			m := readJSONMap(t, p)
			if len(m) == 0 {
				t.Fatalf("schema %s is empty", p)
			}
		}
		presPath := resolveSchemaFile(t, schemasDir, "presentation_pack")
		rawPres, _ := os.ReadFile(presPath)
		txt := string(rawPres)
		for _, kw := range []string{"pack_id", "display_name", "asset"} {
			if !strings.Contains(strings.ToLower(txt), kw) {
				t.Fatalf("presentation_pack schema missing expected field/section %q", kw)
			}
		}
	})
}

// ============================================================================
// Feature #6: IP-Neutral Vertical Slice Gamedata (6 subtests)
// ============================================================================

func TestTier1_F6_IPNeutralCoreGamedata(t *testing.T) {
	cfg := loadConfig(t)
	coreDir := filepath.Join(cfg.gamedataDir, "sources", "core")

	t.Run("T1_F6_01_ZonesAndRaces", func(t *testing.T) {
		requireDirExists(t, coreDir, "gamedata/sources/core")
		zonesRaw, _ := os.ReadFile(filepath.Join(coreDir, "zones.json"))
		for _, id := range []string{"planet.p01_terra", "zone.p01_highland_01"} {
			if !strings.Contains(string(zonesRaw), id) {
				t.Fatalf("zones.json missing canonical ID %q", id)
			}
		}
		racesData := readJSONAny(t, filepath.Join(coreDir, "races.json"))
		zw := findRecordByID(t, racesData, "race.zenith_warrior")
		if zw["scope_status"] != "VERTICAL_SLICE_FULL" {
			t.Fatalf("race.zenith_warrior scope_status = %v, want VERTICAL_SLICE_FULL", zw["scope_status"])
		}
		startersBytes, _ := json.Marshal(zw["starter_skill_ids"])
		for _, sid := range []string{"skill.melee_basic_combo", "skill.def_vanish_step", "skill.ki_volley_01"} {
			if !strings.Contains(string(startersBytes), sid) {
				t.Fatalf("race.zenith_warrior missing starter skill %q", sid)
			}
		}
		for _, stub := range []string{"race.terran_adept", "race.mystic_elder"} {
			rec := findRecordByID(t, racesData, stub)
			if rec["scope_status"] != "MVP_STUB_IN_VS" {
				t.Fatalf("%s scope_status = %v, want MVP_STUB_IN_VS", stub, rec["scope_status"])
			}
		}
	})

	t.Run("T1_F6_02_SkillsAndTransformation", func(t *testing.T) {
		requireDirExists(t, coreDir, "gamedata/sources/core")
		skillsData := readJSONAny(t, filepath.Join(coreDir, "skills.json"))
		expectedSkills := map[string]string{
			"skill.melee_basic_combo": "Ability.Melee.BasicCombo",
			"skill.def_vanish_step":   "Ability.Defense.VanishStep",
			"skill.ki_volley_01":      "Ability.Ki.Volley01",
			"skill.beam_charged_01":   "Ability.Ki.BeamCharged01",
			"skill.rush_strike_01":    "Ability.Melee.RushStrike01",
			"skill.aoe_burst_01":      "Ability.Ki.AoeBurst01",
			"skill.guard_shield_01":   "Ability.Defense.EnergyShield01",
			"skill.ult_supernova_01":  "Ability.Ultimate.Supernova01",
		}
		for sid, tag := range expectedSkills {
			rec := findRecordByID(t, skillsData, sid)
			if rec["ability_tag"] != tag {
				t.Fatalf("skill %s ability_tag = %v, want %q", sid, rec["ability_tag"], tag)
			}
		}

		transData := readJSONAny(t, filepath.Join(coreDir, "transformations.json"))
		form := findRecordByID(t, transData, "form.zenith_awaken_01")
		if form["state_tag"] != "State.Form.ZenithAwaken01" {
			t.Fatalf("form state_tag = %v, want State.Form.ZenithAwaken01", form["state_tag"])
		}
		if form["required_race_id"] != "race.zenith_warrior" {
			t.Fatalf("form required_race_id = %v, want race.zenith_warrior", form["required_race_id"])
		}
		if ul, ok := asFloat64(form["unlock_level"]); !ok || ul != 12 {
			t.Fatalf("form unlock_level = %v, want 12", form["unlock_level"])
		}
		if mult, ok := asFloat64(form["form_damage_multiplier"]); !ok || mult != 1.25 {
			t.Fatalf("form_damage_multiplier = %v, want 1.25", form["form_damage_multiplier"])
		}
	})

	t.Run("T1_F6_03_MobsAndRegionalBoss", func(t *testing.T) {
		requireDirExists(t, coreDir, "gamedata/sources/core")
		monstersData := readJSONAny(t, filepath.Join(coreDir, "monsters.json"))
		for _, mid := range []string{"mob.highland_wolf_01", "mob.raider_scouter_01", "mob.crimson_enforcer_elite"} {
			_ = findRecordByID(t, monstersData, mid)
		}
		boss := findRecordByID(t, monstersData, "boss.highland_titan_01")
		if boss["rank"] != "REGIONAL_BOSS" {
			t.Fatalf("boss rank = %v, want REGIONAL_BOSS", boss["rank"])
		}
		if lv, ok := asFloat64(boss["level"]); !ok || lv != 18 {
			t.Fatalf("boss level = %v, want 18", boss["level"])
		}
		bossBytes, _ := json.Marshal(boss)
		for _, want := range []string{"loot.boss_highland_titan", "token.boss_highland_titan"} {
			if !strings.Contains(string(bossBytes), want) {
				t.Fatalf("boss.highland_titan_01 missing reference %q", want)
			}
		}
		phases, _ := boss["phases"].([]any)
		if len(phases) != 2 {
			t.Fatalf("expected 2 boss phases, got %d", len(phases))
		}
	})

	t.Run("T1_F6_04_ItemsAndItemSet", func(t *testing.T) {
		requireDirExists(t, coreDir, "gamedata/sources/core")
		itemsData := readJSONAny(t, filepath.Join(coreDir, "items.json"))
		gearIDs := []string{
			"item.body_recruit_tunic_01", "item.hands_wolfhide_wraps_05", "item.legs_wolfhide_trousers_06",
			"item.feet_raider_thruster_09", "item.acc2_scouter_charm_10", "item.legs_crimson_plates_14",
			"item.head_vanguard_visor_15", "item.hands_vanguard_gauntlets_15", "item.body_vanguard_cuirass_15",
			"item.feet_vanguard_greaves_15", "item.acc1_titan_crest_ring_18", "item.special_titan_core_18",
		}
		slotsSeen := make(map[string]bool)
		for _, gid := range gearIDs {
			rec := findRecordByID(t, itemsData, gid)
			if rec["item_type"] != "EQUIPMENT" {
				t.Fatalf("%s item_type = %v, want EQUIPMENT", gid, rec["item_type"])
			}
			es, ok := rec["equip_spec"].(map[string]any)
			if !ok || es == nil {
				t.Fatalf("%s expected non-null equip_spec", gid)
			}
			if slot, ok := es["slot"].(string); ok {
				slotsSeen[slot] = true
			}
		}
		for _, s := range []string{"slot.head", "slot.body", "slot.hands", "slot.legs", "slot.feet", "slot.acc_1", "slot.acc_2", "slot.special"} {
			if !slotsSeen[s] {
				t.Fatalf("VS gear set missing slot coverage for %q", s)
			}
		}
		for _, nonEq := range []string{"item.mat_crimson_ore_t1", "item.consumable_verdant_elixir_s", "token.boss_highland_titan"} {
			rec := findRecordByID(t, itemsData, nonEq)
			if rec["equip_spec"] != nil {
				t.Fatalf("%s expected equip_spec == null", nonEq)
			}
		}
		setsData := readJSONAny(t, filepath.Join(coreDir, "item_sets.json"))
		_ = findRecordByID(t, setsData, "set.highland_vanguard")
	})

	t.Run("T1_F6_05_AffixesAndLootTables", func(t *testing.T) {
		requireDirExists(t, coreDir, "gamedata/sources/core")
		affixData := readJSONAny(t, filepath.Join(coreDir, "affix_pools.json"))
		for _, aid := range []string{
			"affix.flat_max_hp", "affix.flat_phys_atk", "affix.flat_ki_atk",
			"affix.phys_dmg_pct", "affix.ki_dmg_pct", "affix.crit_rate",
			"affix.crit_dmg", "affix.energy_regen", "affix.cdr", "affix.form_efficiency",
		} {
			rec := findRecordByID(t, affixData, aid)
			rw, ok := asFloat64(rec["roll_weight"])
			if !ok || rw <= 0 {
				t.Fatalf("%s roll_weight must be > 0, got %v", aid, rec["roll_weight"])
			}
		}

		lootData := readJSONAny(t, filepath.Join(coreDir, "loot_tables.json"))
		for _, lid := range []string{
			"loot.mob_highland_wolf", "loot.mob_raider_scouter",
			"loot.elite_crimson_enforcer", "loot.boss_highland_titan",
		} {
			lt := findRecordByID(t, lootData, lid)
			groups, _ := lt["roll_groups"].([]any)
			if len(groups) == 0 {
				t.Fatalf("%s has no roll_groups", lid)
			}
			for _, gRaw := range groups {
				g, _ := gRaw.(map[string]any)
				entries, _ := g["entries"].([]any)
				sum := 0.0
				for _, eRaw := range entries {
					e, _ := eRaw.(map[string]any)
					w, _ := asFloat64(e["weight"])
					sum += w
				}
				if sum <= 0 {
					t.Fatalf("%s roll_group %v has non-positive weight sum %v", lid, g["group_id"], sum)
				}
			}
		}
	})

	t.Run("T1_F6_06_EnhanceExpPityAndConstants", func(t *testing.T) {
		requireDirExists(t, coreDir, "gamedata/sources/core")
		enhBytes, _ := os.ReadFile(filepath.Join(coreDir, "enhance_curves.json"))
		if !strings.Contains(string(enhBytes), "item.mat_crimson_ore_t1") {
			t.Fatalf("enhance_curves.json missing item.mat_crimson_ore_t1")
		}
		pityBytes, _ := os.ReadFile(filepath.Join(coreDir, "pity_shop.json"))
		if !strings.Contains(string(pityBytes), "token.boss_highland_titan") {
			t.Fatalf("pity_shop.json missing token.boss_highland_titan")
		}
		gcData := readJSONAny(t, filepath.Join(coreDir, "global_constants.json"))
		hoverSpeed, okHover := findJSONNumberField(gcData, "hover_flight_speed_cms")
		if !okHover || hoverSpeed != 900 {
			t.Fatalf("global_constants.json hover_flight_speed_cms=%v (found=%v), want 900", hoverSpeed, okHover)
		}
		boostSpeed, okBoost := findJSONNumberField(gcData, "boost_flight_speed_cms")
		if !okBoost || boostSpeed != 1800 {
			t.Fatalf("global_constants.json boost_flight_speed_cms=%v (found=%v), want 1800", boostSpeed, okBoost)
		}
		expData := readJSONAny(t, filepath.Join(coreDir, "exp_table.json"))
		rows := collectExpRows(t, expData)
		if len(rows) != 20 {
			t.Fatalf("expected 20 EXP levels (1..20), got %d", len(rows))
		}
		sumExp := 0.0
		sumMastery := 0.0
		for lv := 1; lv <= 20; lv++ {
			r, ok := rows[lv]
			if !ok {
				t.Fatalf("exp_table.json missing level %d", lv)
			}
			expNext, _ := asFloat64(r["exp_to_next"])
			mast, _ := asFloat64(r["mastery_reward"])
			sumExp += expNext
			sumMastery += mast
			if lv < 20 && expNext <= 0 {
				t.Fatalf("level %d exp_to_next must be > 0, got %v", lv, expNext)
			}
			if lv == 20 && expNext != 0 {
				t.Fatalf("level 20 exp_to_next must be 0, got %v", expNext)
			}
		}
		if sumExp != 64000 {
			t.Fatalf("expected total EXP Lv 1..20 == 64000, got %v", sumExp)
		}
		if sumMastery != 27 {
			t.Fatalf("expected total mastery points == 27, got %v", sumMastery)
		}
	})
}

// ============================================================================
// Feature #7: Dual IP Presentation Packs (5 subtests)
// ============================================================================

var forbiddenIPKeywords = []string{
	"saiyan", "super saiyan", "ssj", "goku", "vegeta", "piccolo", "gohan",
	"krillin", "frieza", "freezer", "majin", "namekian", "earthling",
	"kamehameha", "kaioken", "zeni", "senzu", "oozaru", "dragon ball",
	"dragonball", "dbz", "ngoc rong", "ngọc rồng", "nro", "genki",
	"kienzan", "kikoho", "makankosappo", "saibaman", "scouter",
}

var whitelistedReadmeTokens = map[string]bool{
	"attr.ki_atk":             true,
	"attr.ki_dmg_pct":         true,
	"skill.ki_volley_01":      true,
	"ability.ki.volley01":     true,
	"ability.ki.beamcharged01": true,
	"ability.ki.aoeburst01":   true,
	"ki_projectile":           true,
	"ki_beam_charged":         true,
	"ki_aoe_burst":            true,
	"mob.raider_scouter_01":   true,
	"item.acc2_scouter_charm_10": true,
	"ki_atk":                  true,
	"ki_dmg_pct":              true,
	"alpha_ki":                true,
}

func TestTier1_F7_DualIPPresentationPacks(t *testing.T) {
	cfg := loadConfig(t)
	packsDir := filepath.Join(cfg.gamedataDir, "sources", "packs")
	coreDir := filepath.Join(cfg.gamedataDir, "sources", "core")

	t.Run("T1_F7_01_PackDirectoriesAndFilesExist", func(t *testing.T) {
		for _, pack := range []string{"Pack_DB_Reference", "Pack_Original_Commercial"} {
			pDir := filepath.Join(packsDir, pack)
			requireDirExists(t, pDir, pack)
			for _, f := range []string{"pack_manifest.json", "strings_vi.json", "strings_en.json", "asset_bindings.json"} {
				fp := filepath.Join(pDir, f)
				requireFileExists(t, fp, pack+"/"+f)
				_ = readJSONAny(t, fp)
			}
		}
	})

	t.Run("T1_F7_02_AllCoreCanonicalIDsMappedInBothPacks", func(t *testing.T) {
		requireDirExists(t, coreDir, "gamedata/sources/core")
		coreIDs := extractAllCanonicalIDsFromCore(t, coreDir)
		if len(coreIDs) == 0 {
			t.Fatalf("extracted 0 Canonical IDs from %s", coreDir)
		}
		for _, pack := range []string{"Pack_DB_Reference", "Pack_Original_Commercial"} {
			pDir := filepath.Join(packsDir, pack)
			vi := readJSONAny(t, filepath.Join(pDir, "strings_vi.json"))
			en := readJSONAny(t, filepath.Join(pDir, "strings_en.json"))
			ab := readJSONAny(t, filepath.Join(pDir, "asset_bindings.json"))
			for _, cid := range coreIDs {
				if !hasCanonicalMapping(vi, cid) {
					t.Fatalf("%s/strings_vi.json missing mapping for %q", pack, cid)
				}
				if !hasCanonicalMapping(en, cid) {
					t.Fatalf("%s/strings_en.json missing mapping for %q", pack, cid)
				}
				if !hasCanonicalMapping(ab, cid) {
					t.Fatalf("%s/asset_bindings.json missing mapping for %q", pack, cid)
				}
			}
		}
	})

	t.Run("T1_F7_03_CoreContainsZeroDisplayLoreOrAssetPaths", func(t *testing.T) {
		requireDirExists(t, coreDir, "gamedata/sources/core")
		forbiddenKeys := map[string]bool{
			"display_name": true, "short_description": true, "lore_flavor_text": true,
			"icon_texture": true, "primary_skeletal_mesh": true, "primary_montage": true,
			"primary_vfx": true, "cast_sfx": true, "impact_sfx": true,
		}
		err := filepath.WalkDir(coreDir, func(path string, d fs.DirEntry, walkErr error) error {
			if walkErr != nil || d.IsDir() || !strings.HasSuffix(d.Name(), ".json") {
				return walkErr
			}
			data := readJSONAny(t, path)
			scanCoreNode(t, path, data, forbiddenKeys)
			return nil
		})
		if err != nil {
			t.Fatal(err)
		}
	})

	t.Run("T1_F7_04_OriginalCommercialPassesIPScan", func(t *testing.T) {
		pDir := filepath.Join(packsDir, "Pack_Original_Commercial")
		requireDirExists(t, pDir, "Pack_Original_Commercial")
		for _, f := range []string{"pack_manifest.json", "strings_vi.json", "strings_en.json", "asset_bindings.json"} {
			fp := filepath.Join(pDir, f)
			data := readJSONAny(t, fp)
			scanPackOriginalNode(t, fp, data)
		}
	})

	t.Run("T1_F7_05_DistinctPackMetadataAndAssetPaths", func(t *testing.T) {
		dbMan := readJSONMap(t, filepath.Join(packsDir, "Pack_DB_Reference", "pack_manifest.json"))
		origMan := readJSONMap(t, filepath.Join(packsDir, "Pack_Original_Commercial", "pack_manifest.json"))
		if dbMan["pack_id"] == "" || origMan["pack_id"] == "" || dbMan["pack_id"] == origMan["pack_id"] {
			t.Fatalf("expected distinct non-empty pack_id, got db=%v orig=%v", dbMan["pack_id"], origMan["pack_id"])
		}
		dbAssets, _ := os.ReadFile(filepath.Join(packsDir, "Pack_DB_Reference", "asset_bindings.json"))
		origAssets, _ := os.ReadFile(filepath.Join(packsDir, "Pack_Original_Commercial", "asset_bindings.json"))
		if !strings.Contains(string(dbAssets), "DB_Reference") && !strings.Contains(string(dbAssets), "Ref") {
			t.Fatalf("Pack_DB_Reference asset_bindings.json missing DB_Reference/Ref asset paths")
		}
		if strings.Contains(string(origAssets), "DB_Reference") {
			t.Fatalf("Pack_Original_Commercial asset_bindings.json must NOT reference DB_Reference")
		}
		if !strings.Contains(string(origAssets), "Original_Commercial") && !strings.Contains(string(origAssets), "Orig") {
			t.Fatalf("Pack_Original_Commercial asset_bindings.json missing Original_Commercial/Orig asset paths")
		}
	})
}

// ============================================================================
// Feature #8: cmd/datacompiler & Build Outputs (6 subtests)
// ============================================================================

func TestTier1_F8_DatacompilerAndBuildOutputs(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T1_F8_01_CLIRunValidateAndExportExitZero", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		tmpOut := t.TempDir()
		start := time.Now()
		stdout, stderr, code := runDataCompiler(t, bin,
			"--validate", "--export",
			"--src", filepath.Join(cfg.gamedataDir, "sources"),
			"--schemas", filepath.Join(cfg.gamedataDir, "schemas"),
			"--out", tmpOut,
		)
		elapsed := time.Since(start)
		if code != 0 {
			t.Fatalf("datacompiler --validate --export failed (code=%d)\nstdout:%s\nstderr:%s", code, stdout, stderr)
		}
		if elapsed > 2*time.Second {
			t.Fatalf("compiled datacompiler took %s (> 2s budget)", elapsed)
		}
	})

	t.Run("T1_F8_02_DatacompilerUnitTestsPass", func(t *testing.T) {
		requireDirExists(t, filepath.Join(cfg.backendDir, "cmd", "datacompiler"), "backend/cmd/datacompiler")
		stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, nil, 60*time.Second, "go", "test", "-v", "-count=1", "./cmd/datacompiler/...")
		if err != nil || code != 0 {
			t.Fatalf("go test ./cmd/datacompiler/... failed (code=%d err=%v)\nstdout:\n%s\nstderr:\n%s", code, err, stdout, stderr)
		}
		if strings.Contains(stdout, "[no test files]") || !strings.Contains(stdout, "PASS") {
			t.Fatalf("datacompiler must have >=1 passing unit test, got:\n%s", stdout)
		}
	})

	t.Run("T1_F8_03_BackendBundleAndSHA256Generated", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		tmpOut := t.TempDir()
		stdout, stderr, code := runDataCompiler(t, bin,
			"--validate", "--export",
			"--src", filepath.Join(cfg.gamedataDir, "sources"),
			"--schemas", filepath.Join(cfg.gamedataDir, "schemas"),
			"--out", tmpOut,
		)
		if code != 0 {
			t.Fatalf("datacompiler failed (code=%d)\nstdout:%s\nstderr:%s", code, stdout, stderr)
		}
		bundlePath := filepath.Join(tmpOut, "backend", "gamedata_bundle.json")
		shaPath := filepath.Join(tmpOut, "backend", "gamedata_bundle.sha256")
		requireFileExists(t, bundlePath, "gamedata_bundle.json")
		requireFileExists(t, shaPath, "gamedata_bundle.sha256")

		shaRaw, _ := os.ReadFile(shaPath)
		shaHex := strings.Fields(strings.TrimSpace(string(shaRaw)))[0]
		if !regexp.MustCompile(`^[0-9a-f]{64}$`).MatchString(shaHex) {
			t.Fatalf("invalid sha256 file content %q", string(shaRaw))
		}
		bundle := readJSONMap(t, bundlePath)
		man, _ := bundle["manifest"].(map[string]any)
		if man["gamedata_hash"] != shaHex {
			t.Fatalf("bundle manifest.gamedata_hash=%v != sha256 file=%s", man["gamedata_hash"], shaHex)
		}
		for _, sec := range []string{
			"manifest", "global_combat_constants", "exp_table", "races", "skills",
			"transformations", "items", "item_sets", "affixes", "monsters_and_bosses",
			"loot_tables", "enhance_curves", "pity_shop_entries",
		} {
			if _, ok := bundle[sec]; !ok {
				t.Fatalf("gamedata_bundle.json missing section %q", sec)
			}
		}
	})

	t.Run("T1_F8_04_UE5DataTableJSONAndCSVOutputs", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		tmpOut := t.TempDir()
		_, stderr, code := runDataCompiler(t, bin,
			"--validate", "--export",
			"--src", filepath.Join(cfg.gamedataDir, "sources"),
			"--schemas", filepath.Join(cfg.gamedataDir, "schemas"),
			"--out", tmpOut,
		)
		if code != 0 {
			t.Fatalf("datacompiler failed: %s", stderr)
		}
		tables := []string{
			"DT_Races", "DT_Skills", "DT_Transformations", "DT_Items",
			"DT_Affixes", "DT_MonstersAndBosses", "DT_LootTables", "DT_PresentationPack",
		}
		for _, tbl := range tables {
			jsonPath := filepath.Join(tmpOut, "unreal", tbl+".json")
			csvPath := filepath.Join(tmpOut, "unreal", tbl+".csv")
			requireFileExists(t, jsonPath, tbl+".json")
			requireFileExists(t, csvPath, tbl+".csv")

			_, jNames := parseUE5JSON(t, jsonPath)
			_, csvRows := parseUE5CSV(t, csvPath)
			csvNames := make([]string, 0, len(csvRows))
			for _, r := range csvRows {
				if n, ok := r["Name"]; ok && n != "" {
					csvNames = append(csvNames, n)
				} else {
					csvNames = append(csvNames, r["RowName"])
				}
			}
			sort.Strings(jNames)
			sort.Strings(csvNames)
			if len(jNames) != len(csvNames) {
				t.Fatalf("%s: JSON row count (%d) != CSV row count (%d)", tbl, len(jNames), len(csvNames))
			}
			for i := range jNames {
				if jNames[i] != csvNames[i] {
					t.Fatalf("%s: JSON/CSV Name mismatch at %d: %q vs %q", tbl, i, jNames[i], csvNames[i])
				}
			}
		}
	})

	t.Run("T1_F8_05_SupportsBothIPPackFlags", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		dirDB := filepath.Join(t.TempDir(), "db")
		dirOrig := filepath.Join(t.TempDir(), "orig")

		for _, tc := range []struct {
			pack string
			out  string
		}{
			{"Pack_DB_Reference", dirDB},
			{"Pack_Original_Commercial", dirOrig},
		} {
			stdout, stderr, code := runDataCompiler(t, bin,
				"--validate", "--export",
				"--src", filepath.Join(cfg.gamedataDir, "sources"),
				"--schemas", filepath.Join(cfg.gamedataDir, "schemas"),
				"--ip-pack", tc.pack,
				"--out", tc.out,
			)
			if code != 0 {
				t.Fatalf("--ip-pack %s failed (code=%d)\nstdout:%s\nstderr:%s", tc.pack, code, stdout, stderr)
			}
		}
		dbPres, _ := os.ReadFile(filepath.Join(dirDB, "unreal", "DT_PresentationPack.json"))
		origPres, _ := os.ReadFile(filepath.Join(dirOrig, "unreal", "DT_PresentationPack.json"))
		if !strings.Contains(string(dbPres), "Kamehameha") && !strings.Contains(string(dbPres), "Charged Ki Wave") {
			t.Fatalf("Pack_DB_Reference DT_PresentationPack.json missing expected DB reference name")
		}
		if !strings.Contains(string(origPres), "Thiên Ngân Xung Pháo") && !strings.Contains(string(origPres), "Astral Convergence Cannon") {
			t.Fatalf("Pack_Original_Commercial DT_PresentationPack.json missing expected commercial name")
		}
	})

	t.Run("T1_F8_06_RepoBuildArtifactsPresentAndValid", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		tmpOut := t.TempDir()
		_, stderr, code := runDataCompiler(t, bin,
			"--validate", "--export",
			"--src", filepath.Join(cfg.gamedataDir, "sources"),
			"--schemas", filepath.Join(cfg.gamedataDir, "schemas"),
			"--out", tmpOut,
		)
		if code != 0 {
			t.Fatalf("fresh export failed: %s", stderr)
		}
		repoShaPath := filepath.Join(cfg.gamedataDir, "build", "backend", "gamedata_bundle.sha256")
		requireFileExists(t, repoShaPath, "gamedata/build/backend/gamedata_bundle.sha256")
		requireFileExists(t, filepath.Join(cfg.gamedataDir, "build", "backend", "gamedata_bundle.json"), "gamedata/build/backend/gamedata_bundle.json")
		repoSha, _ := os.ReadFile(repoShaPath)
		freshSha, _ := os.ReadFile(filepath.Join(tmpOut, "backend", "gamedata_bundle.sha256"))
		if strings.TrimSpace(string(repoSha)) != strings.TrimSpace(string(freshSha)) {
			t.Fatalf("repo gamedata_bundle.sha256 (%q) != fresh compile (%q)",
				strings.TrimSpace(string(repoSha)), strings.TrimSpace(string(freshSha)))
		}
		for _, tbl := range []string{"DT_Races", "DT_Skills", "DT_Items", "DT_PresentationPack"} {
			requireFileExists(t, filepath.Join(cfg.gamedataDir, "build", "unreal", tbl+".json"), tbl+".json")
			requireFileExists(t, filepath.Join(cfg.gamedataDir, "build", "unreal", tbl+".csv"), tbl+".csv")
		}
	})
}

// ============================================================================
// Feature #9: Docker Compose & Observability (6 subtests)
// ============================================================================

func TestTier1_F9_DockerComposeAndObservability(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T1_F9_01_ComposeConfigValid", func(t *testing.T) {
		requireFileExists(t, cfg.composeFile, "docker-compose.local.yml")
		stdout, stderr, code, err := runCmd(t, 30*time.Second, "docker", "compose", "-f", cfg.composeFile, "config", "--quiet")
		if err != nil || code != 0 {
			t.Fatalf("docker compose config --quiet failed (code=%d err=%v)\nstdout:%s\nstderr:%s", code, err, stdout, stderr)
		}
	})

	t.Run("T1_F9_02_All9ServicesAndPostgresPort5433Defined", func(t *testing.T) {
		requireFileExists(t, cfg.composeFile, "docker-compose.local.yml")
		stdout, stderr, code, err := runCmd(t, 30*time.Second, "docker", "compose", "-f", cfg.composeFile, "config", "--services")
		if err != nil || code != 0 {
			t.Fatalf("docker compose config --services failed (code=%d err=%v): %s", code, err, stderr)
		}
		svcs := nonEmptyLines(stdout)
		sort.Strings(svcs)
		wantSvcs := []string{"coordinator", "gameapi", "gateway", "grafana", "migrate", "nats", "postgres", "prometheus", "redis"}
		if len(svcs) != len(wantSvcs) {
			t.Fatalf("expected %d compose services %v, got %d: %v", len(wantSvcs), wantSvcs, len(svcs), svcs)
		}
		for i := range wantSvcs {
			if svcs[i] != wantSvcs[i] {
				t.Fatalf("service mismatch: got %v, want %v", svcs, wantSvcs)
			}
		}
		rawCompose, _ := os.ReadFile(cfg.composeFile)
		if !strings.Contains(string(rawCompose), "5433") || !strings.Contains(string(rawCompose), "5432") {
			t.Fatalf("compose file missing 5433:5432 PostgreSQL port mapping")
		}
		cfgOut, _, code2, _ := runCmd(t, 30*time.Second, "docker", "compose", "-f", cfg.composeFile, "config")
		if code2 != 0 || !strings.Contains(cfgOut, "5433") {
			t.Fatalf("resolved compose config missing host port 5433")
		}
	})

	t.Run("T1_F9_03_PrometheusConfigAndAlertRulesValid", func(t *testing.T) {
		promCfg := filepath.Join(cfg.deployDir, "prometheus", "prometheus.yml")
		alertsCfg := filepath.Join(cfg.deployDir, "prometheus", "alerts.yml")
		requireFileExists(t, promCfg, "prometheus.yml")
		requireFileExists(t, alertsCfg, "alerts.yml")

		promRaw, _ := os.ReadFile(promCfg)
		for _, want := range []string{"iu-gateway", "gateway:9090", "iu-gameapi", "gameapi:9091", "iu-coordinator", "coordinator:9092"} {
			if !strings.Contains(string(promRaw), want) {
				t.Fatalf("prometheus.yml missing %q", want)
			}
		}
		alertsRaw, _ := os.ReadFile(alertsCfg)
		for _, rule := range []string{"FencingEpochConflictDetected", "ZoneServerTickOverBudget30Hz", "InternalGrpcHighLatencyOrErrors"} {
			if !strings.Contains(string(alertsRaw), rule) {
				t.Fatalf("alerts.yml missing alert rule %q", rule)
			}
		}

		skipIfDockerDisabled(t, cfg)
		stdout, stderr, code, err := runCmd(t, 30*time.Second, "docker", "exec", "iu-prometheus", "promtool", "check", "config", "/etc/prometheus/prometheus.yml")
		if err != nil || code != 0 {
			t.Fatalf("promtool check config failed (code=%d err=%v)\nstdout:%s\nstderr:%s", code, err, stdout, stderr)
		}
	})

	t.Run("T1_F9_04_GrafanaDatasourceProvisioningValid", func(t *testing.T) {
		dsPath := filepath.Join(cfg.deployDir, "grafana", "provisioning", "datasources", "prometheus.yml")
		requireFileExists(t, dsPath, "grafana datasource prometheus.yml")
		dsRaw, _ := os.ReadFile(dsPath)
		for _, want := range []string{"prometheus", "http://prometheus:9090", "isDefault: true"} {
			if !strings.Contains(string(dsRaw), want) {
				t.Fatalf("grafana datasource YAML missing %q", want)
			}
		}
		skipIfDockerDisabled(t, cfg)
		var datasources []map[string]any
		status := httpGetJSONWithBasicAuth(t, cfg.grafanaURL+"/api/datasources", "admin", "iu_grafana_admin", &datasources)
		if status != http.StatusOK || len(datasources) == 0 {
			t.Fatalf("Grafana /api/datasources failed: status=%d count=%d", status, len(datasources))
		}
		found := false
		for _, ds := range datasources {
			if ds["type"] == "prometheus" && ds["url"] == "http://prometheus:9090" {
				found = true
				break
			}
		}
		if !found {
			t.Fatalf("Grafana datasources missing prometheus@http://prometheus:9090: %+v", datasources)
		}
	})

	t.Run("T1_F9_05_DockerfileGoDistrolessNonroot", func(t *testing.T) {
		dfPath := filepath.Join(cfg.backendDir, "build", "Dockerfile.go")
		requireFileExists(t, dfPath, "backend/build/Dockerfile.go")
		raw, _ := os.ReadFile(dfPath)
		txt := string(raw)
		for _, want := range []string{"golang:1.23", "gcr.io/distroless/static-debian12:nonroot", "USER 65532:65532", `ENTRYPOINT ["/app/service"]`} {
			if !strings.Contains(txt, want) {
				t.Fatalf("Dockerfile.go missing %q", want)
			}
		}
		for _, bad := range []string{"COPY api/", "COPY gen/"} {
			if strings.Contains(txt, bad) {
				t.Fatalf("Dockerfile.go contains forbidden VS-02 directive %q", bad)
			}
		}
	})

	t.Run("T1_F9_06_ComposeNetworkAndNamedVolumesDefined", func(t *testing.T) {
		requireFileExists(t, cfg.composeFile, "docker-compose.local.yml")
		stdout, stderr, code, err := runCmd(t, 30*time.Second, "docker", "compose", "-f", cfg.composeFile, "config", "--format", "json")
		if err != nil || code != 0 {
			t.Fatalf("docker compose config --format json failed (code=%d err=%v): %s", code, err, stderr)
		}
		var doc map[string]any
		if err := json.Unmarshal([]byte(stdout), &doc); err != nil {
			t.Fatalf("decode compose json: %v", err)
		}
		nets, _ := doc["networks"].(map[string]any)
		if len(nets) == 0 || !strings.Contains(stdout, "iu-local-net") {
			t.Fatalf("compose config missing network iu-local-net")
		}
		vols, _ := doc["volumes"].(map[string]any)
		for _, v := range []string{"iu_pgdata", "iu_redisdata", "iu_natsdata", "iu_promdata", "iu_grafanadata"} {
			if _, ok := vols[v]; !ok {
				t.Fatalf("compose config missing named volume %q (vols=%v)", v, vols)
			}
		}
	})
}

// ============================================================================
// Feature #10: Live Stack Health & Endpoints (6 subtests)
// ============================================================================

func TestTier1_F10_LiveStackHealthAndEndpoints(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T1_F10_01_AllContainersHealthyAndMigrateExitedZero", func(t *testing.T) {
		requireFileExists(t, cfg.composeFile, "docker-compose.local.yml")
		skipIfDockerDisabled(t, cfg)
		stdout, stderr, code, err := runCmd(t, 30*time.Second, "docker", "compose", "-f", cfg.composeFile, "ps", "-a", "--format", "json")
		if err != nil || code != 0 {
			t.Fatalf("docker compose ps failed (code=%d err=%v): %s", code, err, stderr)
		}
		containers := parseComposePSJSON(t, stdout)
		wantRunning := []string{"iu-postgres", "iu-redis", "iu-nats", "iu-gameapi", "iu-coordinator", "iu-gateway", "iu-prometheus", "iu-grafana"}
		for _, name := range wantRunning {
			c, ok := containers[name]
			if !ok {
				t.Fatalf("container %q not found in compose ps", name)
			}
			if c.State != "running" || c.Health != "healthy" {
				t.Fatalf("container %q State=%q Health=%q (want running/healthy)", name, c.State, c.Health)
			}
		}
		mig, ok := containers["iu-migrate"]
		if !ok {
			t.Fatalf("container iu-migrate not found in compose ps")
		}
		if mig.State != "exited" || mig.ExitCode != 0 {
			t.Fatalf("iu-migrate State=%q ExitCode=%d (want exited/0)", mig.State, mig.ExitCode)
		}
	})

	t.Run("T1_F10_02_AllGoServicesHealthz200OK", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		for _, url := range []string{
			cfg.gatewayMetricsURL + "/healthz",
			cfg.gameapiMetricsURL + "/healthz",
			cfg.coordinatorMetricsURL + "/healthz",
		} {
			var body map[string]any
			httpGetJSON(t, url, &body)
			if body["status"] != "ok" {
				t.Fatalf("%s status = %v, want ok", url, body["status"])
			}
			if _, ok := body["gamedata_hash"]; !ok {
				t.Fatalf("%s missing gamedata_hash key", url)
			}
		}
	})

	t.Run("T1_F10_03_PrometheusMetricsEndpointsAndScrapeTargetsUp", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		for _, url := range []string{
			cfg.gatewayMetricsURL + "/metrics",
			cfg.gameapiMetricsURL + "/metrics",
			cfg.coordinatorMetricsURL + "/metrics",
		} {
			txt := httpGetText(t, url)
			if !strings.Contains(txt, "go_goroutines") || !strings.Contains(txt, "process_cpu_seconds_total") {
				t.Fatalf("%s missing standard Go/process metrics", url)
			}
		}
		var targetsResp struct {
			Status string `json:"status"`
			Data   struct {
				ActiveTargets []struct {
					Labels map[string]string `json:"labels"`
					Health string            `json:"health"`
				} `json:"activeTargets"`
			} `json:"data"`
		}
		httpGetJSON(t, cfg.prometheusURL+"/api/v1/targets", &targetsResp)
		upJobs := make(map[string]string)
		for _, tgt := range targetsResp.Data.ActiveTargets {
			upJobs[tgt.Labels["job"]] = tgt.Health
		}
		for _, job := range []string{"iu-gateway", "iu-gameapi", "iu-coordinator"} {
			if upJobs[job] != "up" {
				t.Fatalf("Prometheus target %q health = %q (want up); all=%v", job, upJobs[job], upJobs)
			}
		}
	})

	t.Run("T1_F10_04_NatsHealthzAndJetStreamEnabled", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		var hz map[string]any
		httpGetJSON(t, cfg.natsMonitorURL+"/healthz", &hz)
		if hz["status"] != "ok" {
			t.Fatalf("NATS /healthz status=%v, want ok", hz["status"])
		}
		var jsz map[string]any
		httpGetJSON(t, cfg.natsMonitorURL+"/jsz", &jsz)
		if disabled, ok := jsz["disabled"].(bool); ok && disabled {
			t.Fatalf("NATS JetStream reported disabled=true")
		}
		if _, hasCfg := jsz["config"]; !hasCfg {
			t.Fatalf("NATS /jsz missing JetStream config: %+v", jsz)
		}
	})

	t.Run("T1_F10_05_PostgresAndRedisLiveReadiness", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		stdout, stderr, code, err := runCmd(t, 15*time.Second, "docker", "exec", cfg.pgContainer, "pg_isready", "-U", cfg.pgUser, "-d", cfg.pgDB)
		if err != nil || code != 0 || !strings.Contains(stdout, "accepting connections") {
			t.Fatalf("pg_isready failed (code=%d err=%v): stdout=%q stderr=%q", code, err, stdout, stderr)
		}
		if err := dialTCPCheck(t, "127.0.0.1:"+cfg.pgHostPort, 5*time.Second); err != nil {
			t.Fatalf("PG host port 127.0.0.1:%s unreachable: %v", cfg.pgHostPort, err)
		}
		if got := psqlExecOrFatal(t, cfg, cfg.pgDB, "SELECT 1;"); got != "1" {
			t.Fatalf("SELECT 1 returned %q", got)
		}
		if got := redisExecOrFatal(t, cfg, "PING"); got != "PONG" {
			t.Fatalf("redis PING returned %q", got)
		}
		respPing(t, cfg.redisAddr)
	})

	t.Run("T1_F10_06_PrometheusAndGrafanaHealthEndpoints", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		promBody := httpGetText(t, cfg.prometheusURL+"/-/healthy")
		if !strings.Contains(promBody, "Healthy") {
			t.Fatalf("Prometheus /-/healthy unexpected body %q", promBody)
		}
		var grafHealth map[string]any
		httpGetJSON(t, cfg.grafanaURL+"/api/health", &grafHealth)
		if grafHealth["database"] != "ok" {
			t.Fatalf("Grafana /api/health database = %v, want ok", grafHealth["database"])
		}
	})
}

// ============================================================================
// Tier 1 Local Helpers
// ============================================================================

func nonEmptyLines(s string) []string {
	raw := strings.Split(strings.ReplaceAll(s, "\r\n", "\n"), "\n")
	out := make([]string, 0, len(raw))
	for _, l := range raw {
		t := strings.TrimSpace(l)
		if t != "" {
			out = append(out, t)
		}
	}
	return out
}

func asFloat64(v any) (float64, bool) {
	switch n := v.(type) {
	case float64:
		return n, true
	case int:
		return float64(n), true
	case int64:
		return float64(n), true
	case json.Number:
		f, err := n.Float64()
		return f, err == nil
	default:
		return 0, false
	}
}

func findJSONNumberField(node any, key string) (float64, bool) {
	switch v := node.(type) {
	case map[string]any:
		if raw, ok := v[key]; ok {
			if n, okNum := asFloat64(raw); okNum {
				return n, true
			}
		}
		keys := make([]string, 0, len(v))
		for k := range v {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		for _, k := range keys {
			if n, ok := findJSONNumberField(v[k], key); ok {
				return n, true
			}
		}
	case []any:
		for _, elem := range v {
			if n, ok := findJSONNumberField(elem, key); ok {
				return n, true
			}
		}
	}
	return 0, false
}

func schemaTypeIncludesObject(typVal any) bool {
	switch tv := typVal.(type) {
	case string:
		return tv == "object"
	case []any:
		for _, item := range tv {
			if s, ok := item.(string); ok && s == "object" {
				return true
			}
		}
	}
	return false
}

func checkObjectSchemasHaveAdditionalPropertiesFalse(fileName, jsonPath string, node any) error {
	switch v := node.(type) {
	case map[string]any:
		if schemaTypeIncludesObject(v["type"]) {
			if _, hasProps := v["properties"]; hasProps {
				ap, ok := v["additionalProperties"].(bool)
				if !ok || ap {
					return fmt.Errorf("%s (%s): object schema with properties MUST set additionalProperties: false", fileName, jsonPath)
				}
			}
		}
		keys := make([]string, 0, len(v))
		for k := range v {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		for _, k := range keys {
			if err := checkObjectSchemasHaveAdditionalPropertiesFalse(fileName, jsonPath+"."+k, v[k]); err != nil {
				return err
			}
		}
	case []any:
		for i, child := range v {
			if err := checkObjectSchemasHaveAdditionalPropertiesFalse(fileName, fmt.Sprintf("%s[%d]", jsonPath, i), child); err != nil {
				return err
			}
		}
	}
	return nil
}

func assertObjectSchemasHaveAdditionalPropertiesFalse(t *testing.T, fileName, jsonPath string, node any) {
	t.Helper()
	if err := checkObjectSchemasHaveAdditionalPropertiesFalse(fileName, jsonPath, node); err != nil {
		t.Fatal(err)
	}
}

type schemaDocSet struct {
	files []string
	docs  map[string]map[string]any
	byID  map[string]string
}

func loadSchemaDocSet(t *testing.T, schemasDir string) *schemaDocSet {
	t.Helper()
	entries, err := os.ReadDir(schemasDir)
	if err != nil {
		t.Fatal(err)
	}
	ds := &schemaDocSet{
		docs: make(map[string]map[string]any),
		byID: make(map[string]string),
	}
	for _, e := range entries {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".schema.json") {
			continue
		}
		name := e.Name()
		m := readJSONMap(t, filepath.Join(schemasDir, name))
		ds.files = append(ds.files, name)
		ds.docs[name] = m
		if idStr, ok := m["$id"].(string); ok && idStr != "" {
			ds.byID[idStr] = name
			if idx := strings.LastIndex(idStr, "/"); idx >= 0 && idx+1 < len(idStr) {
				ds.byID[idStr[idx+1:]] = name
			}
		}
	}
	sort.Strings(ds.files)
	return ds
}

func (ds *schemaDocSet) resolveRef(currentFile, ref string) (string, any, bool) {
	if ref == "" {
		return "", nil, false
	}
	filePart := ""
	fragPart := ""
	if idx := strings.IndexByte(ref, '#'); idx >= 0 {
		filePart = ref[:idx]
		fragPart = ref[idx+1:]
	} else {
		filePart = ref
	}

	targetFile := currentFile
	if filePart != "" {
		if mapped, ok := ds.byID[filePart]; ok {
			targetFile = mapped
		} else if _, ok := ds.docs[filePart]; ok {
			targetFile = filePart
		} else {
			base := filepath.Base(filePart)
			if mapped, ok := ds.byID[base]; ok {
				targetFile = mapped
			} else if _, ok := ds.docs[base]; ok {
				targetFile = base
			} else {
				return "", nil, false
			}
		}
	}

	doc, ok := ds.docs[targetFile]
	if !ok {
		return "", nil, false
	}
	if fragPart == "" || fragPart == "/" {
		return targetFile, doc, true
	}
	if !strings.HasPrefix(fragPart, "/") {
		return "", nil, false
	}

	var cur any = doc
	parts := strings.Split(fragPart[1:], "/")
	for _, rawSeg := range parts {
		seg := strings.ReplaceAll(strings.ReplaceAll(rawSeg, "~1", "/"), "~0", "~")
		switch typed := cur.(type) {
		case map[string]any:
			nxt, exists := typed[seg]
			if !exists {
				return "", nil, false
			}
			cur = nxt
		case []any:
			idx, err := strconv.Atoi(seg)
			if err != nil || idx < 0 || idx >= len(typed) {
				return "", nil, false
			}
			cur = typed[idx]
		default:
			return "", nil, false
		}
	}
	return targetFile, cur, true
}

func collectSchemaPatterns(t *testing.T, schemasDir string) map[string][]string {
	t.Helper()
	ds := loadSchemaDocSet(t, schemasDir)
	out := make(map[string][]string)
	for _, fileName := range ds.files {
		walkSchemaForPatterns(ds, fileName, ds.docs[fileName], "", out, make(map[string]bool))
	}
	return out
}

func walkSchemaForPatterns(ds *schemaDocSet, currentFile string, node any, currentProp string, out map[string][]string, visited map[string]bool) {
	switch v := node.(type) {
	case map[string]any:
		if currentProp != "" {
			if pat, ok := v["pattern"].(string); ok {
				out[currentProp] = append(out[currentProp], pat)
			}
			if ref, ok := v["$ref"].(string); ok && ref != "" {
				if targetFile, targetNode, okRes := ds.resolveRef(currentFile, ref); okRes {
					visitKey := currentProp + "|" + targetFile + "#" + ref
					if !visited[visitKey] {
						visited[visitKey] = true
						walkSchemaForPatterns(ds, targetFile, targetNode, currentProp, out, visited)
						delete(visited, visitKey)
					}
				}
			}
		}
		for _, containerKey := range []string{"properties", "$defs", "definitions"} {
			if sub, ok := v[containerKey].(map[string]any); ok {
				for propName, propVal := range sub {
					walkSchemaForPatterns(ds, currentFile, propVal, propName, out, visited)
				}
			}
		}
		for k, val := range v {
			if k == "properties" || k == "$defs" || k == "definitions" || k == "$ref" {
				continue
			}
			walkSchemaForPatterns(ds, currentFile, val, currentProp, out, visited)
		}
	case []any:
		for _, item := range v {
			walkSchemaForPatterns(ds, currentFile, item, currentProp, out, visited)
		}
	}
}

func collectSchemaPropertyNodes(t *testing.T, schemasDir string) map[string][]map[string]any {
	t.Helper()
	ds := loadSchemaDocSet(t, schemasDir)
	out := make(map[string][]map[string]any)
	for _, fileName := range ds.files {
		walkSchemaForPropNodes(ds, fileName, ds.docs[fileName], out)
	}
	return out
}

func expandPropNodeCandidates(ds *schemaDocSet, currentFile string, node map[string]any, visited map[string]bool) []map[string]any {
	var list []map[string]any
	list = append(list, node)

	if ref, ok := node["$ref"].(string); ok && ref != "" {
		if targetFile, targetNode, okRes := ds.resolveRef(currentFile, ref); okRes {
			visitKey := targetFile + "#" + ref
			if !visited[visitKey] {
				visited[visitKey] = true
				if tm, okMap := targetNode.(map[string]any); okMap {
					list = append(list, expandPropNodeCandidates(ds, targetFile, tm, visited)...)
				}
				delete(visited, visitKey)
			}
		}
	}
	for _, combKey := range []string{"allOf", "anyOf", "oneOf"} {
		if arr, ok := node[combKey].([]any); ok {
			for _, elem := range arr {
				if em, okMap := elem.(map[string]any); okMap {
					list = append(list, expandPropNodeCandidates(ds, currentFile, em, visited)...)
				}
			}
		}
	}
	if itemsMap, ok := node["items"].(map[string]any); ok {
		list = append(list, expandPropNodeCandidates(ds, currentFile, itemsMap, visited)...)
	}

	// Merge constraints across node + $ref/allOf chain so split minimum/maximum/enum still match.
	merged := make(map[string]any)
	for i := len(list) - 1; i >= 0; i-- {
		for _, k := range []string{"pattern", "enum", "minimum", "maximum"} {
			if val, ok := list[i][k]; ok {
				merged[k] = val
			}
		}
	}
	if len(merged) > 0 {
		list = append(list, merged)
	}
	return list
}

func walkSchemaForPropNodes(ds *schemaDocSet, currentFile string, node any, out map[string][]map[string]any) {
	switch v := node.(type) {
	case map[string]any:
		for _, containerKey := range []string{"properties", "$defs", "definitions"} {
			if sub, ok := v[containerKey].(map[string]any); ok {
				for propName, propVal := range sub {
					if pm, ok := propVal.(map[string]any); ok {
						expanded := expandPropNodeCandidates(ds, currentFile, pm, make(map[string]bool))
						out[propName] = append(out[propName], expanded...)
					}
					walkSchemaForPropNodes(ds, currentFile, propVal, out)
				}
			}
		}
		for k, val := range v {
			if k == "properties" || k == "$defs" || k == "definitions" {
				continue
			}
			walkSchemaForPropNodes(ds, currentFile, val, out)
		}
	case []any:
		for _, item := range v {
			walkSchemaForPropNodes(ds, currentFile, item, out)
		}
	}
}

func findRecordByID(t *testing.T, data any, targetID string) map[string]any {
	t.Helper()
	rec, ok := walkFindRecordByID(data, targetID)
	if !ok {
		t.Fatalf("record with ID %q not found", targetID)
	}
	return rec
}

func walkFindRecordByID(node any, targetID string) (map[string]any, bool) {
	switch v := node.(type) {
	case map[string]any:
		for _, field := range idFieldCandidates {
			if s, ok := v[field].(string); ok && s == targetID {
				return v, true
			}
		}
		if child, ok := v[targetID].(map[string]any); ok {
			return child, true
		}
		for _, val := range v {
			if found, ok := walkFindRecordByID(val, targetID); ok {
				return found, true
			}
		}
	case []any:
		for _, elem := range v {
			if found, ok := walkFindRecordByID(elem, targetID); ok {
				return found, true
			}
		}
	}
	return nil, false
}

func collectExpRows(t *testing.T, data any) map[int]map[string]any {
	t.Helper()
	rows := make(map[int]map[string]any)
	var walk func(any)
	walk = func(node any) {
		switch v := node.(type) {
		case map[string]any:
			if lv, ok := asFloat64(v["level"]); ok {
				if _, hasExp := v["exp_to_next"]; hasExp {
					rows[int(lv)] = v
					return
				}
			}
			for _, val := range v {
				walk(val)
			}
		case []any:
			for _, elem := range v {
				walk(elem)
			}
		}
	}
	walk(data)
	return rows
}

func hasCanonicalMapping(node any, cid string) bool {
	switch v := node.(type) {
	case map[string]any:
		if entry, ok := v[cid]; ok {
			if em, ok := entry.(map[string]any); ok {
				if dn, ok := em["display_name"].(string); ok && strings.TrimSpace(dn) == "" {
					return false
				}
			}
			return true
		}
		for _, field := range []string{"canonical_id", "id"} {
			if s, ok := v[field].(string); ok && s == cid {
				if dn, ok := v["display_name"].(string); ok && strings.TrimSpace(dn) == "" {
					return false
				}
				return true
			}
		}
		for _, val := range v {
			if hasCanonicalMapping(val, cid) {
				return true
			}
		}
	case []any:
		for _, elem := range v {
			if hasCanonicalMapping(elem, cid) {
				return true
			}
		}
	}
	return false
}

func scanCoreNode(t *testing.T, path string, node any, forbiddenKeys map[string]bool) {
	t.Helper()
	switch v := node.(type) {
	case map[string]any:
		for k, val := range v {
			if forbiddenKeys[k] {
				t.Fatalf("core file %s contains forbidden presentation/asset key %q", path, k)
			}
			scanCoreNode(t, path, val, forbiddenKeys)
		}
	case []any:
		for _, elem := range v {
			scanCoreNode(t, path, elem, forbiddenKeys)
		}
	case string:
		if strings.Contains(v, "/Game/") {
			t.Fatalf("core file %s contains forbidden UE5 asset path %q", path, v)
		}
		checkForbiddenIPString(t, path, v)
	}
}

func scanPackOriginalNode(t *testing.T, path string, node any) {
	t.Helper()
	switch v := node.(type) {
	case map[string]any:
		for k, val := range v {
			checkForbiddenIPString(t, path+" key:"+k, k)
			scanPackOriginalNode(t, path, val)
		}
	case []any:
		for _, elem := range v {
			scanPackOriginalNode(t, path, elem)
		}
	case string:
		checkForbiddenIPString(t, path, v)
	}
}

func checkForbiddenIPString(t *testing.T, loc, val string) {
	t.Helper()
	lower := strings.ToLower(strings.TrimSpace(val))
	if whitelistedReadmeTokens[lower] {
		return
	}
	for _, kw := range forbiddenIPKeywords {
		re := regexp.MustCompile(`(^|[^a-z0-9])` + regexp.QuoteMeta(kw) + `([^a-z0-9]|$)`)
		if re.MatchString(lower) {
			t.Fatalf("IP hygiene violation in %s: forbidden keyword %q matched in %q", loc, kw, val)
		}
	}
}

type composePSItem struct {
	Name     string `json:"Name"`
	State    string `json:"State"`
	Health   string `json:"Health"`
	ExitCode int    `json:"ExitCode"`
}

func parseComposePSJSON(t *testing.T, stdout string) map[string]composePSItem {
	t.Helper()
	trimmed := strings.TrimSpace(stdout)
	res := make(map[string]composePSItem)
	if trimmed == "" {
		return res
	}
	if strings.HasPrefix(trimmed, "[") {
		var items []composePSItem
		if err := json.Unmarshal([]byte(trimmed), &items); err != nil {
			t.Fatalf("decode compose ps JSON array: %v\n%s", err, stdout)
		}
		for _, it := range items {
			res[it.Name] = it
		}
		return res
	}
	for _, line := range nonEmptyLines(trimmed) {
		var it composePSItem
		if err := json.Unmarshal([]byte(line), &it); err != nil {
			t.Fatalf("decode compose ps JSON line: %v\nline: %s", err, line)
		}
		res[it.Name] = it
	}
	return res
}
