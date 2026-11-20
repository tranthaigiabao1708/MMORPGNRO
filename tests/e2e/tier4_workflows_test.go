package e2e_test

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

func TestTier4_Scenario01_FreshStackBootCompileAndHashSync(t *testing.T) {
	cfg := loadConfig(t)
	requireDirExists(t, cfg.backendDir, "backend")
	requireFileExists(t, cfg.composeFile, "docker-compose.local.yml")

	// 1. Compile all Go binaries
	stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, nil, 120*time.Second, "go", "build", "./...")
	if err != nil || code != 0 {
		t.Fatalf("go build ./... failed (code=%d err=%v): %s %s", code, err, stdout, stderr)
	}

	// 2. Validate & export gamedata to isolated temp out
	bin := compileDataCompiler(t, cfg)
	tmpOut := t.TempDir()
	dcOut, dcErr, dcCode := runDataCompiler(t, bin,
		"--validate", "--export",
		"--src", filepath.Join(cfg.gamedataDir, "sources"),
		"--schemas", filepath.Join(cfg.gamedataDir, "schemas"),
		"--out", tmpOut,
	)
	if dcCode != 0 {
		t.Fatalf("datacompiler --validate --export failed (code=%d): %s %s", dcCode, dcOut, dcErr)
	}
	expectedHash := strings.TrimSpace(string(mustReadFileBytes(t, filepath.Join(tmpOut, "backend", "gamedata_bundle.sha256"))))
	if len(expectedHash) != 64 {
		t.Fatalf("invalid gamedata_bundle.sha256: %q", expectedHash)
	}

	// 3. Verify live Compose stack health across all 9 containers
	skipIfDockerDisabled(t, cfg)
	if _, upErr, upCode, err := runCmd(t, 120*time.Second, "docker", "compose", "-f", cfg.composeFile, "up", "-d"); err != nil || upCode != 0 {
		t.Fatalf("docker compose up -d failed (code=%d err=%v): %s", upCode, err, upErr)
	}

	wantRunning := []string{"iu-postgres", "iu-redis", "iu-nats", "iu-gameapi", "iu-coordinator", "iu-gateway", "iu-prometheus", "iu-grafana"}
	deadline := time.Now().Add(60 * time.Second)
	var lastPS map[string]composePSItem
	for {
		psOut, _, psCode, _ := runCmd(t, 15*time.Second, "docker", "compose", "-f", cfg.composeFile, "ps", "-a", "--format", "json")
		if psCode == 0 {
			ps := parseComposePSJSON(t, psOut)
			lastPS = ps
			allOK := ps["iu-migrate"].State == "exited" && ps["iu-migrate"].ExitCode == 0
			for _, svc := range wantRunning {
				if ps[svc].State != "running" || ps[svc].Health != "healthy" {
					allOK = false
					break
				}
			}
			if allOK {
				break
			}
		}
		if time.Now().After(deadline) {
			t.Fatalf("compose stack did not reach healthy state within 60s; last ps=%+v", lastPS)
		}
		time.Sleep(2 * time.Second)
	}

	// 4. Verify all 19 tables exist in PostgreSQL
	tblCount := psqlExecOrFatal(t, cfg, cfg.pgDB, `SELECT count(*) FROM information_schema.tables WHERE table_schema='public' AND table_type='BASE TABLE' AND table_name <> 'schema_migrations';`)
	if tblCount != "19" {
		t.Fatalf("expected 19 domain tables, got %s", tblCount)
	}

	// 5. Verify gateway /healthz status + gamedata_hash key and gameapi/coordinator hash sync
	var gwHz map[string]any
	httpGetJSON(t, cfg.gatewayMetricsURL+"/healthz", &gwHz)
	if gwHz["status"] != "ok" {
		t.Fatalf("%s/healthz status=%v, want ok", cfg.gatewayMetricsURL, gwHz["status"])
	}
	if _, ok := gwHz["gamedata_hash"].(string); !ok {
		t.Fatalf("%s/healthz missing string-typed gamedata_hash key: %+v", cfg.gatewayMetricsURL, gwHz)
	}
	for _, url := range []string{
		cfg.gameapiMetricsURL + "/healthz",
		cfg.coordinatorMetricsURL + "/healthz",
	} {
		var hz map[string]any
		httpGetJSON(t, url, &hz)
		if hz["status"] != "ok" || hz["gamedata_hash"] != expectedHash {
			t.Fatalf("%s healthz=%+v, expected status=ok and gamedata_hash=%q", url, hz, expectedHash)
		}
	}
}

func TestTier4_Scenario02_DisasterRecoveryMigrationRoundTripAndLiveness(t *testing.T) {
	cfg := loadConfig(t)
	requireFileExists(t, cfg.composeFile, "docker-compose.local.yml")
	requireFileExists(t, filepath.Join(cfg.backendDir, "migrations", "000001_init_core_schema.up.sql"), "up.sql")
	requireFileExists(t, filepath.Join(cfg.backendDir, "migrations", "000001_init_core_schema.down.sql"), "down.sql")
	skipIfDockerDisabled(t, cfg)

	// 1. Full up -> down -> up round-trip in isolated ephemeral DB
	dbName := withTempPGDatabase(t, cfg, false)
	dsn := cfg.dockerPGDSNForDB(dbName)
	runMigrate := func(args ...string) {
		cmdArgs := append([]string{"compose", "-f", cfg.composeFile, "run", "--rm", "--no-deps", "migrate", "-path=/migrations", "-database=" + dsn}, args...)
		stdout, stderr, code, err := runCmd(t, 60*time.Second, "docker", cmdArgs...)
		if err != nil || code != 0 {
			t.Fatalf("migrate %v failed (code=%d err=%v): %s %s", args, code, err, stdout, stderr)
		}
	}

	runMigrate("up")
	if c := psqlExecOrFatal(t, cfg, dbName, `SELECT count(*) FROM information_schema.tables WHERE table_schema='public' AND table_type='BASE TABLE' AND table_name <> 'schema_migrations';`); c != "19" {
		t.Fatalf("expected 19 tables after up, got %s", c)
	}
	runMigrate("down", "-all")
	if c := psqlExecOrFatal(t, cfg, dbName, `SELECT count(*) FROM information_schema.tables WHERE table_schema='public' AND table_type='BASE TABLE' AND table_name <> 'schema_migrations';`); c != "0" {
		t.Fatalf("expected 0 tables after down -all, got %s", c)
	}
	runMigrate("up")
	if c := psqlExecOrFatal(t, cfg, dbName, `SELECT count(*) FROM information_schema.tables WHERE table_schema='public' AND table_type='BASE TABLE' AND table_name <> 'schema_migrations';`); c != "19" {
		t.Fatalf("expected 19 tables restored after second up, got %s", c)
	}
	if fn := psqlExecOrFatal(t, cfg, dbName, `SELECT count(*) FROM pg_proc WHERE proname IN ('fn_prevent_ledger_mutation','fn_touch_updated_at');`); fn != "2" {
		t.Fatalf("expected 2 trigger functions restored, got %s", fn)
	}

	// 2. Verify Redis & NATS liveness
	if pong := redisExecOrFatal(t, cfg, "PING"); pong != "PONG" {
		t.Fatalf("redis PING failed: %q", pong)
	}
	var natsHz map[string]any
	httpGetJSON(t, cfg.natsMonitorURL+"/healthz", &natsHz)
	if natsHz["status"] != "ok" {
		t.Fatalf("NATS /healthz not ok: %+v", natsHz)
	}

	// 3. Run platform integration suite against live Compose PG
	env := map[string]string{
		"IU_TEST_PG_DSN":     cfg.hostPGDSN(),
		"IU_TEST_REDIS_ADDR": cfg.redisAddr,
	}
	stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, env, 180*time.Second, "go", "test", "-v", "-count=1", "./internal/platform/...")
	if err != nil || code != 0 {
		t.Fatalf("go test ./internal/platform/... failed (code=%d err=%v)\nstdout:%s\nstderr:%s", code, err, stdout, stderr)
	}
}

func TestTier4_Scenario03_ConcurrentNegativeCoinAndImmutableLedgerExploit(t *testing.T) {
	cfg := loadConfig(t)
	requireFileExists(t, filepath.Join(cfg.backendDir, "migrations", "000001_init_core_schema.up.sql"), "up.sql")
	skipIfDockerDisabled(t, cfg)

	dbName := withTempPGDatabase(t, cfg, true)
	tag := shortRunTag(t)
	charID := psqlExecOrFatal(t, cfg, dbName, fmt.Sprintf(`
WITH acc AS (
  INSERT INTO accounts (username, email, password_hash)
  VALUES ('conc_%s', 'conc_%s@test.local', 'h') RETURNING id
), ch AS (
  INSERT INTO characters (account_id, character_name, race_id, fencing_epoch)
  SELECT id, 'concc_%s', 'race.zenith_warrior', 1 FROM acc RETURNING id
), w AS (
  INSERT INTO character_wallets (character_id, standard_coin)
  SELECT id, 100 FROM ch
)
SELECT id FROM ch;
`, tag, tag, tag))

	const workers = 8
	startCh := make(chan struct{})
	var wg sync.WaitGroup
	var successCount atomic.Int32
	var checkViolationCount atomic.Int32
	errMsgs := make([]string, workers)

	for i := 0; i < workers; i++ {
		idx := i
		wg.Add(1)
		go func() {
			defer wg.Done()
			<-startCh
			txSQL := fmt.Sprintf(`
BEGIN;
WITH upd AS (
  UPDATE character_wallets
  SET standard_coin = standard_coin - 30
  WHERE character_id = '%s'
  RETURNING standard_coin
)
INSERT INTO currency_ledger (
  idempotency_key, character_id, currency_type, delta_amount, balance_after,
  reason_code, reference_type, reference_id, fencing_epoch, server_id
)
SELECT 'idem:conc:%s:%d', '%s', 'currency.standard_coin', -30, standard_coin,
       'SHOP_BUY', 'E2E_CONC', 'w_%d', 1, 'e2e-worker-%d'
FROM upd;
COMMIT;
`, charID, tag, idx, charID, idx, idx)
			stdout, stderr, code, _ := psqlExec(t, cfg, dbName, txSQL)
			if code == 0 {
				successCount.Add(1)
			} else {
				combined := stdout + stderr
				errMsgs[idx] = combined
				if strings.Contains(combined, "standard_coin") || strings.Contains(combined, "check") {
					checkViolationCount.Add(1)
				}
			}
		}()
	}
	close(startCh)
	wg.Wait()

	succ := int(successCount.Load())
	if succ != 3 {
		t.Fatalf("expected exactly 3 concurrent -30 debits from 100 coin to succeed, got %d (checkViolations=%d errs=%v)",
			succ, checkViolationCount.Load(), errMsgs)
	}
	if int(checkViolationCount.Load()) < 5 {
		t.Fatalf("expected >=5 check constraint rejections, got %d (errs=%v)", checkViolationCount.Load(), errMsgs)
	}

	reconcile := psqlExecOrFatal(t, cfg, dbName, fmt.Sprintf(`
SELECT w.standard_coin || '|' || COALESCE((SELECT SUM(delta_amount) FROM currency_ledger WHERE character_id = '%s'), 0)
FROM character_wallets w
WHERE w.character_id = '%s';
`, charID, charID))
	parts := strings.Split(reconcile, "|")
	if len(parts) != 2 {
		t.Fatalf("unexpected reconcile output %q", reconcile)
	}
	bal, _ := strconv.ParseInt(parts[0], 10, 64)
	sumDelta, _ := strconv.ParseInt(parts[1], 10, 64)
	if bal != 10 || 100+sumDelta != bal {
		t.Fatalf("ledger/wallet mismatch: balance=%d sumDelta=%d (want bal=10, sumDelta=-90)", bal, sumDelta)
	}

	// Verify immutability & stale fencing_epoch rejection
	assertPsqlRejectedWith(t, cfg, dbName, fmt.Sprintf(`UPDATE currency_ledger SET delta_amount = 0 WHERE character_id = '%s';`, charID), "IMMUTABLE_LEDGER_VIOLATION")
	assertPsqlRejectedWith(t, cfg, dbName, fmt.Sprintf(`DELETE FROM currency_ledger WHERE character_id = '%s';`, charID), "IMMUTABLE_LEDGER_VIOLATION")
	staleRows := psqlExecOrFatal(t, cfg, dbName, fmt.Sprintf(`WITH u AS (UPDATE characters SET fencing_epoch = 2 WHERE id = '%s' AND fencing_epoch <= 0 RETURNING id) SELECT count(*) FROM u;`, charID))
	if staleRows != "0" {
		t.Fatalf("stale fencing_epoch=0 update modified %s rows, want 0", staleRows)
	}
}

func TestTier4_Scenario04_CIIPHygieneAndLootIntegrityPRGate(t *testing.T) {
	cfg := loadConfig(t)
	bin := compileDataCompiler(t, cfg)

	// 1. Clean baseline passes
	_, baseSrc, baseSch, baseOut := copyGamedataToTemp(t, cfg)
	if st, se, code := runDataCompiler(t, bin, "--validate", "--export", "--src", baseSrc, "--schemas", baseSch, "--out", baseOut); code != 0 {
		t.Fatalf("clean PR baseline failed (code=%d): %s %s", code, st, se)
	}
	baseSha := strings.TrimSpace(string(mustReadFileBytes(t, filepath.Join(baseOut, "backend", "gamedata_bundle.sha256"))))

	// 2. PR Fixture #1: brand ID leak in core/skills.json -> exit != 0 + location
	_, s1, sch1, o1 := copyGamedataToTemp(t, cfg)
	mutateRecordInJSONFile(t, filepath.Join(s1, "core", "skills.json"), "skill.beam_charged_01", func(obj map[string]any) error {
		obj["skill_id"] = "skill.kamehameha"
		return nil
	})
	st1, se1, c1 := runDataCompiler(t, bin, "--validate", "--src", s1, "--schemas", sch1, "--out", o1)
	if c1 == 0 || !strings.Contains(st1+se1, "skills.json") || !strings.Contains(st1+se1, "skill.kamehameha") {
		t.Fatalf("PR Gate #1 failed to block skill.kamehameha cleanly: code=%d out=%s err=%s", c1, st1, se1)
	}

	// 3. PR Fixture #2: zero-weight loot group -> exit != 0 + location
	_, s2, sch2, o2 := copyGamedataToTemp(t, cfg)
	mutateRecordInJSONFile(t, filepath.Join(s2, "core", "loot_tables.json"), "loot.mob_highland_wolf", func(obj map[string]any) error {
		groups := obj["roll_groups"].([]any)
		g0 := groups[0].(map[string]any)
		for _, eRaw := range g0["entries"].([]any) {
			eRaw.(map[string]any)["weight"] = 0
		}
		return nil
	})
	st2, se2, c2 := runDataCompiler(t, bin, "--validate", "--src", s2, "--schemas", sch2, "--out", o2)
	if c2 == 0 || !strings.Contains(st2+se2, "loot_tables.json") {
		t.Fatalf("PR Gate #2 failed to block zero-weight loot table cleanly: code=%d out=%s err=%s", c2, st2, se2)
	}

	// 4. PR Fixture #3: unknown schema field -> exit != 0
	_, s3, sch3, o3 := copyGamedataToTemp(t, cfg)
	mutateRecordInJSONFile(t, filepath.Join(s3, "core", "items.json"), "item.body_recruit_tunic_01", func(obj map[string]any) error {
		obj["illegal_pr_field"] = true
		return nil
	})
	st3, se3, c3 := runDataCompiler(t, bin, "--validate", "--src", s3, "--schemas", sch3, "--out", o3)
	if c3 == 0 || !strings.Contains(st3+se3, "items.json") {
		t.Fatalf("PR Gate #3 failed to block unknown schema field cleanly: code=%d out=%s err=%s", c3, st3, se3)
	}

	// 5. PR Fixture #4: display text tweak in Pack_DB_Reference -> exit 0 and core gamedata_hash unchanged
	_, s4, sch4, o4 := copyGamedataToTemp(t, cfg)
	mutateRecordInJSONFile(t, filepath.Join(s4, "packs", "Pack_DB_Reference", "strings_en.json"), "skill.beam_charged_01", func(obj map[string]any) error {
		obj["display_name"] = "Super Kamehameha PR Tweaked"
		return nil
	})
	st4, se4, c4 := runDataCompiler(t, bin, "--validate", "--export", "--ip-pack", "Pack_DB_Reference", "--src", s4, "--schemas", sch4, "--out", o4)
	if c4 != 0 {
		t.Fatalf("PR Gate #4 display text tweak in Pack_DB_Reference failed: code=%d out=%s err=%s", c4, st4, se4)
	}
	sha4 := strings.TrimSpace(string(mustReadFileBytes(t, filepath.Join(o4, "backend", "gamedata_bundle.sha256"))))
	if sha4 != baseSha {
		t.Fatalf("Pack_DB_Reference display tweak altered core gamedata_hash: %q vs %q", sha4, baseSha)
	}
}

func TestTier4_Scenario05_DualTargetGoUE5BuildAndObservabilityPipeline(t *testing.T) {
	cfg := loadConfig(t)
	bin := compileDataCompiler(t, cfg)

	outDB := filepath.Join(t.TempDir(), "db")
	outOrig := filepath.Join(t.TempDir(), "orig")
	for _, tc := range []struct{ pack, out string }{
		{"Pack_DB_Reference", outDB},
		{"Pack_Original_Commercial", outOrig},
	} {
		st, se, code := runDataCompiler(t, bin,
			"--validate", "--export",
			"--ip-pack", tc.pack,
			"--src", filepath.Join(cfg.gamedataDir, "sources"),
			"--schemas", filepath.Join(cfg.gamedataDir, "schemas"),
			"--out", tc.out,
		)
		if code != 0 {
			t.Fatalf("export %s failed (code=%d): %s %s", tc.pack, code, st, se)
		}
		requireFileExists(t, filepath.Join(tc.out, "backend", "gamedata_bundle.json"), tc.pack+" bundle")
		requireFileExists(t, filepath.Join(tc.out, "backend", "gamedata_bundle.sha256"), tc.pack+" sha256")
		_, _ = parseUE5JSON(t, filepath.Join(tc.out, "unreal", "DT_Skills.json"))
		_, _ = parseUE5CSV(t, filepath.Join(tc.out, "unreal", "DT_Skills.csv"))
		_, _ = parseUE5JSON(t, filepath.Join(tc.out, "unreal", "DT_PresentationPack.json"))
		_, _ = parseUE5CSV(t, filepath.Join(tc.out, "unreal", "DT_PresentationPack.csv"))
	}

	// Verify CRLF/LF invariance
	_, coreSrcCRLF, coreSchCRLF, _ := copyGamedataToTemp(t, cfg)
	rewriteDirLineEndings(t, filepath.Join(coreSrcCRLF, "core"), "\r\n")
	outCRLF := filepath.Join(t.TempDir(), "crlf")
	if st, se, code := runDataCompiler(t, bin, "--validate", "--export", "--src", coreSrcCRLF, "--schemas", coreSchCRLF, "--out", outCRLF); code != 0 {
		t.Fatalf("CRLF export failed (code=%d): %s %s", code, st, se)
	}
	shaOrig := strings.TrimSpace(string(mustReadFileBytes(t, filepath.Join(outOrig, "backend", "gamedata_bundle.sha256"))))
	shaCRLF := strings.TrimSpace(string(mustReadFileBytes(t, filepath.Join(outCRLF, "backend", "gamedata_bundle.sha256"))))
	if shaOrig != shaCRLF {
		t.Fatalf("CRLF/LF gamedata_hash mismatch: %q vs %q", shaOrig, shaCRLF)
	}

	// Live Prometheus + Grafana pipeline check
	skipIfDockerDisabled(t, cfg)
	var promQuery struct {
		Status string `json:"status"`
		Data   struct {
			Result []struct {
				Metric map[string]string `json:"metric"`
				Value  []any             `json:"value"`
			} `json:"result"`
		} `json:"data"`
	}
	httpGetJSON(t, cfg.prometheusURL+"/api/v1/query?query=up", &promQuery)
	if promQuery.Status != "success" {
		t.Fatalf("Prometheus query?query=up failed: %+v", promQuery)
	}
	upJobs := map[string]bool{}
	for _, r := range promQuery.Data.Result {
		if len(r.Value) == 2 && fmt.Sprintf("%v", r.Value[1]) == "1" {
			upJobs[r.Metric["job"]] = true
		}
	}
	for _, job := range []string{"iu-gateway", "iu-gameapi", "iu-coordinator"} {
		if !upJobs[job] {
			t.Fatalf("Prometheus query up missing active job %q (got %v)", job, upJobs)
		}
	}

	var grafHealth map[string]any
	httpGetJSON(t, cfg.grafanaURL+"/api/health", &grafHealth)
	if grafHealth["database"] != "ok" {
		t.Fatalf("Grafana /api/health not ok: %+v", grafHealth)
	}
	var datasources []map[string]any
	if code := httpGetJSONWithBasicAuth(t, cfg.grafanaURL+"/api/datasources", "admin", "iu_grafana_admin", &datasources); code != 200 || len(datasources) == 0 {
		t.Fatalf("Grafana /api/datasources failed: code=%d count=%d", code, len(datasources))
	}
}

func TestTier4_Scenario06_StructuredLogsAndVS02ExclusionAudit(t *testing.T) {
	cfg := loadConfig(t)
	requireDirExists(t, cfg.backendDir, "backend")

	// 1. Audit zero VS-02 Protobuf/gRPC artifacts in repo
	assertNoVS02Artifacts(t, cfg.repoRoot, cfg.backendDir)

	// 2. Audit live container structured JSON logs & Redis cleanliness
	skipIfDockerDisabled(t, cfg)
	for _, cname := range []string{"iu-gateway", "iu-gameapi", "iu-coordinator"} {
		stdout, stderr, code, err := runCmd(t, 15*time.Second, "docker", "logs", "--tail", "50", cname)
		if err != nil || code != 0 {
			t.Fatalf("docker logs %s failed (code=%d err=%v): %s", cname, code, err, stderr)
		}
		combined := stdout + "\n" + stderr
		if strings.Contains(combined, "panic:") {
			t.Fatalf("panic found in %s logs:\n%s", cname, combined)
		}
		jsonLogLines := 0
		for _, line := range nonEmptyLines(combined) {
			if strings.HasPrefix(line, "{") {
				var obj map[string]any
				if err := json.Unmarshal([]byte(line), &obj); err == nil {
					jsonLogLines++
				}
			}
		}
		if jsonLogLines == 0 {
			t.Fatalf("expected structured slog JSON log lines in %s, got:\n%s", cname, combined)
		}
	}

	// 3. Verify Redis contains zero leftover e2e_ test keys
	leftover := redisScanMatch(t, cfg, "*e2e_*")
	if len(leftover) > 0 {
		t.Fatalf("found leftover e2e test keys in Redis: %v", leftover)
	}
}

var _ = os.ReadFile
