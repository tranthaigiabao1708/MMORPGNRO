package e2e_test

import (
	"encoding/json"
	"fmt"
	"io/fs"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"testing"
	"time"
)

// ============================================================================
// Feature #1: Go Backend Scaffold & VS-02 Boundary (6 subtests)
// ============================================================================

func TestTier2_F1_BoundaryAndExclusion(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T2_F1_01_VS02NegativeContentScanZeroMatches", func(t *testing.T) {
		requireDirExists(t, cfg.backendDir, "backend")
		assertNoVS02Artifacts(t, cfg.repoRoot, cfg.backendDir)
	})

	t.Run("T2_F1_02_HealthcheckFailsOnClosedPortAnd503", func(t *testing.T) {
		closedAddr := getFreeLocalAddr(t)
		srv503 := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			w.WriteHeader(http.StatusServiceUnavailable)
			_, _ = w.Write([]byte(`{"status":"unavailable"}`))
		}))
		defer srv503.Close()

		for _, svc := range []string{"gateway", "gameapi", "coordinator"} {
			bin := compileBackendBinary(t, cfg, "./cmd/"+svc, svc)
			for _, targetURL := range []string{"http://" + closedAddr + "/healthz", srv503.URL + "/healthz"} {
				stdout, stderr, code, _ := runCmd(t, 5*time.Second, bin, "-healthcheck="+targetURL)
				if code == 0 {
					t.Fatalf("%s -healthcheck=%s unexpectedly exited 0 (stdout=%q stderr=%q)",
						svc, targetURL, stdout, stderr)
				}
			}
		}
	})

	t.Run("T2_F1_03_InvalidGameDataDirHandledGracefully", func(t *testing.T) {
		bin := compileBackendBinary(t, cfg, "./cmd/gameapi", "gameapi")
		missingGamedata := filepath.Join(t.TempDir(), "does_not_exist")
		metricsAddr := getFreeLocalAddr(t)
		grpcAddr := getFreeLocalAddr(t)

		cmd := exec.Command(bin)
		cmd.Dir = cfg.backendDir
		env := os.Environ()
		for k, v := range map[string]string{
			"IU_GAMEDATA_DIR":        missingGamedata,
			"IU_METRICS_LISTEN_ADDR": metricsAddr,
			"IU_GRPC_LISTEN_ADDR":    grpcAddr,
			"IU_ALLOW_OPTIONAL_DEPS": "true",
			"IU_PG_DSN":              "",
			"IU_REDIS_ADDR":          "",
			"IU_NATS_URL":            "",
		} {
			env = setOrReplaceEnv(env, k, v)
		}
		cmd.Env = env
		if err := cmd.Start(); err != nil {
			t.Fatalf("start gameapi with missing gamedata dir: %v", err)
		}
		t.Cleanup(func() {
			if cmd.Process != nil {
				_ = cmd.Process.Kill()
				_, _ = cmd.Process.Wait()
			}
		})

		deadline := time.Now().Add(5 * time.Second)
		var lastErr error
		var gotStatus int
		var gotBody []byte
		for time.Now().Before(deadline) {
			status, _, body, err := httpDoRaw(t, http.MethodGet, "http://"+metricsAddr+"/healthz", nil)
			if err == nil && status == http.StatusOK {
				gotStatus = status
				gotBody = body
				break
			}
			lastErr = err
			gotStatus = status
			time.Sleep(150 * time.Millisecond)
		}
		if gotStatus != http.StatusOK {
			t.Fatalf("gameapi with missing IU_GAMEDATA_DIR failed to serve 200 /healthz: status=%d err=%v", gotStatus, lastErr)
		}
		var payload map[string]any
		if err := json.Unmarshal(gotBody, &payload); err != nil {
			t.Fatalf("invalid /healthz JSON: %v (%s)", err, string(gotBody))
		}
		if payload["status"] != "ok" {
			t.Fatalf("expected /healthz status=ok even without gamedata, got %v", payload["status"])
		}
	})

	t.Run("T2_F1_04_GoVetClean", func(t *testing.T) {
		requireDirExists(t, cfg.backendDir, "backend")
		stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, nil, 90*time.Second, "go", "vet", "./...")
		if err != nil || code != 0 {
			t.Fatalf("go vet ./... in backend failed (code=%d err=%v)\nstdout:\n%s\nstderr:\n%s", code, err, stdout, stderr)
		}
	})

	t.Run("T2_F1_05_GameDataHashEmptyDirErrorAndCRLFNormalization", func(t *testing.T) {
		requireFileExists(t, filepath.Join(cfg.backendDir, "pkg", "gamedata", "hash.go"), "pkg/gamedata/hash.go")

		probeDir := t.TempDir()
		goModContent := fmt.Sprintf("module e2ehashprobe\n\ngo 1.23\n\nrequire github.com/infinite-universe/backend v0.0.0\n\nreplace github.com/infinite-universe/backend => %s\n",
			filepath.ToSlash(cfg.backendDir))
		probeMain := `package main

import (
	"fmt"
	"os"
	"github.com/infinite-universe/backend/pkg/gamedata"
)

func main() {
	for _, dir := range os.Args[1:4] {
		if _, err := gamedata.ComputeCanonicalGameDataHash(dir); err == nil {
			fmt.Fprintf(os.Stderr, "expected error for %s, got nil\n", dir)
			os.Exit(2)
		}
	}
	hLF, err := gamedata.ComputeCanonicalGameDataHash(os.Args[4])
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(3)
	}
	hCRLF, err := gamedata.ComputeCanonicalGameDataHash(os.Args[5])
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(4)
	}
	if hLF != hCRLF || len(hLF) != 64 {
		fmt.Fprintf(os.Stderr, "hash mismatch: %s vs %s\n", hLF, hCRLF)
		os.Exit(5)
	}
	fmt.Println(hLF)
}
`
		_ = os.WriteFile(filepath.Join(probeDir, "go.mod"), []byte(goModContent), 0o644)
		_ = os.WriteFile(filepath.Join(probeDir, "main.go"), []byte(probeMain), 0o644)

		emptyDir := filepath.Join(probeDir, "empty")
		missingDir := filepath.Join(probeDir, "missing")
		txtOnlyDir := filepath.Join(probeDir, "txtonly")
		lfDir := filepath.Join(probeDir, "lf")
		crlfDir := filepath.Join(probeDir, "crlf")
		for _, d := range []string{emptyDir, txtOnlyDir, lfDir, crlfDir} {
			_ = os.MkdirAll(d, 0o755)
		}
		_ = os.WriteFile(filepath.Join(txtOnlyDir, "readme.txt"), []byte("not json"), 0o644)
		_ = os.WriteFile(filepath.Join(lfDir, "c.json"), []byte("{\n  \"k\": 1\n}\n"), 0o644)
		_ = os.WriteFile(filepath.Join(crlfDir, "c.json"), []byte("{\r\n  \"k\": 1\r\n}\r\n"), 0o644)

		stdout, stderr, code, err := runCmdInDir(t, probeDir, nil, 60*time.Second, "go", "run", "-mod=mod", ".", emptyDir, missingDir, txtOnlyDir, lfDir, crlfDir)
		if err != nil || code != 0 {
			t.Fatalf("ComputeCanonicalGameDataHash boundary probe failed (code=%d err=%v): stdout=%q stderr=%q", code, err, stdout, stderr)
		}
	})

	t.Run("T2_F1_06_StaticCgoDisabledBuildSucceeds", func(t *testing.T) {
		requireDirExists(t, cfg.backendDir, "backend")
		stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, map[string]string{"CGO_ENABLED": "0"}, 120*time.Second, "go", "build", "./...")
		if err != nil || code != 0 {
			t.Fatalf("CGO_ENABLED=0 go build ./... failed (code=%d err=%v)\nstdout:\n%s\nstderr:\n%s", code, err, stdout, stderr)
		}
	})
}

// ============================================================================
// Feature #2: Redis Keyspace Boundaries & Policy (5 subtests)
// ============================================================================

func TestTier2_F2_RedisBoundaryAndPolicy(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T2_F2_01_EmptyAndSpecialIDKeyFormatting", func(t *testing.T) {
		rkDir := filepath.Join(cfg.backendDir, "internal", "platform", "rediskeys")
		requireDirExists(t, rkDir, "internal/platform/rediskeys")

		// Probe key builders with empty, UUID, and canonical zone IDs via an isolated module
		stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, nil, 60*time.Second, "go", "test", "-v", "-count=1", "./internal/platform/rediskeys/...")
		if err != nil || code != 0 {
			t.Fatalf("rediskeys tests failed (code=%d err=%v)\nstdout:\n%s\nstderr:\n%s", code, err, stdout, stderr)
		}
		// Ensure keyspace doc exposes deterministic builders without panicking
		docOut, _, docCode, _ := runCmdInDir(t, cfg.backendDir, nil, 30*time.Second, "go", "doc", "./internal/platform/rediskeys")
		if docCode != 0 || strings.TrimSpace(docOut) == "" {
			t.Fatalf("go doc ./internal/platform/rediskeys failed")
		}
	})

	t.Run("T2_F2_02_LiveRedisShortTTLExpiry", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		key := "session:ticket:exp_" + uniqueRunID(t)
		t.Cleanup(func() { _, _, _, _ = redisExec(t, cfg, "DEL", key) })

		if out := redisExecOrFatal(t, cfg, "SETEX", key, "1", `{"ok":true}`); out != "OK" {
			t.Fatalf("SETEX failed: %q", out)
		}
		if ex := redisExecOrFatal(t, cfg, "EXISTS", key); ex != "1" {
			t.Fatalf("expected EXISTS=1 immediately after SETEX, got %q", ex)
		}
		time.Sleep(1500 * time.Millisecond)
		if ex := redisExecOrFatal(t, cfg, "EXISTS", key); ex != "0" {
			t.Fatalf("expected EXISTS=0 after 1s TTL expiry, got %q", ex)
		}
		if val := redisExecOrFatal(t, cfg, "GET", key); val != "" {
			t.Fatalf("expected empty/nil GET after expiry, got %q", val)
		}
	})

	t.Run("T2_F2_03_KeyPatternNamespaceIsolation", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		id := uniqueRunID(t)
		kSess := "session:ticket:" + id
		kLease := "char:lease:" + id
		kZone := "zone:registry:zone.p01_highland_01:" + id
		t.Cleanup(func() {
			_, _, _, _ = redisExec(t, cfg, "DEL", kSess, kLease, kZone)
		})

		redisExecOrFatal(t, cfg, "SET", kSess, "ticket-payload", "EX", "30")
		redisExecOrFatal(t, cfg, "HSET", kLease, "owner", "srv-a")
		redisExecOrFatal(t, cfg, "EXPIRE", kLease, "30")
		redisExecOrFatal(t, cfg, "HSET", kZone, "status", "READY")
		redisExecOrFatal(t, cfg, "EXPIRE", kZone, "30")

		scanKeys := redisScanMatch(t, cfg, "session:ticket:"+id)
		if len(scanKeys) != 1 || scanKeys[0] != kSess {
			t.Fatalf("namespace SCAN mismatch: got %v, want [%s] (must not leak %s or %s)", scanKeys, kSess, kLease, kZone)
		}
		if got := redisExecOrFatal(t, cfg, "GET", kSess); got != "ticket-payload" {
			t.Fatalf("session ticket value corrupted: %q", got)
		}
		if got := redisExecOrFatal(t, cfg, "HGET", kLease, "owner"); got != "srv-a" {
			t.Fatalf("char lease hash corrupted: %q", got)
		}
	})

	t.Run("T2_F2_04_RedisNoEvictionAndPersistencePolicy", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		pol := redisExecOrFatal(t, cfg, "CONFIG", "GET", "maxmemory-policy")
		if !strings.Contains(pol, "noeviction") {
			t.Fatalf("expected maxmemory-policy=noeviction, got %q", pol)
		}
		mm := redisExecOrFatal(t, cfg, "CONFIG", "GET", "maxmemory")
		if !strings.Contains(mm, "536870912") {
			t.Fatalf("expected maxmemory=536870912 (512mb), got %q", mm)
		}
		aof := redisExecOrFatal(t, cfg, "CONFIG", "GET", "appendonly")
		if !strings.Contains(aof, "yes") {
			t.Fatalf("expected appendonly=yes, got %q", aof)
		}
	})

	t.Run("T2_F2_05_MissingKeyAndPersistentKeyTTLBoundaries", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		missingKey := "session:ticket:nonexistent_" + uniqueRunID(t)
		if got := redisExecOrFatal(t, cfg, "GET", missingKey); got != "" {
			t.Fatalf("expected empty GET for missing key, got %q", got)
		}
		if ttl := redisExecOrFatal(t, cfg, "TTL", missingKey); ttl != "-2" {
			t.Fatalf("expected TTL=-2 for missing key, got %q", ttl)
		}
		epochKey := "char:epoch:" + uniqueRunID(t)
		t.Cleanup(func() { _, _, _, _ = redisExec(t, cfg, "DEL", epochKey) })
		redisExecOrFatal(t, cfg, "SET", epochKey, "1")
		if ttl := redisExecOrFatal(t, cfg, "TTL", epochKey); ttl != "-1" {
			t.Fatalf("expected TTL=-1 for persistent key, got %q", ttl)
		}
	})
}

// ============================================================================
// Feature #3: 19-Table PG16 Schema Constraints & Round-Trip (6 subtests)
// ============================================================================

func TestTier2_F3_SchemaConstraintsAndRoundTrip(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T2_F3_01_NegativeWalletInsertRejected23514", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		tag := shortRunTag(t)
		sql := fmt.Sprintf(`
BEGIN;
WITH acc AS (
  INSERT INTO accounts (username, email, password_hash)
  VALUES ('nw_%s', 'nw_%s@test.local', 'h') RETURNING id
), ch AS (
  INSERT INTO characters (account_id, character_name, race_id)
  SELECT id, 'nwc_%s', 'race.zenith_warrior' FROM acc RETURNING id
)
INSERT INTO character_wallets (character_id, standard_coin)
SELECT id, -1 FROM ch;
ROLLBACK;
`, tag, tag, tag)
		assertPsqlRejectedWith(t, cfg, cfg.pgDB, sql, "standard_coin")
	})

	t.Run("T2_F3_02_NegativeWalletUpdateRejected23514", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		tag := shortRunTag(t)
		out := psqlTxRollback(t, cfg, cfg.pgDB, fmt.Sprintf(`
DO $$
DECLARE
  v_acc uuid;
  v_char uuid;
  v_state text;
  v_coin bigint;
BEGIN
  INSERT INTO accounts (username, email, password_hash)
  VALUES ('wu_%s', 'wu_%s@test.local', 'h') RETURNING id INTO v_acc;
  INSERT INTO characters (account_id, character_name, race_id)
  VALUES (v_acc, 'wuc_%s', 'race.zenith_warrior') RETURNING id INTO v_char;
  INSERT INTO character_wallets (character_id, standard_coin)
  VALUES (v_char, 100);

  BEGIN
    UPDATE character_wallets SET standard_coin = standard_coin - 150 WHERE character_id = v_char;
    RAISE EXCEPTION 'UNEXPECTED_SUCCESS';
  EXCEPTION WHEN check_violation THEN
    GET STACKED DIAGNOSTICS v_state = RETURNED_SQLSTATE;
    IF v_state <> '23514' THEN
      RAISE EXCEPTION 'WRONG_SQLSTATE: %%', v_state;
    END IF;
  END;

  SELECT standard_coin INTO v_coin FROM character_wallets WHERE character_id = v_char;
  IF v_coin <> 100 THEN
    RAISE EXCEPTION 'BALANCE_CHANGED: %%', v_coin;
  END IF;
  RAISE NOTICE 'OK_23514_BAL_100';
END $$;
`, tag, tag, tag))
		_ = out
	})

	t.Run("T2_F3_03_NegativeFencingEpochRejected23514", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		tag := shortRunTag(t)
		sql := fmt.Sprintf(`
BEGIN;
WITH acc AS (
  INSERT INTO accounts (username, email, password_hash)
  VALUES ('fe_%s', 'fe_%s@test.local', 'h') RETURNING id
)
INSERT INTO characters (account_id, character_name, race_id, fencing_epoch)
SELECT id, 'fec_%s', 'race.zenith_warrior', -1 FROM acc;
ROLLBACK;
`, tag, tag, tag)
		assertPsqlRejectedWith(t, cfg, cfg.pgDB, sql, "fencing_epoch")
	})

	t.Run("T2_F3_04_CurrencyLedgerUpdateAndDeleteBlocked", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		tag := shortRunTag(t)
		psqlTxRollback(t, cfg, cfg.pgDB, fmt.Sprintf(`
DO $$
DECLARE
  v_acc uuid;
  v_char uuid;
  v_state text;
  v_msg text;
BEGIN
  INSERT INTO accounts (username, email, password_hash)
  VALUES ('im_%s', 'im_%s@test.local', 'h') RETURNING id INTO v_acc;
  INSERT INTO characters (account_id, character_name, race_id)
  VALUES (v_acc, 'imc_%s', 'race.zenith_warrior') RETURNING id INTO v_char;
  INSERT INTO currency_ledger (
    idempotency_key, character_id, currency_type, delta_amount, balance_after,
    reason_code, reference_type, reference_id, fencing_epoch, server_id
  ) VALUES ('idem:%s', v_char, 'currency.standard_coin', 100, 100, 'QUEST', 'Q1', 'ref1', 1, 'srv1');

  BEGIN
    UPDATE currency_ledger SET delta_amount = 999 WHERE idempotency_key = 'idem:%s';
    RAISE EXCEPTION 'UPDATE_SHOULD_FAIL';
  EXCEPTION WHEN raise_exception THEN
    GET STACKED DIAGNOSTICS v_state = RETURNED_SQLSTATE, v_msg = MESSAGE_TEXT;
    IF v_state <> 'P0001' OR position('IMMUTABLE_LEDGER_VIOLATION' in v_msg) = 0 THEN
      RAISE EXCEPTION 'UNEXPECTED_UPDATE_ERR: %% %%', v_state, v_msg;
    END IF;
  END;

  BEGIN
    DELETE FROM currency_ledger WHERE idempotency_key = 'idem:%s';
    RAISE EXCEPTION 'DELETE_SHOULD_FAIL';
  EXCEPTION WHEN raise_exception THEN
    GET STACKED DIAGNOSTICS v_state = RETURNED_SQLSTATE, v_msg = MESSAGE_TEXT;
    IF v_state <> 'P0001' OR position('IMMUTABLE_LEDGER_VIOLATION' in v_msg) = 0 THEN
      RAISE EXCEPTION 'UNEXPECTED_DELETE_ERR: %% %%', v_state, v_msg;
    END IF;
  END;
END $$;
`, tag, tag, tag, tag, tag, tag))
	})

	t.Run("T2_F3_05_SelfTradeAndEquipConsistencyRejected23514", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		tag := shortRunTag(t)
		selfTradeSQL := fmt.Sprintf(`
BEGIN;
WITH acc AS (
  INSERT INTO accounts (username, email, password_hash)
  VALUES ('st_%s', 'st_%s@test.local', 'h') RETURNING id
), ch AS (
  INSERT INTO characters (account_id, character_name, race_id)
  SELECT id, 'stc_%s', 'race.zenith_warrior' FROM acc RETURNING id
)
INSERT INTO trade_transactions (
  idempotency_key, initiator_char_id, recipient_char_id, trade_state,
  zone_id, initiator_epoch, recipient_epoch
)
SELECT 'idem:self:%s', id, id, 'SETTLED', 'zone.p01_highland_01', 1, 1 FROM ch;
ROLLBACK;
`, tag, tag, tag, tag)
		assertPsqlRejectedWith(t, cfg, cfg.pgDB, selfTradeSQL, "chk_no_self_trade")

		equipSQL := fmt.Sprintf(`
BEGIN;
INSERT INTO item_definitions (item_def_id, item_category, equip_slot, base_rarity, max_stack, content_hash)
VALUES ('item.bad_eq_%s', 'EQUIPMENT', NULL, 'rarity.common', 1, 'abc');
ROLLBACK;
`, tag)
		assertPsqlRejectedWith(t, cfg, cfg.pgDB, equipSQL, "chk_equip_slot_consistency")
	})

	t.Run("T2_F3_06_MigrationUpDownUpRoundTripZeroFKErrors", func(t *testing.T) {
		requireFileExists(t, cfg.composeFile, "docker-compose.local.yml")
		skipIfDockerDisabled(t, cfg)

		// Run round-trip in an isolated ephemeral database so live services stay healthy
		dbName := withTempPGDatabase(t, cfg, false)
		dsn := cfg.dockerPGDSNForDB(dbName)

		runMigrate := func(args ...string) {
			cmdArgs := append([]string{"compose", "-f", cfg.composeFile, "run", "--rm", "--no-deps", "migrate", "-path=/migrations", "-database=" + dsn}, args...)
			stdout, stderr, code, err := runCmd(t, 60*time.Second, "docker", cmdArgs...)
			if err != nil || code != 0 {
				t.Fatalf("migrate %v failed (code=%d err=%v)\nstdout:%s\nstderr:%s", args, code, err, stdout, stderr)
			}
		}

		runMigrate("up")
		cntUp1 := psqlExecOrFatal(t, cfg, dbName, `SELECT count(*) FROM information_schema.tables WHERE table_schema='public' AND table_type='BASE TABLE' AND table_name <> 'schema_migrations';`)
		if cntUp1 != "19" {
			t.Fatalf("after initial up expected 19 tables, got %s", cntUp1)
		}

		runMigrate("down", "-all")
		cntDown := psqlExecOrFatal(t, cfg, dbName, `SELECT count(*) FROM information_schema.tables WHERE table_schema='public' AND table_type='BASE TABLE' AND table_name <> 'schema_migrations';`)
		if cntDown != "0" {
			t.Fatalf("after down -all expected 0 domain tables, got %s", cntDown)
		}
		fnDown := psqlExecOrFatal(t, cfg, dbName, `SELECT count(*) FROM pg_proc WHERE proname IN ('fn_prevent_ledger_mutation', 'fn_touch_updated_at');`)
		if fnDown != "0" {
			t.Fatalf("after down -all expected 0 migration functions, got %s", fnDown)
		}

		runMigrate("up")
		cntUp2 := psqlExecOrFatal(t, cfg, dbName, `SELECT count(*) FROM information_schema.tables WHERE table_schema='public' AND table_type='BASE TABLE' AND table_name <> 'schema_migrations';`)
		if cntUp2 != "19" {
			t.Fatalf("after second up expected 19 tables, got %s", cntUp2)
		}
	})
}

// ============================================================================
// Feature #4: Platform DB Boundaries (5 subtests)
// ============================================================================

func TestTier2_F4_PlatformDBBoundary(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T2_F4_01_InvalidPGDSNFailsCleanly", func(t *testing.T) {
		requireDirExists(t, filepath.Join(cfg.backendDir, "internal", "platform", "postgres"), "internal/platform/postgres")
		env := map[string]string{
			"IU_TEST_PG_DSN": "postgres://bad:bad@127.0.0.1:59999/nope?sslmode=disable&connect_timeout=2",
		}
		stdout, stderr, code, _ := runCmdInDir(t, cfg.backendDir, env, 15*time.Second, "go", "test", "-count=1", "./internal/platform/postgres/...")
		if code == 0 {
			t.Fatalf("expected non-zero exit when IU_TEST_PG_DSN points to closed port 59999, got 0\nstdout:%s\nstderr:%s", stdout, stderr)
		}
		if strings.Contains(stdout+stderr, "panic:") {
			t.Fatalf("invalid DSN caused Go panic:\nstdout:%s\nstderr:%s", stdout, stderr)
		}
	})

	t.Run("T2_F4_02_TxRollbackLeavesZeroPartialRows", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		tag := shortRunTag(t)
		uname := "rb_" + tag
		cname := "rbc_" + tag
		sql := fmt.Sprintf(`
BEGIN;
WITH acc AS (
  INSERT INTO accounts (username, email, password_hash)
  VALUES ('%s', '%s@test.local', 'h') RETURNING id
), ch AS (
  INSERT INTO characters (account_id, character_name, race_id)
  SELECT id, '%s', 'race.zenith_warrior' FROM acc RETURNING id
)
INSERT INTO character_wallets (character_id, standard_coin)
SELECT id, -10 FROM ch;
COMMIT;
`, uname, uname, cname)
		stdout, stderr, code, _ := psqlExec(t, cfg, cfg.pgDB, sql)
		combined := stdout + "\n" + stderr
		if code == 0 {
			t.Fatalf("expected transaction with -10 coin to fail")
		}
		if strings.Contains(combined, "accounts_username_check") || strings.Contains(combined, "characters_character_name_check") {
			t.Fatalf("transaction failed on unexpected identifier length check instead of wallet coin check:\n%s", combined)
		}
		if !strings.Contains(combined, "character_wallets") && !strings.Contains(combined, "standard_coin") && !strings.Contains(combined, "23514") {
			t.Fatalf("expected failure citing character_wallets / standard_coin / 23514, got:\n%s", combined)
		}
		accCnt := psqlExecOrFatal(t, cfg, cfg.pgDB, fmt.Sprintf(`SELECT count(*) FROM accounts WHERE username = '%s';`, uname))
		charCnt := psqlExecOrFatal(t, cfg, cfg.pgDB, fmt.Sprintf(`SELECT count(*) FROM characters WHERE character_name = '%s';`, cname))
		if accCnt != "0" || charCnt != "0" {
			t.Fatalf("rollback leaked partial rows: accounts=%s characters=%s", accCnt, charCnt)
		}
	})

	t.Run("T2_F4_03_DuplicateLedgerIdempotencyKeyRejected23505", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		tag := shortRunTag(t)
		psqlTxRollback(t, cfg, cfg.pgDB, fmt.Sprintf(`
DO $$
DECLARE
  v_acc uuid;
  v_char uuid;
  v_state text;
BEGIN
  INSERT INTO accounts (username, email, password_hash)
  VALUES ('dup_%s', 'dup_%s@test.local', 'h') RETURNING id INTO v_acc;
  INSERT INTO characters (account_id, character_name, race_id)
  VALUES (v_acc, 'dupc_%s', 'race.zenith_warrior') RETURNING id INTO v_char;

  INSERT INTO currency_ledger (
    idempotency_key, character_id, currency_type, delta_amount, balance_after,
    reason_code, reference_type, reference_id, fencing_epoch, server_id
  ) VALUES ('idem:dup:%s', v_char, 'currency.standard_coin', 50, 50, 'MOB_DROP', 'KILL', 'k1', 1, 's1');

  BEGIN
    INSERT INTO currency_ledger (
      idempotency_key, character_id, currency_type, delta_amount, balance_after,
      reason_code, reference_type, reference_id, fencing_epoch, server_id
    ) VALUES ('idem:dup:%s', v_char, 'currency.standard_coin', 50, 100, 'MOB_DROP', 'KILL', 'k1', 1, 's1');
    RAISE EXCEPTION 'DUPLICATE_SHOULD_FAIL';
  EXCEPTION WHEN unique_violation THEN
    GET STACKED DIAGNOSTICS v_state = RETURNED_SQLSTATE;
    IF v_state <> '23505' THEN
      RAISE EXCEPTION 'WRONG_SQLSTATE: %%', v_state;
    END IF;
  END;
END $$;
`, tag, tag, tag, tag, tag))
	})

	t.Run("T2_F4_04_CaseInsensitiveCitextUniquenessEnforced23505", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		tag := shortRunTag(t)
		psqlTxRollback(t, cfg, cfg.pgDB, fmt.Sprintf(`
DO $$
DECLARE
  v_acc uuid;
  v_state text;
BEGIN
  INSERT INTO accounts (username, email, password_hash)
  VALUES ('CaseHero_%s', 'Case_%s@Test.Local', 'h') RETURNING id INTO v_acc;

  BEGIN
    INSERT INTO accounts (username, email, password_hash)
    VALUES ('casehero_%s', 'other_%s@test.local', 'h');
    RAISE EXCEPTION 'CITEXT_USERNAME_DUP_SHOULD_FAIL';
  EXCEPTION WHEN unique_violation THEN
    GET STACKED DIAGNOSTICS v_state = RETURNED_SQLSTATE;
    IF v_state <> '23505' THEN RAISE EXCEPTION 'BAD_STATE: %%', v_state; END IF;
  END;

  INSERT INTO characters (account_id, character_name, race_id)
  VALUES (v_acc, 'ZenithOne_%s', 'race.zenith_warrior');

  BEGIN
    INSERT INTO characters (account_id, character_name, race_id)
    VALUES (v_acc, 'zenithone_%s', 'race.zenith_warrior');
    RAISE EXCEPTION 'CITEXT_CHARNAME_DUP_SHOULD_FAIL';
  EXCEPTION WHEN unique_violation THEN
    GET STACKED DIAGNOSTICS v_state = RETURNED_SQLSTATE;
    IF v_state <> '23505' THEN RAISE EXCEPTION 'BAD_STATE: %%', v_state; END IF;
  END;
END $$;
`, tag, tag, tag, tag, tag, tag))
	})

	t.Run("T2_F4_05_GoTestPlatformWithRaceDetector", func(t *testing.T) {
		requireDirExists(t, filepath.Join(cfg.backendDir, "internal", "platform"), "internal/platform")
		skipIfDockerDisabled(t, cfg)
		baseEnv := map[string]string{
			"IU_TEST_PG_DSN":     cfg.hostPGDSN(),
			"IU_TEST_REDIS_ADDR": cfg.redisAddr,
		}
		if _, gccErr := exec.LookPath("gcc"); gccErr != nil {
			stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, baseEnv, 180*time.Second, "go", "test", "-count=1", "./internal/platform/...")
			if err != nil || code != 0 {
				t.Fatalf("non-race go test ./internal/platform/... failed before -race skip (code=%d err=%v)\nstdout:\n%s\nstderr:\n%s", code, err, stdout, stderr)
			}
			t.Skip("gcc not found in PATH on Windows host; non-race ./internal/platform/... passed, skipping -race detector check")
		}
		raceEnv := map[string]string{
			"IU_TEST_PG_DSN":     cfg.hostPGDSN(),
			"IU_TEST_REDIS_ADDR": cfg.redisAddr,
			"CGO_ENABLED":        "1",
		}
		stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, raceEnv, 180*time.Second, "go", "test", "-race", "-count=1", "./internal/platform/...")
		if err != nil || code != 0 {
			t.Fatalf("go test -race ./internal/platform/... failed (code=%d err=%v)\nstdout:\n%s\nstderr:\n%s", code, err, stdout, stderr)
		}
		if strings.Contains(stdout+stderr, "DATA RACE") {
			t.Fatalf("data race detected:\n%s\n%s", stdout, stderr)
		}
	})
}

// ============================================================================
// Feature #5: JSON Schema Boundary Cases (5 subtests)
// ============================================================================

func TestTier2_F5_SchemaBoundaryCases(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T2_F5_01_UnknownFieldRejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		_, tmpSources, tmpSchemas, tmpOut := copyGamedataToTemp(t, cfg)
		skillsPath := filepath.Join(tmpSources, "core", "skills.json")
		mutateRecordInJSONFile(t, skillsPath, "skill.beam_charged_01", func(obj map[string]any) error {
			obj["unexpected_extra_field"] = "bogus"
			return nil
		})
		stdout, stderr, code := runDataCompiler(t, bin, "--validate", "--src", tmpSources, "--schemas", tmpSchemas, "--out", tmpOut)
		combined := stdout + "\n" + stderr
		if code == 0 {
			t.Fatalf("expected non-zero exit when unknown field added to skills.json")
		}
		if !strings.Contains(combined, "skills.json") || (!strings.Contains(combined, "unexpected_extra_field") && !strings.Contains(strings.ToLower(combined), "additional")) {
			t.Fatalf("expected error citing skills.json & unexpected_extra_field/additionalProperties, got:\n%s", combined)
		}
	})

	t.Run("T2_F5_02_InvalidCanonicalIDRegexRejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		_, tmpSources, tmpSchemas, tmpOut := copyGamedataToTemp(t, cfg)
		skillsPath := filepath.Join(tmpSources, "core", "skills.json")
		mutateRecordInJSONFile(t, skillsPath, "skill.beam_charged_01", func(obj map[string]any) error {
			obj["skill_id"] = "Skill.Bad"
			return nil
		})
		stdout, stderr, code := runDataCompiler(t, bin, "--validate", "--src", tmpSources, "--schemas", tmpSchemas, "--out", tmpOut)
		combined := stdout + "\n" + stderr
		if code == 0 {
			t.Fatalf("expected non-zero exit for invalid skill_id 'Skill.Bad'")
		}
		if !strings.Contains(combined, "skills.json") || (!strings.Contains(combined, "Skill.Bad") && !strings.Contains(combined, "skill_id")) {
			t.Fatalf("expected error citing skills.json & Skill.Bad/skill_id, got:\n%s", combined)
		}
	})

	t.Run("T2_F5_03_WrongEnumValueRejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		_, tmpSources, tmpSchemas, tmpOut := copyGamedataToTemp(t, cfg)
		itemsPath := filepath.Join(tmpSources, "core", "items.json")
		mutateRecordInJSONFile(t, itemsPath, "item.body_recruit_tunic_01", func(obj map[string]any) error {
			obj["rarity"] = "rarity.omega_invalid"
			return nil
		})
		stdout, stderr, code := runDataCompiler(t, bin, "--validate", "--src", tmpSources, "--schemas", tmpSchemas, "--out", tmpOut)
		combined := stdout + "\n" + stderr
		if code == 0 {
			t.Fatalf("expected non-zero exit for invalid rarity enum")
		}
		if !strings.Contains(combined, "items.json") || (!strings.Contains(combined, "rarity.omega_invalid") && !strings.Contains(combined, "rarity")) {
			t.Fatalf("expected error citing items.json & rarity, got:\n%s", combined)
		}
	})

	t.Run("T2_F5_04_NumericBoundsRejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)

		// Sub-case A: crit_rate = 1.01 (> 1.0)
		_, srcA, schA, outA := copyGamedataToTemp(t, cfg)
		mutateRecordInJSONFile(t, filepath.Join(srcA, "core", "races.json"), "race.zenith_warrior", func(obj map[string]any) error {
			stats, ok := obj["base_stats_lv1"].(map[string]any)
			if !ok {
				return fmt.Errorf("missing base_stats_lv1")
			}
			stats["crit_rate"] = 1.01
			return nil
		})
		stA, seA, codeA := runDataCompiler(t, bin, "--validate", "--src", srcA, "--schemas", schA, "--out", outA)
		if codeA == 0 || !strings.Contains(stA+seA, "races.json") {
			t.Fatalf("expected crit_rate=1.01 rejection in races.json, got code=%d out=%s err=%s", codeA, stA, seA)
		}

		// Sub-case B: unlock_level = 0 (< 1)
		_, srcB, schB, outB := copyGamedataToTemp(t, cfg)
		mutateRecordInJSONFile(t, filepath.Join(srcB, "core", "skills.json"), "skill.beam_charged_01", func(obj map[string]any) error {
			obj["unlock_level"] = 0
			return nil
		})
		stB, seB, codeB := runDataCompiler(t, bin, "--validate", "--src", srcB, "--schemas", schB, "--out", outB)
		if codeB == 0 || !strings.Contains(stB+seB, "skills.json") {
			t.Fatalf("expected unlock_level=0 rejection in skills.json, got code=%d out=%s err=%s", codeB, stB, seB)
		}

		// Sub-case C: valid boundary crit_rate = 1.0 and unlock_level = 1 pass
		_, srcC, schC, outC := copyGamedataToTemp(t, cfg)
		mutateRecordInJSONFile(t, filepath.Join(srcC, "core", "races.json"), "race.zenith_warrior", func(obj map[string]any) error {
			obj["base_stats_lv1"].(map[string]any)["crit_rate"] = 1.0
			return nil
		})
		mutateRecordInJSONFile(t, filepath.Join(srcC, "core", "skills.json"), "skill.beam_charged_01", func(obj map[string]any) error {
			obj["unlock_level"] = 1
			return nil
		})
		stC, seC, codeC := runDataCompiler(t, bin, "--validate", "--src", srcC, "--schemas", schC, "--out", outC)
		if codeC != 0 {
			t.Fatalf("expected valid boundary crit_rate=1.0 & unlock_level=1 to pass, got code=%d out=%s err=%s", codeC, stC, seC)
		}
	})

	t.Run("T2_F5_05_MalformedJSONSyntaxRejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		_, tmpSources, tmpSchemas, tmpOut := copyGamedataToTemp(t, cfg)
		affixPath := filepath.Join(tmpSources, "core", "affix_pools.json")
		if err := os.WriteFile(affixPath, []byte(`{"broken": [1, 2, `), 0o644); err != nil {
			t.Fatal(err)
		}
		stdout, stderr, code := runDataCompiler(t, bin, "--validate", "--src", tmpSources, "--schemas", tmpSchemas, "--out", tmpOut)
		combined := stdout + "\n" + stderr
		if code == 0 {
			t.Fatalf("expected non-zero exit for malformed JSON syntax")
		}
		if strings.Contains(combined, "panic:") {
			t.Fatalf("malformed JSON caused panic:\n%s", combined)
		}
		if !strings.Contains(combined, "affix_pools.json") {
			t.Fatalf("expected error citing affix_pools.json, got:\n%s", combined)
		}
	})
}

// ============================================================================
// Feature #6: Core Gamedata Boundary Cases (6 subtests)
// ============================================================================

func TestTier2_F6_GamedataBoundaryCases(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T2_F6_01_NothingLootAcceptedVsItemMinQtyZeroRejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)

		// Sub-case A: baseline with NOTHING entry passes
		_, srcA, schA, outA := copyGamedataToTemp(t, cfg)
		_, seA, codeA := runDataCompiler(t, bin, "--validate", "--src", srcA, "--schemas", schA, "--out", outA)
		if codeA != 0 {
			t.Fatalf("baseline NOTHING loot entry should validate cleanly, got code=%d err=%s", codeA, seA)
		}

		// Sub-case B: ITEM entry with min_qty=0 fails
		_, srcB, schB, outB := copyGamedataToTemp(t, cfg)
		mutateRecordInJSONFile(t, filepath.Join(srcB, "core", "loot_tables.json"), "loot.mob_highland_wolf", func(obj map[string]any) error {
			groups, _ := obj["roll_groups"].([]any)
			for _, gRaw := range groups {
				g, _ := gRaw.(map[string]any)
				entries, _ := g["entries"].([]any)
				for _, eRaw := range entries {
					e, _ := eRaw.(map[string]any)
					if e["entry_type"] == "ITEM" {
						e["min_qty"] = 0
						e["max_qty"] = 1
						return nil
					}
				}
			}
			return fmt.Errorf("no ITEM entry found in loot.mob_highland_wolf")
		})
		stB, seB, codeB := runDataCompiler(t, bin, "--validate", "--src", srcB, "--schemas", schB, "--out", outB)
		combined := stB + "\n" + seB
		if codeB == 0 {
			t.Fatalf("expected ITEM entry with min_qty=0 to be rejected")
		}
		if !strings.Contains(combined, "loot_tables.json") {
			t.Fatalf("expected error citing loot_tables.json, got:\n%s", combined)
		}
	})

	t.Run("T2_F6_02_AffixMinValueGreaterThanMaxValueRejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		_, tmpSources, tmpSchemas, tmpOut := copyGamedataToTemp(t, cfg)
		mutateRecordInJSONFile(t, filepath.Join(tmpSources, "core", "affix_pools.json"), "affix.crit_rate", func(obj map[string]any) error {
			tiers, ok := obj["tiers"].([]any)
			if !ok || len(tiers) == 0 {
				return fmt.Errorf("missing tiers in affix.crit_rate")
			}
			t0, _ := tiers[0].(map[string]any)
			t0["min_value"] = 0.05
			t0["max_value"] = 0.02
			return nil
		})
		stdout, stderr, code := runDataCompiler(t, bin, "--validate", "--src", tmpSources, "--schemas", tmpSchemas, "--out", tmpOut)
		combined := stdout + "\n" + stderr
		if code == 0 {
			t.Fatalf("expected min_value > max_value in affix.crit_rate to be rejected")
		}
		if !strings.Contains(combined, "affix_pools.json") {
			t.Fatalf("expected error citing affix_pools.json, got:\n%s", combined)
		}
	})

	t.Run("T2_F6_03_SubTableCycleAndDepthOver3Rejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)

		// Sub-case A: self-cycle in loot.mob_highland_wolf
		_, srcA, schA, outA := copyGamedataToTemp(t, cfg)
		mutateRecordInJSONFile(t, filepath.Join(srcA, "core", "loot_tables.json"), "loot.mob_highland_wolf", func(obj map[string]any) error {
			groups := obj["roll_groups"].([]any)
			g0 := groups[0].(map[string]any)
			entries := g0["entries"].([]any)
			entries = append(entries, map[string]any{
				"entry_type": "SUB_TABLE",
				"ref_id":     "loot.mob_highland_wolf",
				"weight":     100,
				"min_qty":    1,
				"max_qty":    1,
			})
			g0["entries"] = entries
			return nil
		})
		stA, seA, codeA := runDataCompiler(t, bin, "--validate", "--src", srcA, "--schemas", schA, "--out", outA)
		if codeA == 0 || !strings.Contains(stA+seA, "loot_tables.json") {
			t.Fatalf("expected loot sub-table cycle to be rejected citing loot_tables.json, got code=%d out=%s err=%s", codeA, stA, seA)
		}

		// Sub-case B: depth 4 > 3 chain
		_, srcB, schB, outB := copyGamedataToTemp(t, cfg)
		lootPathB := filepath.Join(srcB, "core", "loot_tables.json")
		appendLootChainTables(t, lootPathB)
		injectLootChainPackMappings(t, srcB, []string{
			"loot.sub_d0", "loot.sub_d1", "loot.sub_d2", "loot.sub_d3", "loot.sub_d4",
		})
		stB, seB, codeB := runDataCompiler(t, bin, "--validate", "--src", srcB, "--schemas", schB, "--out", outB)
		if codeB == 0 || !strings.Contains(stB+seB, "loot_tables.json") {
			t.Fatalf("expected loot sub-table depth > 3 to be rejected citing loot_tables.json, got code=%d out=%s err=%s", codeB, stB, seB)
		}
	})

	t.Run("T2_F6_04_NonEquipNullSpecAcceptedVsEquipNullSpecRejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)

		// Sub-case A: baseline passes with non-equipment equip_spec: null
		_, srcA, schA, outA := copyGamedataToTemp(t, cfg)
		_, seA, codeA := runDataCompiler(t, bin, "--validate", "--src", srcA, "--schemas", schA, "--out", outA)
		if codeA != 0 {
			t.Fatalf("baseline items with non-equipment null equip_spec should pass, got code=%d err=%s", codeA, seA)
		}

		// Sub-case B: EQUIPMENT item with equip_spec: null fails
		_, srcB, schB, outB := copyGamedataToTemp(t, cfg)
		mutateRecordInJSONFile(t, filepath.Join(srcB, "core", "items.json"), "item.body_recruit_tunic_01", func(obj map[string]any) error {
			obj["equip_spec"] = nil
			return nil
		})
		stB, seB, codeB := runDataCompiler(t, bin, "--validate", "--src", srcB, "--schemas", schB, "--out", outB)
		if codeB == 0 || !strings.Contains(stB+seB, "items.json") {
			t.Fatalf("expected EQUIPMENT item with null equip_spec to fail citing items.json, got code=%d out=%s err=%s", codeB, stB, seB)
		}
	})

	t.Run("T2_F6_05_ApprovedReadmeTokensNotFalsePositived", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		coreDir := filepath.Join(cfg.gamedataDir, "sources", "core")
		requireDirExists(t, coreDir, "gamedata/sources/core")

		// Confirm canonical whitelisted tokens are present in core/
		skillsRaw, _ := os.ReadFile(filepath.Join(coreDir, "skills.json"))
		monstersRaw, _ := os.ReadFile(filepath.Join(coreDir, "monsters.json"))
		itemsRaw, _ := os.ReadFile(filepath.Join(coreDir, "items.json"))
		affixRaw, _ := os.ReadFile(filepath.Join(coreDir, "affix_pools.json"))
		allCore := string(skillsRaw) + string(monstersRaw) + string(itemsRaw) + string(affixRaw)
		for _, tok := range []string{"skill.ki_volley_01", "mob.raider_scouter_01", "item.acc2_scouter_charm_10", "ki_dmg_pct"} {
			if !strings.Contains(allCore, tok) {
				t.Fatalf("core gamedata missing expected whitelisted canonical token %q", tok)
			}
		}

		stdout, stderr, code := runDataCompiler(t, bin,
			"--validate",
			"--src", filepath.Join(cfg.gamedataDir, "sources"),
			"--schemas", filepath.Join(cfg.gamedataDir, "schemas"),
		)
		if code != 0 {
			t.Fatalf("approved README.md tokens caused false positive failure (code=%d)\nstdout:%s\nstderr:%s", code, stdout, stderr)
		}
	})

	t.Run("T2_F6_06_MissingCrossReferenceRejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		_, tmpSources, tmpSchemas, tmpOut := copyGamedataToTemp(t, cfg)
		mutateRecordInJSONFile(t, filepath.Join(tmpSources, "core", "monsters.json"), "mob.highland_wolf_01", func(obj map[string]any) error {
			if rew, ok := obj["rewards"].(map[string]any); ok {
				rew["loot_table_id"] = "loot.does_not_exist_99"
				return nil
			}
			obj["loot_table_id"] = "loot.does_not_exist_99"
			return nil
		})
		stdout, stderr, code := runDataCompiler(t, bin, "--validate", "--src", tmpSources, "--schemas", tmpSchemas, "--out", tmpOut)
		combined := stdout + "\n" + stderr
		if code == 0 {
			t.Fatalf("expected missing cross-reference loot.does_not_exist_99 to be rejected")
		}
		if !strings.Contains(combined, "monsters.json") || !strings.Contains(combined, "loot.does_not_exist_99") {
			t.Fatalf("expected error citing monsters.json & loot.does_not_exist_99, got:\n%s", combined)
		}
	})
}

// ============================================================================
// Feature #7: Presentation Pack Boundary Cases (5 subtests)
// ============================================================================

func TestTier2_F7_PresentationPackBoundaryCases(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T2_F7_01_DBReferenceAllowsBrandDisplayStringsOnly", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)

		// Sub-case A: baseline Pack_DB_Reference passes
		_, srcA, schA, outA := copyGamedataToTemp(t, cfg)
		stA, seA, codeA := runDataCompiler(t, bin, "--validate", "--ip-pack", "Pack_DB_Reference", "--src", srcA, "--schemas", schA, "--out", outA)
		if codeA != 0 {
			t.Fatalf("Pack_DB_Reference should allow brand terms in display text, got code=%d out=%s err=%s", codeA, stA, seA)
		}

		// Sub-case B: brand term as canonical key in Pack_DB_Reference/strings_en.json fails
		_, srcB, schB, outB := copyGamedataToTemp(t, cfg)
		enPath := filepath.Join(srcB, "packs", "Pack_DB_Reference", "strings_en.json")
		injectCanonicalEntryInPackFile(t, enPath, "skill.kamehameha", map[string]any{
			"canonical_id": "skill.kamehameha",
			"display_name": "Kamehameha",
		})
		stB, seB, codeB := runDataCompiler(t, bin, "--validate", "--ip-pack", "Pack_DB_Reference", "--src", srcB, "--schemas", schB, "--out", outB)
		if codeB == 0 {
			t.Fatalf("expected brand canonical ID key 'skill.kamehameha' in Pack_DB_Reference to be rejected, got 0 (out=%s err=%s)", stB, seB)
		}
	})

	t.Run("T2_F7_02_OriginalCommercialBrandTermsRejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		for _, brand := range []string{"Saiyan", "Goku", "Zeni", "Kamehameha", "Dragon Ball"} {
			_, tmpSources, tmpSchemas, tmpOut := copyGamedataToTemp(t, cfg)
			enPath := filepath.Join(tmpSources, "packs", "Pack_Original_Commercial", "strings_en.json")
			mutateRecordInJSONFile(t, enPath, "skill.beam_charged_01", func(obj map[string]any) error {
				obj["display_name"] = "Test " + brand + " Wave"
				return nil
			})
			stdout, stderr, code := runDataCompiler(t, bin, "--validate", "--ip-pack", "Pack_Original_Commercial", "--src", tmpSources, "--schemas", tmpSchemas, "--out", tmpOut)
			combined := stdout + "\n" + stderr
			if code == 0 {
				t.Fatalf("Pack_Original_Commercial accepted forbidden brand term %q", brand)
			}
			if !strings.Contains(combined, "strings_en.json") || !strings.Contains(combined, "skill.beam_charged_01") {
				t.Fatalf("error for brand %q should cite strings_en.json & skill.beam_charged_01, got:\n%s", brand, combined)
			}
		}
	})

	t.Run("T2_F7_03_MissingCanonicalIDInStringsViRejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		_, tmpSources, tmpSchemas, tmpOut := copyGamedataToTemp(t, cfg)
		viPath := filepath.Join(tmpSources, "packs", "Pack_Original_Commercial", "strings_vi.json")
		deleteRecordInJSONFile(t, viPath, "skill.beam_charged_01")
		stdout, stderr, code := runDataCompiler(t, bin, "--validate", "--ip-pack", "Pack_Original_Commercial", "--src", tmpSources, "--schemas", tmpSchemas, "--out", tmpOut)
		combined := stdout + "\n" + stderr
		if code == 0 {
			t.Fatalf("expected missing skill.beam_charged_01 in strings_vi.json to fail")
		}
		if !strings.Contains(combined, "strings_vi.json") || !strings.Contains(combined, "skill.beam_charged_01") {
			t.Fatalf("expected error citing strings_vi.json & skill.beam_charged_01, got:\n%s", combined)
		}
	})

	t.Run("T2_F7_04_MissingCanonicalIDInAssetBindingsRejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		_, tmpSources, tmpSchemas, tmpOut := copyGamedataToTemp(t, cfg)
		abPath := filepath.Join(tmpSources, "packs", "Pack_Original_Commercial", "asset_bindings.json")
		deleteRecordInJSONFile(t, abPath, "boss.highland_titan_01")
		stdout, stderr, code := runDataCompiler(t, bin, "--validate", "--ip-pack", "Pack_Original_Commercial", "--src", tmpSources, "--schemas", tmpSchemas, "--out", tmpOut)
		combined := stdout + "\n" + stderr
		if code == 0 {
			t.Fatalf("expected missing boss.highland_titan_01 in asset_bindings.json to fail")
		}
		if !strings.Contains(combined, "asset_bindings.json") || !strings.Contains(combined, "boss.highland_titan_01") {
			t.Fatalf("expected error citing asset_bindings.json & boss.highland_titan_01, got:\n%s", combined)
		}
	})

	t.Run("T2_F7_05_GamedataHashIdenticalAcrossPacks", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		outA := filepath.Join(t.TempDir(), "packA")
		outB := filepath.Join(t.TempDir(), "packB")
		for _, tc := range []struct{ pack, out string }{
			{"Pack_DB_Reference", outA},
			{"Pack_Original_Commercial", outB},
		} {
			_, se, code := runDataCompiler(t, bin,
				"--validate", "--export",
				"--ip-pack", tc.pack,
				"--src", filepath.Join(cfg.gamedataDir, "sources"),
				"--schemas", filepath.Join(cfg.gamedataDir, "schemas"),
				"--out", tc.out,
			)
			if code != 0 {
				t.Fatalf("export %s failed: %s", tc.pack, se)
			}
		}
		shaA := strings.TrimSpace(string(mustReadFileBytes(t, filepath.Join(outA, "backend", "gamedata_bundle.sha256"))))
		shaB := strings.TrimSpace(string(mustReadFileBytes(t, filepath.Join(outB, "backend", "gamedata_bundle.sha256"))))
		if shaA != shaB || len(shaA) < 64 {
			t.Fatalf("gamedata_hash must be identical across packs: A=%q B=%q", shaA, shaB)
		}
		presA := string(mustReadFileBytes(t, filepath.Join(outA, "unreal", "DT_PresentationPack.json")))
		presB := string(mustReadFileBytes(t, filepath.Join(outB, "unreal", "DT_PresentationPack.json")))
		if presA == presB {
			t.Fatalf("expected DT_PresentationPack.json to differ between packs")
		}
	})
}

// ============================================================================
// Feature #8: Datacompiler Boundary & CLI Cases (6 subtests)
// ============================================================================

func TestTier2_F8_DatacompilerBoundaryAndCLICases(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T2_F8_01_BrandIDInCoreSkillsRejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		_, tmpSources, tmpSchemas, tmpOut := copyGamedataToTemp(t, cfg)
		mutateRecordInJSONFile(t, filepath.Join(tmpSources, "core", "skills.json"), "skill.beam_charged_01", func(obj map[string]any) error {
			obj["skill_id"] = "skill.kamehameha"
			return nil
		})
		stdout, stderr, code := runDataCompiler(t, bin, "--validate", "--src", tmpSources, "--schemas", tmpSchemas, "--out", tmpOut)
		combined := stdout + "\n" + stderr
		if code == 0 {
			t.Fatalf("expected brand ID skill.kamehameha in core/skills.json to exit non-zero")
		}
		if !strings.Contains(combined, "skills.json") || !strings.Contains(combined, "skill.kamehameha") {
			t.Fatalf("expected error citing skills.json & skill.kamehameha, got:\n%s", combined)
		}
	})

	t.Run("T2_F8_02_LootGroupTotalWeightZeroRejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		_, tmpSources, tmpSchemas, tmpOut := copyGamedataToTemp(t, cfg)
		mutateRecordInJSONFile(t, filepath.Join(tmpSources, "core", "loot_tables.json"), "loot.mob_highland_wolf", func(obj map[string]any) error {
			groups := obj["roll_groups"].([]any)
			for _, gRaw := range groups {
				g := gRaw.(map[string]any)
				if gid, _ := g["group_id"].(string); gid == "consumables_and_mat" || len(groups) == 1 {
					for _, eRaw := range g["entries"].([]any) {
						e := eRaw.(map[string]any)
						e["weight"] = 0
					}
					return nil
				}
			}
			// Fallback to first group if group_id name differs
			g0 := groups[0].(map[string]any)
			for _, eRaw := range g0["entries"].([]any) {
				e := eRaw.(map[string]any)
				e["weight"] = 0
			}
			return nil
		})
		stdout, stderr, code := runDataCompiler(t, bin, "--validate", "--src", tmpSources, "--schemas", tmpSchemas, "--out", tmpOut)
		combined := stdout + "\n" + stderr
		if code == 0 {
			t.Fatalf("expected loot group with total weight 0 to exit non-zero")
		}
		hasLootFile := strings.Contains(combined, "loot_tables.json")
		hasLocation := strings.Contains(combined, "loot.mob_highland_wolf") ||
			strings.Contains(combined, "consumables_and_mat") ||
			strings.Contains(combined, "roll_groups") ||
			strings.Contains(combined, "entries") ||
			strings.Contains(combined, "$[0]") ||
			strings.Contains(combined, "weight")
		if !hasLootFile || !hasLocation {
			t.Fatalf("expected error citing loot_tables.json and location (record/group/JSON path), got:\n%s", combined)
		}
	})

	t.Run("T2_F8_03_CRLFvsLFIdenticalSHA256", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		_, srcLF, schLF, _ := copyGamedataToTemp(t, cfg)
		_, srcCRLF, schCRLF, _ := copyGamedataToTemp(t, cfg)

		rewriteDirLineEndings(t, filepath.Join(srcLF, "core"), "\n")
		rewriteDirLineEndings(t, filepath.Join(srcCRLF, "core"), "\r\n")

		outLF := filepath.Join(t.TempDir(), "lf")
		outCRLF := filepath.Join(t.TempDir(), "crlf")

		_, se1, c1 := runDataCompiler(t, bin, "--validate", "--export", "--src", srcLF, "--schemas", schLF, "--out", outLF)
		_, se2, c2 := runDataCompiler(t, bin, "--validate", "--export", "--src", srcCRLF, "--schemas", schCRLF, "--out", outCRLF)
		if c1 != 0 || c2 != 0 {
			t.Fatalf("LF/CRLF exports failed: c1=%d (%s) c2=%d (%s)", c1, se1, c2, se2)
		}
		shaLF := strings.TrimSpace(string(mustReadFileBytes(t, filepath.Join(outLF, "backend", "gamedata_bundle.sha256"))))
		shaCRLF := strings.TrimSpace(string(mustReadFileBytes(t, filepath.Join(outCRLF, "backend", "gamedata_bundle.sha256"))))
		if shaLF != shaCRLF || len(shaLF) < 64 {
			t.Fatalf("LF vs CRLF SHA256 mismatch: LF=%q CRLF=%q", shaLF, shaCRLF)
		}
	})

	t.Run("T2_F8_04_InvalidFlagsAndMissingPathsExitNonZero", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		missingSrc := filepath.Join(t.TempDir(), "nonexistent_src")
		cases := [][]string{
			{"--unknown-flag-xyz"},
			{"--validate", "--src", missingSrc},
			{"--validate", "--src", filepath.Join(cfg.gamedataDir, "sources"), "--schemas", filepath.Join(cfg.gamedataDir, "schemas"), "--ip-pack", "Pack_Nonexistent"},
		}
		for _, args := range cases {
			stdout, stderr, code := runDataCompiler(t, bin, args...)
			combined := stdout + stderr
			if code == 0 {
				t.Fatalf("args %v unexpectedly exited 0", args)
			}
			if strings.TrimSpace(combined) == "" {
				t.Fatalf("args %v produced empty error output", args)
			}
			if strings.Contains(combined, "panic:") {
				t.Fatalf("args %v caused panic:\n%s", args, combined)
			}
		}
	})

	t.Run("T2_F8_05_ValidateOnlyDoesNotExportArtifacts", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		emptyOut := t.TempDir()
		stdout, stderr, code := runDataCompiler(t, bin,
			"--validate",
			"--src", filepath.Join(cfg.gamedataDir, "sources"),
			"--schemas", filepath.Join(cfg.gamedataDir, "schemas"),
			"--out", emptyOut,
		)
		if code != 0 {
			t.Fatalf("--validate only failed (code=%d): %s %s", code, stdout, stderr)
		}
		entries, err := os.ReadDir(emptyOut)
		if err != nil {
			t.Fatal(err)
		}
		if len(entries) != 0 {
			t.Fatalf("--validate without --export must not write output files, found %d entries in %s", len(entries), emptyOut)
		}
	})

	t.Run("T2_F8_06_NonMonotonicExpCurveRejected", func(t *testing.T) {
		bin := compileDataCompiler(t, cfg)
		_, tmpSources, tmpSchemas, tmpOut := copyGamedataToTemp(t, cfg)
		expPath := filepath.Join(tmpSources, "core", "exp_table.json")
		mutateExpLevelInJSONFile(t, expPath, 5, 100)
		stdout, stderr, code := runDataCompiler(t, bin, "--validate", "--src", tmpSources, "--schemas", tmpSchemas, "--out", tmpOut)
		combined := stdout + "\n" + stderr
		if code == 0 {
			t.Fatalf("expected non-monotonic exp_table (level 5 exp_to_next=100) to fail")
		}
		if !strings.Contains(combined, "exp_table.json") {
			t.Fatalf("expected error citing exp_table.json, got:\n%s", combined)
		}
	})
}

// ============================================================================
// Feature #9: Docker Compose & Observability Boundaries (5 subtests)
// ============================================================================

func TestTier2_F9_InfraBoundaries(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T2_F9_01_HostPort5432ConflictAvoidanceAndOverride", func(t *testing.T) {
		requireFileExists(t, cfg.composeFile, "docker-compose.local.yml")

		outDef, errDef, codeDef, _ := runCmdInDir(t, filepath.Dir(cfg.composeFile), map[string]string{"POSTGRES_HOST_PORT": ""}, 30*time.Second,
			"docker", "compose", "-f", cfg.composeFile, "config")
		if codeDef != 0 {
			t.Fatalf("compose config default failed (code=%d): %s", codeDef, errDef)
		}
		if !strings.Contains(outDef, "published: \"5433\"") && !strings.Contains(outDef, "published: 5433") && !strings.Contains(outDef, "5433") {
			t.Fatalf("expected default postgres published port 5433, got:\n%s", outDef)
		}

		outOvr, errOvr, codeOvr, _ := runCmdInDir(t, filepath.Dir(cfg.composeFile), map[string]string{"POSTGRES_HOST_PORT": "5434"}, 30*time.Second,
			"docker", "compose", "-f", cfg.composeFile, "config")
		if codeOvr != 0 || !strings.Contains(outOvr, "5434") {
			t.Fatalf("POSTGRES_HOST_PORT=5434 override failed (code=%d): %s\n%s", codeOvr, errOvr, outOvr)
		}
	})

	t.Run("T2_F9_02_EmptyEnvFallbackResolvesCleanly", func(t *testing.T) {
		requireFileExists(t, cfg.composeFile, "docker-compose.local.yml")
		env := map[string]string{
			"POSTGRES_USER":      "",
			"POSTGRES_PASSWORD":  "",
			"POSTGRES_DB":        "",
			"POSTGRES_HOST_PORT": "",
		}
		stdout, stderr, code, _ := runCmdInDir(t, filepath.Dir(cfg.composeFile), env, 30*time.Second,
			"docker", "compose", "--env-file", os.DevNull, "-f", cfg.composeFile, "config")
		if code != 0 {
			t.Fatalf("compose config with empty env failed (code=%d): %s", code, stderr)
		}
		for _, def := range []string{"iu_admin", "infinite_universe", "5433"} {
			if !strings.Contains(stdout, def) {
				t.Fatalf("compose fallback missing default %q", def)
			}
		}
	})

	t.Run("T2_F9_03_InvalidComposeProfileOrServiceFailsCleanly", func(t *testing.T) {
		requireFileExists(t, cfg.composeFile, "docker-compose.local.yml")
		stdout, stderr, code, _ := runCmd(t, 20*time.Second, "docker", "compose", "-f", cfg.composeFile, "up", "-d", "nonexistent-svc")
		if code == 0 {
			t.Fatalf("expected nonexistent service to fail non-zero, got 0 (stdout=%s stderr=%s)", stdout, stderr)
		}
	})

	t.Run("T2_F9_04_GrafanaAnonymousViewerAndAdminAuthBoundary", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		statusOrg, _, _, err := httpDoRaw(t, http.MethodGet, cfg.grafanaURL+"/api/org", nil)
		if err != nil || statusOrg != http.StatusOK {
			t.Fatalf("anonymous /api/org expected 200 OK, got status=%d err=%v", statusOrg, err)
		}
		statusNoAuth, _, _, err := httpDoRaw(t, http.MethodGet, cfg.grafanaURL+"/api/datasources", nil)
		if err != nil || (statusNoAuth != http.StatusUnauthorized && statusNoAuth != http.StatusForbidden) {
			t.Fatalf("unauthenticated /api/datasources expected 401/403, got status=%d err=%v", statusNoAuth, err)
		}
		var ds []map[string]any
		statusAdmin := httpGetJSONWithBasicAuth(t, cfg.grafanaURL+"/api/datasources", "admin", "iu_grafana_admin", &ds)
		if statusAdmin != http.StatusOK {
			t.Fatalf("admin /api/datasources expected 200, got %d", statusAdmin)
		}
	})

	t.Run("T2_F9_05_PrometheusAlertRulesLoadedAndPromtoolValid", func(t *testing.T) {
		requireFileExists(t, filepath.Join(cfg.deployDir, "prometheus", "alerts.yml"), "alerts.yml")
		skipIfDockerDisabled(t, cfg)

		stdout, stderr, code, err := runCmd(t, 30*time.Second, "docker", "exec", "iu-prometheus", "promtool", "check", "rules", "/etc/prometheus/alerts.yml")
		if err != nil || code != 0 {
			t.Fatalf("promtool check rules failed (code=%d err=%v): %s %s", code, err, stdout, stderr)
		}

		var rulesResp struct {
			Status string `json:"status"`
			Data   struct {
				Groups []struct {
					Name  string `json:"name"`
					Rules []struct {
						Name   string `json:"name"`
						Health string `json:"health"`
					} `json:"rules"`
				} `json:"groups"`
			} `json:"data"`
		}
		httpGetJSON(t, cfg.prometheusURL+"/api/v1/rules", &rulesResp)
		if rulesResp.Status != "success" {
			t.Fatalf("Prometheus /api/v1/rules status=%q", rulesResp.Status)
		}
		foundRules := make(map[string]string)
		for _, g := range rulesResp.Data.Groups {
			if g.Name == "infinite_universe_core_alerts" {
				for _, r := range g.Rules {
					foundRules[r.Name] = r.Health
				}
			}
		}
		for _, want := range []string{"FencingEpochConflictDetected", "ZoneServerTickOverBudget30Hz", "InternalGrpcHighLatencyOrErrors"} {
			if foundRules[want] != "ok" {
				t.Fatalf("alert rule %q health=%q (want ok); all=%v", want, foundRules[want], foundRules)
			}
		}
	})
}

// ============================================================================
// Feature #10: Live Stack Corner Cases (6 subtests)
// ============================================================================

func TestTier2_F10_LiveStackCornerCases(t *testing.T) {
	cfg := loadConfig(t)

	t.Run("T2_F10_01_StackRestartCleanRecovery", func(t *testing.T) {
		requireFileExists(t, cfg.composeFile, "docker-compose.local.yml")
		skipIfDockerDisabled(t, cfg)

		if _, stderr, code, err := runCmd(t, 120*time.Second, "docker", "compose", "-f", cfg.composeFile, "down"); err != nil || code != 0 {
			t.Fatalf("compose down failed (code=%d err=%v): %s", code, err, stderr)
		}
		if _, stderr, code, err := runCmd(t, 120*time.Second, "docker", "compose", "-f", cfg.composeFile, "up", "-d"); err != nil || code != 0 {
			t.Fatalf("compose up -d failed (code=%d err=%v): %s", code, err, stderr)
		}

		deadline := time.Now().Add(60 * time.Second)
		wantRunning := []string{"iu-postgres", "iu-redis", "iu-nats", "iu-gameapi", "iu-coordinator", "iu-gateway", "iu-prometheus", "iu-grafana"}
		for {
			stdout, _, code, _ := runCmd(t, 15*time.Second, "docker", "compose", "-f", cfg.composeFile, "ps", "-a", "--format", "json")
			if code == 0 {
				ps := parseComposePSJSON(t, stdout)
				allOK := ps["iu-migrate"].State == "exited" && ps["iu-migrate"].ExitCode == 0
				for _, name := range wantRunning {
					if ps[name].State != "running" || ps[name].Health != "healthy" {
						allOK = false
						break
					}
				}
				if allOK {
					break
				}
			}
			if time.Now().After(deadline) {
				t.Fatalf("stack did not recover to all healthy within 60s")
			}
			time.Sleep(2 * time.Second)
		}

		cnt := psqlExecOrFatal(t, cfg, cfg.pgDB, `SELECT count(*) FROM information_schema.tables WHERE table_schema='public' AND table_type='BASE TABLE' AND table_name <> 'schema_migrations';`)
		if cnt != "19" {
			t.Fatalf("expected 19 tables preserved after restart, got %s", cnt)
		}
	})

	t.Run("T2_F10_02_UnknownRoutesAndNonGetMethodsDoNotCrashServices", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		for _, base := range []string{cfg.gatewayHTTPURL, cfg.gatewayMetricsURL, cfg.gameapiMetricsURL, cfg.coordinatorMetricsURL} {
			st1, _, _, err1 := httpDoRaw(t, http.MethodGet, base+"/nonexistent-e2e-route", nil)
			if err1 != nil || st1 >= 500 {
				t.Fatalf("GET %s/nonexistent-e2e-route failed: status=%d err=%v", base, st1, err1)
			}
			st2, _, _, err2 := httpDoRaw(t, http.MethodPost, base+"/healthz", nil)
			if err2 != nil || st2 >= 500 {
				t.Fatalf("POST %s/healthz failed: status=%d err=%v", base, st2, err2)
			}
		}
		for _, base := range []string{cfg.gatewayMetricsURL, cfg.gameapiMetricsURL, cfg.coordinatorMetricsURL} {
			var hz map[string]any
			httpGetJSON(t, base+"/healthz", &hz)
			if hz["status"] != "ok" {
				t.Fatalf("%s/healthz degraded after probe: %+v", base, hz)
			}
		}
		restarts, stderr, code, err := runCmd(t, 15*time.Second, "docker", "inspect", "--format", "{{.RestartCount}}", "iu-gateway", "iu-gameapi", "iu-coordinator")
		if err != nil || code != 0 {
			t.Fatalf("docker inspect RestartCount failed: %s", stderr)
		}
		for _, line := range nonEmptyLines(restarts) {
			if line != "0" {
				t.Fatalf("expected RestartCount=0 across Go containers, got:\n%s", restarts)
			}
		}
	})

	t.Run("T2_F10_03_HealthzJSONSchemaStrictValidation", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		hex64 := regexp.MustCompile(`^$|^[0-9a-f]{64}$`)
		for _, base := range []string{cfg.gatewayMetricsURL, cfg.gameapiMetricsURL, cfg.coordinatorMetricsURL} {
			var hz map[string]any
			ct := httpGetJSON(t, base+"/healthz", &hz)
			if !strings.Contains(strings.ToLower(ct), "application/json") {
				t.Fatalf("%s/healthz Content-Type=%q, want application/json", base, ct)
			}
			if hz["status"] != "ok" {
				t.Fatalf("%s/healthz status=%v, want ok", base, hz["status"])
			}
			hashVal, ok := hz["gamedata_hash"].(string)
			if !ok || !hex64.MatchString(hashVal) {
				t.Fatalf("%s/healthz gamedata_hash=%v invalid (expected empty or 64-char hex)", base, hz["gamedata_hash"])
			}
		}
	})

	t.Run("T2_F10_04_GoContainersRunAsNonRootUID65532", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		stdout, stderr, code, err := runCmd(t, 15*time.Second, "docker", "inspect", "--format", "{{.Config.User}}", "iu-gateway", "iu-gameapi", "iu-coordinator")
		if err != nil || code != 0 {
			t.Fatalf("docker inspect User failed (code=%d err=%v): %s", code, err, stderr)
		}
		lines := nonEmptyLines(stdout)
		if len(lines) != 3 {
			t.Fatalf("expected 3 container user outputs, got %v", lines)
		}
		for _, u := range lines {
			if u != "65532:65532" {
				t.Fatalf("expected non-root user 65532:65532, got %q", u)
			}
		}
	})

	t.Run("T2_F10_05_MigrateIdempotentReRunNoChange", func(t *testing.T) {
		requireFileExists(t, cfg.composeFile, "docker-compose.local.yml")
		skipIfDockerDisabled(t, cfg)
		stdout, stderr, code, err := runCmd(t, 60*time.Second,
			"docker", "compose", "-f", cfg.composeFile, "run", "--rm", "--no-deps", "migrate",
			"-path=/migrations", "-database="+cfg.dockerPGDSN(), "up",
		)
		if err != nil || code != 0 {
			t.Fatalf("idempotent migrate up failed (code=%d err=%v)\nstdout:%s\nstderr:%s", code, err, stdout, stderr)
		}
		cnt := psqlExecOrFatal(t, cfg, cfg.pgDB, `SELECT count(*) FROM information_schema.tables WHERE table_schema='public' AND table_type='BASE TABLE' AND table_name <> 'schema_migrations';`)
		if cnt != "19" {
			t.Fatalf("expected 19 tables intact after idempotent up, got %s", cnt)
		}
	})

	t.Run("T2_F10_06_GatewayEdgePort8080ReachableAndMetricsFormatValid", func(t *testing.T) {
		skipIfDockerDisabled(t, cfg)
		if err := dialTCPCheck(t, "127.0.0.1:8080", 5*time.Second); err != nil {
			t.Fatalf("gateway HTTP edge 127.0.0.1:8080 unreachable: %v", err)
		}
		st, _, _, err := httpDoRaw(t, http.MethodGet, cfg.gatewayHTTPURL+"/", nil)
		if err != nil || st >= 500 {
			t.Fatalf("HTTP GET %s/ failed: status=%d err=%v", cfg.gatewayHTTPURL, st, err)
		}
		for _, base := range []string{cfg.gatewayMetricsURL, cfg.gameapiMetricsURL, cfg.coordinatorMetricsURL} {
			body := httpGetText(t, base+"/metrics")
			if !strings.Contains(body, "# HELP") || !strings.Contains(body, "# TYPE") || !strings.Contains(body, "go_info") {
				t.Fatalf("%s/metrics missing valid Prometheus exposition lines", base)
			}
		}
	})
}

// ============================================================================
// Tier 2 Local Helpers
// ============================================================================

func assertPsqlRejectedWith(t *testing.T, cfg e2eConfig, dbName, sql, wantText string) {
	t.Helper()
	stdout, stderr, code, _ := psqlExec(t, cfg, dbName, sql)
	combined := stdout + "\n" + stderr
	if code == 0 {
		t.Fatalf("expected SQL to fail with %q, but exited 0\nSQL:\n%s\nSTDOUT:\n%s", wantText, sql, stdout)
	}
	if !strings.Contains(combined, wantText) {
		t.Fatalf("SQL failed as expected (code=%d), but output missing %q:\n%s", code, wantText, combined)
	}
}

func appendLootChainTables(t *testing.T, lootPath string) {
	t.Helper()
	raw := mustReadFileBytes(t, lootPath)
	var root any
	if err := json.Unmarshal(raw, &root); err != nil {
		t.Fatal(err)
	}

	makeTable := func(id, nextID string) map[string]any {
		entry := map[string]any{
			"entry_type": "SUB_TABLE",
			"ref_id":     nextID,
			"weight":     100,
			"min_qty":    1,
			"max_qty":    1,
		}
		if nextID == "" {
			entry["entry_type"] = "ITEM"
			entry["ref_id"] = "item.mat_crimson_ore_t1"
		}
		return map[string]any{
			"loot_table_id": id,
			"roll_groups": []any{
				map[string]any{
					"group_id": "chain",
					"rolls":    1,
					"entries":  []any{entry},
				},
			},
		}
	}

	chain := []map[string]any{
		makeTable("loot.sub_d0", "loot.sub_d1"),
		makeTable("loot.sub_d1", "loot.sub_d2"),
		makeTable("loot.sub_d2", "loot.sub_d3"),
		makeTable("loot.sub_d3", "loot.sub_d4"),
		makeTable("loot.sub_d4", ""),
	}

	switch v := root.(type) {
	case []any:
		for _, tbl := range chain {
			v = append(v, tbl)
		}
		root = v
	case map[string]any:
		if arr, ok := v["loot_tables"].([]any); ok {
			for _, tbl := range chain {
				arr = append(arr, tbl)
			}
			v["loot_tables"] = arr
		} else {
			for _, tbl := range chain {
				v[tbl["loot_table_id"].(string)] = tbl
			}
		}
		root = v
	}

	out, err := json.MarshalIndent(root, "", "  ")
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(lootPath, append(out, '\n'), 0o644); err != nil {
		t.Fatal(err)
	}
}

func injectLootChainPackMappings(t *testing.T, tmpSources string, ids []string) {
	t.Helper()
	for _, pack := range []string{"Pack_Original_Commercial", "Pack_DB_Reference"} {
		for _, fname := range []string{"strings_vi.json", "strings_en.json", "asset_bindings.json"} {
			fp := filepath.Join(tmpSources, "packs", pack, fname)
			if _, err := os.Stat(fp); err != nil {
				continue
			}
			cloneAndInjectCanonicalEntries(t, fp, ids, strings.HasPrefix(fname, "strings_"))
		}
	}
}

func cloneAndInjectCanonicalEntries(t *testing.T, filePath string, ids []string, isStringsFile bool) {
	t.Helper()
	raw := mustReadFileBytes(t, filePath)
	var root any
	if err := json.Unmarshal(raw, &root); err != nil {
		t.Fatal(err)
	}

	template := findSamplePackEntry(root)
	cloneEntry := func(cid string) map[string]any {
		if template == nil {
			if isStringsFile {
				return map[string]any{
					"canonical_id": cid,
					"display_name": "Sub Loot " + cid,
				}
			}
			return map[string]any{
				"canonical_id": cid,
			}
		}
		b, err := json.Marshal(template)
		if err != nil {
			t.Fatal(err)
		}
		var cp map[string]any
		if err := json.Unmarshal(b, &cp); err != nil {
			t.Fatal(err)
		}
		if _, ok := cp["canonical_id"]; ok {
			cp["canonical_id"] = cid
		}
		if _, ok := cp["id"]; ok {
			cp["id"] = cid
		}
		if isStringsFile {
			if _, ok := cp["display_name"]; ok {
				cp["display_name"] = "Sub Loot " + cid
			}
		}
		return cp
	}

	for _, cid := range ids {
		entry := cloneEntry(cid)
		switch v := root.(type) {
		case map[string]any:
			if entries, ok := v["entries"].([]any); ok {
				v["entries"] = append(entries, entry)
			} else if entriesMap, ok := v["entries"].(map[string]any); ok {
				entriesMap[cid] = entry
			} else {
				v[cid] = entry
			}
			root = v
		case []any:
			root = append(v, entry)
		}
	}

	out, err := json.MarshalIndent(root, "", "  ")
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filePath, append(out, '\n'), 0o644); err != nil {
		t.Fatal(err)
	}
}

func findSamplePackEntry(root any) map[string]any {
	var firstObj map[string]any
	var lootObj map[string]any
	inspectEntry := func(keyHint string, m map[string]any) {
		if firstObj == nil {
			firstObj = m
		}
		if strings.HasPrefix(keyHint, "loot.") {
			lootObj = m
			return
		}
		if cid, _ := m["canonical_id"].(string); strings.HasPrefix(cid, "loot.") {
			lootObj = m
		}
	}

	switch v := root.(type) {
	case map[string]any:
		if entries, ok := v["entries"].([]any); ok {
			for _, elem := range entries {
				if em, ok := elem.(map[string]any); ok {
					inspectEntry("", em)
				}
			}
		} else if entriesMap, ok := v["entries"].(map[string]any); ok {
			for k, val := range entriesMap {
				if em, ok := val.(map[string]any); ok {
					inspectEntry(k, em)
				}
			}
		} else {
			for k, val := range v {
				if em, ok := val.(map[string]any); ok {
					inspectEntry(k, em)
				}
			}
		}
	case []any:
		for _, elem := range v {
			if em, ok := elem.(map[string]any); ok {
				inspectEntry("", em)
			}
		}
	}
	if lootObj != nil {
		return lootObj
	}
	return firstObj
}

func injectCanonicalEntryInPackFile(t *testing.T, filePath, canonicalID string, entry map[string]any) {
	t.Helper()
	raw := mustReadFileBytes(t, filePath)
	var root any
	if err := json.Unmarshal(raw, &root); err != nil {
		t.Fatal(err)
	}
	switch v := root.(type) {
	case map[string]any:
		if entries, ok := v["entries"].([]any); ok {
			v["entries"] = append(entries, entry)
		} else if entriesMap, ok := v["entries"].(map[string]any); ok {
			entriesMap[canonicalID] = entry
		} else {
			v[canonicalID] = entry
		}
		root = v
	case []any:
		root = append(v, entry)
	}
	out, err := json.MarshalIndent(root, "", "  ")
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filePath, append(out, '\n'), 0o644); err != nil {
		t.Fatal(err)
	}
}

func mutateExpLevelInJSONFile(t *testing.T, expPath string, targetLevel int, newExp int) {
	t.Helper()
	raw := mustReadFileBytes(t, expPath)
	var root any
	if err := json.Unmarshal(raw, &root); err != nil {
		t.Fatal(err)
	}
	mutated := false
	var walk func(any)
	walk = func(node any) {
		if mutated {
			return
		}
		switch v := node.(type) {
		case map[string]any:
			if lv, ok := asFloat64(v["level"]); ok && int(lv) == targetLevel {
				if _, has := v["exp_to_next"]; has {
					v["exp_to_next"] = newExp
					mutated = true
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
	walk(root)
	if !mutated {
		t.Fatalf("level %d not found in %s", targetLevel, expPath)
	}
	out, err := json.MarshalIndent(root, "", "  ")
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(expPath, append(out, '\n'), 0o644); err != nil {
		t.Fatal(err)
	}
}

func rewriteDirLineEndings(t *testing.T, dir, eol string) {
	t.Helper()
	err := filepath.WalkDir(dir, func(path string, d fs.DirEntry, walkErr error) error {
		if walkErr != nil || d.IsDir() || !strings.HasSuffix(strings.ToLower(d.Name()), ".json") {
			return walkErr
		}
		raw, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		norm := strings.ReplaceAll(string(raw), "\r\n", "\n")
		if eol != "\n" {
			norm = strings.ReplaceAll(norm, "\n", eol)
		}
		return os.WriteFile(path, []byte(norm), 0o644)
	})
	if err != nil {
		t.Fatal(err)
	}
}

func mustReadFileBytes(t *testing.T, path string) []byte {
	t.Helper()
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	return b
}

var _ = strconv.Itoa
