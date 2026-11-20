package e2e_test

import (
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"
)

func TestTier3_Pair01_ComposePG16MigrationsAndHealthz(t *testing.T) {
	cfg := loadConfig(t)
	requireFileExists(t, cfg.composeFile, "docker-compose.local.yml")
	skipIfDockerDisabled(t, cfg)

	psOut, stderr, code, err := runCmd(t, 30*time.Second, "docker", "compose", "-f", cfg.composeFile, "ps", "-a", "--format", "json")
	if err != nil || code != 0 {
		t.Fatalf("compose ps failed (code=%d err=%v): %s", code, err, stderr)
	}
	ps := parseComposePSJSON(t, psOut)
	if mig, ok := ps["iu-migrate"]; !ok || mig.State != "exited" || mig.ExitCode != 0 {
		t.Fatalf("iu-migrate did not exit 0: %+v", mig)
	}

	tables := nonEmptyLines(psqlExecOrFatal(t, cfg, cfg.pgDB, `
SELECT table_name FROM information_schema.tables
WHERE table_schema='public' AND table_type='BASE TABLE' AND table_name <> 'schema_migrations'
ORDER BY table_name;
`))
	if len(tables) != 19 {
		t.Fatalf("expected 19 tables in %s, got %d: %v", cfg.pgDB, len(tables), tables)
	}
	exts := nonEmptyLines(psqlExecOrFatal(t, cfg, cfg.pgDB, `SELECT extname FROM pg_extension WHERE extname IN ('pgcrypto','citext') ORDER BY extname;`))
	if len(exts) != 2 {
		t.Fatalf("expected pgcrypto & citext extensions, got %v", exts)
	}
	fns := nonEmptyLines(psqlExecOrFatal(t, cfg, cfg.pgDB, `SELECT proname FROM pg_proc WHERE proname IN ('fn_touch_updated_at','fn_prevent_ledger_mutation') ORDER BY proname;`))
	if len(fns) != 2 {
		t.Fatalf("expected trigger functions installed, got %v", fns)
	}

	for _, url := range []string{
		cfg.gatewayMetricsURL + "/healthz",
		cfg.gameapiMetricsURL + "/healthz",
		cfg.coordinatorMetricsURL + "/healthz",
	} {
		var hz map[string]any
		httpGetJSON(t, url, &hz)
		if hz["status"] != "ok" {
			t.Fatalf("%s status=%v, want ok", url, hz["status"])
		}
	}
}

func TestTier3_Pair02_DataCompilerHashMatchesServiceHealthz(t *testing.T) {
	cfg := loadConfig(t)
	bin := compileDataCompiler(t, cfg)
	tmpOut := t.TempDir()

	stdout, stderr, code := runDataCompiler(t, bin,
		"--validate", "--export",
		"--src", filepath.Join(cfg.gamedataDir, "sources"),
		"--schemas", filepath.Join(cfg.gamedataDir, "schemas"),
		"--out", tmpOut,
	)
	if code != 0 {
		t.Fatalf("datacompiler --validate --export failed (code=%d)\nstdout:%s\nstderr:%s", code, stdout, stderr)
	}

	compilerSha := strings.TrimSpace(string(mustReadFileBytes(t, filepath.Join(tmpOut, "backend", "gamedata_bundle.sha256"))))
	coreHash := computeCoreGameDataHash(t, filepath.Join(cfg.gamedataDir, "sources", "core"))
	if compilerSha != coreHash {
		t.Fatalf("datacompiler sha256 (%q) != computeCoreGameDataHash (%q)", compilerSha, coreHash)
	}

	skipIfDockerDisabled(t, cfg)
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
		if hz["status"] != "ok" {
			t.Fatalf("%s status=%v, want ok", url, hz["status"])
		}
		if hz["gamedata_hash"] != coreHash {
			t.Fatalf("%s gamedata_hash=%v != canonical coreHash=%q", url, hz["gamedata_hash"], coreHash)
		}
	}
}

func TestTier3_Pair03_UE5DataTablesMatchCanonicalIDsAndCore(t *testing.T) {
	cfg := loadConfig(t)
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

	checkTableIDs := func(tbl string, wantIDs []string) {
		t.Helper()
		_, jsonNames := parseUE5JSON(t, filepath.Join(tmpOut, "unreal", tbl+".json"))
		_, csvRows := parseUE5CSV(t, filepath.Join(tmpOut, "unreal", tbl+".csv"))
		jSet := make(map[string]bool, len(jsonNames))
		for _, n := range jsonNames {
			jSet[n] = true
		}
		cSet := make(map[string]bool, len(csvRows))
		for _, r := range csvRows {
			name := r["Name"]
			if name == "" {
				name = r["RowName"]
			}
			cSet[name] = true
		}
		for _, id := range wantIDs {
			if !jSet[id] {
				t.Fatalf("%s.json missing canonical ID %q", tbl, id)
			}
			if !cSet[id] {
				t.Fatalf("%s.csv missing canonical ID %q", tbl, id)
			}
		}
	}

	checkTableIDs("DT_Races", []string{"race.zenith_warrior", "race.terran_adept", "race.mystic_elder"})
	checkTableIDs("DT_Skills", []string{
		"skill.melee_basic_combo", "skill.def_vanish_step", "skill.ki_volley_01", "skill.beam_charged_01",
		"skill.rush_strike_01", "skill.aoe_burst_01", "skill.guard_shield_01", "skill.ult_supernova_01",
	})
	checkTableIDs("DT_Transformations", []string{"form.zenith_awaken_01"})
	checkTableIDs("DT_MonstersAndBosses", []string{
		"mob.highland_wolf_01", "mob.raider_scouter_01", "mob.crimson_enforcer_elite", "boss.highland_titan_01",
	})
	checkTableIDs("DT_Items", []string{
		"item.body_recruit_tunic_01", "item.hands_wolfhide_wraps_05", "item.legs_wolfhide_trousers_06",
		"item.feet_raider_thruster_09", "item.acc2_scouter_charm_10", "item.legs_crimson_plates_14",
		"item.head_vanguard_visor_15", "item.hands_vanguard_gauntlets_15", "item.body_vanguard_cuirass_15",
		"item.feet_vanguard_greaves_15", "item.acc1_titan_crest_ring_18", "item.special_titan_core_18",
		"item.mat_crimson_ore_t1", "item.consumable_verdant_elixir_s", "token.boss_highland_titan",
	})

	// Cross-check boss level in DT_MonstersAndBosses matches core/monsters.json
	bossJSON, _ := os.ReadFile(filepath.Join(tmpOut, "unreal", "DT_MonstersAndBosses.json"))
	if !strings.Contains(string(bossJSON), "boss.highland_titan_01") || !strings.Contains(string(bossJSON), "18") {
		t.Fatalf("DT_MonstersAndBosses.json missing boss.highland_titan_01 level 18")
	}
}

func TestTier3_Pair04_DualIPPacksPreserveCoreHashDifferPresentation(t *testing.T) {
	cfg := loadConfig(t)
	bin := compileDataCompiler(t, cfg)
	outDB := filepath.Join(t.TempDir(), "db")
	outOrig := filepath.Join(t.TempDir(), "orig")

	for _, tc := range []struct{ pack, out string }{
		{"Pack_DB_Reference", outDB},
		{"Pack_Original_Commercial", outOrig},
	} {
		stdout, stderr, code := runDataCompiler(t, bin,
			"--validate", "--export",
			"--ip-pack", tc.pack,
			"--src", filepath.Join(cfg.gamedataDir, "sources"),
			"--schemas", filepath.Join(cfg.gamedataDir, "schemas"),
			"--out", tc.out,
		)
		if code != 0 {
			t.Fatalf("datacompiler %s failed (code=%d): %s %s", tc.pack, code, stdout, stderr)
		}
	}

	shaDB := strings.TrimSpace(string(mustReadFileBytes(t, filepath.Join(outDB, "backend", "gamedata_bundle.sha256"))))
	shaOrig := strings.TrimSpace(string(mustReadFileBytes(t, filepath.Join(outOrig, "backend", "gamedata_bundle.sha256"))))
	if shaDB != shaOrig || len(shaDB) != 64 {
		t.Fatalf("core gamedata_hash must remain identical across IP packs: db=%q orig=%q", shaDB, shaOrig)
	}

	presDB := string(mustReadFileBytes(t, filepath.Join(outDB, "unreal", "DT_PresentationPack.json")))
	presOrig := string(mustReadFileBytes(t, filepath.Join(outOrig, "unreal", "DT_PresentationPack.json")))
	if presDB == presOrig {
		t.Fatalf("DT_PresentationPack.json must differ between Pack_DB_Reference and Pack_Original_Commercial")
	}

	// Verify Pack_Original_Commercial DT_PresentationPack has zero forbidden brand terms
	origData := readJSONAny(t, filepath.Join(outOrig, "unreal", "DT_PresentationPack.json"))
	scanPackOriginalNode(t, "DT_PresentationPack.json(Pack_Original_Commercial)", origData)
}

func TestTier3_Pair05_PrometheusScrapesAllGoServicesAndRulesLoaded(t *testing.T) {
	cfg := loadConfig(t)
	requireFileExists(t, filepath.Join(cfg.deployDir, "prometheus", "prometheus.yml"), "prometheus.yml")
	requireFileExists(t, filepath.Join(cfg.deployDir, "prometheus", "alerts.yml"), "alerts.yml")
	skipIfDockerDisabled(t, cfg)

	for _, base := range []string{cfg.gatewayMetricsURL, cfg.gameapiMetricsURL, cfg.coordinatorMetricsURL} {
		body := httpGetText(t, base+"/metrics")
		if !strings.Contains(body, "go_goroutines") {
			t.Fatalf("%s/metrics missing go_goroutines", base)
		}
	}

	var targets struct {
		Status string `json:"status"`
		Data   struct {
			ActiveTargets []struct {
				Labels map[string]string `json:"labels"`
				Health string            `json:"health"`
			} `json:"activeTargets"`
		} `json:"data"`
	}
	httpGetJSON(t, cfg.prometheusURL+"/api/v1/targets", &targets)
	up := map[string]bool{}
	for _, tgt := range targets.Data.ActiveTargets {
		if tgt.Health == "up" {
			up[tgt.Labels["job"]] = true
		}
	}
	for _, job := range []string{"iu-gateway", "iu-gameapi", "iu-coordinator"} {
		if !up[job] {
			t.Fatalf("Prometheus target %q is not up", job)
		}
	}

	var rules struct {
		Status string `json:"status"`
		Data   struct {
			Groups []struct {
				Name  string `json:"name"`
				Rules []struct {
					Name string `json:"name"`
				} `json:"rules"`
			} `json:"groups"`
		} `json:"data"`
	}
	httpGetJSON(t, cfg.prometheusURL+"/api/v1/rules", &rules)
	ruleNames := map[string]bool{}
	for _, g := range rules.Data.Groups {
		for _, r := range g.Rules {
			ruleNames[r.Name] = true
		}
	}
	for _, want := range []string{"FencingEpochConflictDetected", "ZoneServerTickOverBudget30Hz", "InternalGrpcHighLatencyOrErrors"} {
		if !ruleNames[want] {
			t.Fatalf("Prometheus missing loaded alert rule %q", want)
		}
	}
}

func TestTier3_Pair06_RedisKeysContractWithLiveRedisTTLAndScan(t *testing.T) {
	cfg := loadConfig(t)
	rkDir := filepath.Join(cfg.backendDir, "internal", "platform", "rediskeys")
	requireDirExists(t, rkDir, "internal/platform/rediskeys")

	stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, nil, 60*time.Second, "go", "test", "-count=1", "./internal/platform/rediskeys/...")
	if err != nil || code != 0 {
		t.Fatalf("rediskeys tests failed (code=%d err=%v): %s %s", code, err, stdout, stderr)
	}

	skipIfDockerDisabled(t, cfg)
	id := uniqueRunID(t)
	k1 := "session:ticket:" + id
	k2 := "char:lease:" + id
	k3 := "zone:registry:zone.p01_highland_01:" + id
	t.Cleanup(func() { _, _, _, _ = redisExec(t, cfg, "DEL", k1, k2, k3) })

	redisExecOrFatal(t, cfg, "SET", k1, "val", "EX", "20")
	redisExecOrFatal(t, cfg, "HSET", k2, "state", "ACTIVE")
	redisExecOrFatal(t, cfg, "EXPIRE", k2, "20")
	redisExecOrFatal(t, cfg, "HSET", k3, "status", "READY")
	redisExecOrFatal(t, cfg, "EXPIRE", k3, "20")

	for _, k := range []string{k1, k2, k3} {
		ttlStr := redisExecOrFatal(t, cfg, "TTL", k)
		ttl, err := strconv.Atoi(ttlStr)
		if err != nil || ttl <= 0 || ttl > 20 {
			t.Fatalf("key %s TTL=%q, expected (0,20]", k, ttlStr)
		}
	}
	scanKeys := redisScanMatch(t, cfg, "*"+id+"*")
	seen := make(map[string]bool, len(scanKeys))
	for _, sk := range scanKeys {
		seen[sk] = true
	}
	for _, k := range []string{k1, k2, k3} {
		if !seen[k] {
			t.Fatalf("Redis SCAN missing key %s in %v", k, scanKeys)
		}
	}
}

func TestTier3_Pair07_FencingEpochWalletsLedgerFKAndImmutability(t *testing.T) {
	cfg := loadConfig(t)
	requireFileExists(t, filepath.Join(cfg.backendDir, "migrations", "000001_init_core_schema.up.sql"), "up migration")
	skipIfDockerDisabled(t, cfg)

	dbName := withTempPGDatabase(t, cfg, true)
	tag := shortRunTag(t)
	charID := psqlExecOrFatal(t, cfg, dbName, fmt.Sprintf(`
WITH acc AS (
  INSERT INTO accounts (username, email, password_hash)
  VALUES ('p7_%s', 'p7_%s@test.local', 'h') RETURNING id
), ch AS (
  INSERT INTO characters (account_id, character_name, race_id, fencing_epoch)
  SELECT id, 'p7c_%s', 'race.zenith_warrior', 2 FROM acc RETURNING id
), w AS (
  INSERT INTO character_wallets (character_id, standard_coin)
  SELECT id, 100 FROM ch
), l AS (
  INSERT INTO currency_ledger (
    idempotency_key, character_id, currency_type, delta_amount, balance_after,
    reason_code, reference_type, reference_id, fencing_epoch, server_id
  )
  SELECT 'idem:p7:%s', id, 'currency.standard_coin', 100, 100, 'QUEST', 'Q1', 'r1', 2, 'srv1' FROM ch
)
SELECT id FROM ch;
`, tag, tag, tag, tag))

	// (a) Stale epoch update touches 0 rows
	staleRes := psqlExecOrFatal(t, cfg, dbName, fmt.Sprintf(`
WITH upd AS (
  UPDATE characters SET fencing_epoch = 3 WHERE id = '%s' AND fencing_epoch <= 1 RETURNING id
)
SELECT count(*) FROM upd;
`, charID))
	if staleRes != "0" {
		t.Fatalf("stale fencing_epoch update affected %s rows, want 0", staleRes)
	}

	// (b) UPDATE/DELETE on currency_ledger blocked with IMMUTABLE_LEDGER_VIOLATION
	assertPsqlRejectedWith(t, cfg, dbName, fmt.Sprintf(`UPDATE currency_ledger SET delta_amount = 999 WHERE idempotency_key = 'idem:p7:%s';`, tag), "IMMUTABLE_LEDGER_VIOLATION")
	assertPsqlRejectedWith(t, cfg, dbName, fmt.Sprintf(`DELETE FROM currency_ledger WHERE idempotency_key = 'idem:p7:%s';`, tag), "IMMUTABLE_LEDGER_VIOLATION")

	// (c) DELETE FROM characters blocked by ON DELETE RESTRICT FK from currency_ledger
	assertPsqlRejectedWith(t, cfg, dbName, fmt.Sprintf(`DELETE FROM characters WHERE id = '%s';`, charID), "currency_ledger")

	// (d) standard_coin = -1 rejected by CHECK constraint
	assertPsqlRejectedWith(t, cfg, dbName, fmt.Sprintf(`UPDATE character_wallets SET standard_coin = -1 WHERE character_id = '%s';`, charID), "standard_coin")
}

func TestTier3_Pair08_GoBuildAndDockerfileNonRootRuntime(t *testing.T) {
	cfg := loadConfig(t)
	requireDirExists(t, cfg.backendDir, "backend")
	requireFileExists(t, filepath.Join(cfg.backendDir, "build", "Dockerfile.go"), "Dockerfile.go")

	for _, svc := range []string{"gateway", "gameapi", "coordinator", "datacompiler"} {
		_ = compileBackendBinary(t, cfg, "./cmd/"+svc, svc)
	}
	dfRaw := string(mustReadFileBytes(t, filepath.Join(cfg.backendDir, "build", "Dockerfile.go")))
	for _, want := range []string{"CGO_ENABLED=0", "distroless", "USER 65532:65532"} {
		if !strings.Contains(dfRaw, want) {
			t.Fatalf("Dockerfile.go missing %q", want)
		}
	}

	skipIfDockerDisabled(t, cfg)
	for _, cname := range []string{"iu-gateway", "iu-gameapi", "iu-coordinator"} {
		out, stderr, code, err := runCmd(t, 15*time.Second, "docker", "inspect", "--format", "{{.Config.User}}|{{.State.Health.Status}}", cname)
		if err != nil || code != 0 {
			t.Fatalf("docker inspect %s failed: %s", cname, stderr)
		}
		if strings.TrimSpace(out) != "65532:65532|healthy" {
			t.Fatalf("%s runtime state=%q, want 65532:65532|healthy", cname, strings.TrimSpace(out))
		}
	}
}

func TestTier3_Pair09_NatsJetStreamHealthAndStackReadiness(t *testing.T) {
	cfg := loadConfig(t)
	requireFileExists(t, cfg.composeFile, "docker-compose.local.yml")
	composeRaw := string(mustReadFileBytes(t, cfg.composeFile))
	if !strings.Contains(composeRaw, "nats:") || !strings.Contains(composeRaw, "service_healthy") {
		t.Fatalf("compose file missing nats service_healthy dependency wiring")
	}

	skipIfDockerDisabled(t, cfg)
	var hz map[string]any
	httpGetJSON(t, cfg.natsMonitorURL+"/healthz", &hz)
	if hz["status"] != "ok" {
		t.Fatalf("NATS /healthz status=%v", hz["status"])
	}
	var jsz map[string]any
	httpGetJSON(t, cfg.natsMonitorURL+"/jsz", &jsz)
	if _, ok := jsz["config"]; !ok {
		t.Fatalf("NATS JetStream /jsz missing config: %+v", jsz)
	}
	for _, url := range []string{cfg.gatewayMetricsURL + "/healthz", cfg.gameapiMetricsURL + "/healthz", cfg.coordinatorMetricsURL + "/healthz"} {
		var svcHz map[string]any
		httpGetJSON(t, url, &svcHz)
		if svcHz["status"] != "ok" {
			t.Fatalf("%s status=%v", url, svcHz["status"])
		}
	}
}

func TestTier3_Pair10_ItemDefinitionsConstraintsMatchGamedataItems(t *testing.T) {
	cfg := loadConfig(t)
	itemsPath := filepath.Join(cfg.gamedataDir, "sources", "core", "items.json")
	requireFileExists(t, itemsPath, "gamedata/sources/core/items.json")
	requireFileExists(t, filepath.Join(cfg.backendDir, "migrations", "000001_init_core_schema.up.sql"), "up migration")

	items := collectItemRecords(t, readJSONAny(t, itemsPath))
	if len(items) < 15 {
		t.Fatalf("expected >= 15 VS items in items.json, got %d", len(items))
	}

	skipIfDockerDisabled(t, cfg)
	dbName := withTempPGDatabase(t, cfg, true)
	var sqlBuf strings.Builder
	for _, it := range items {
		itemID, _ := it["item_id"].(string)
		itemType, _ := it["item_type"].(string)
		pgCategory := mapVS03ItemTypeToPGCategory(t, itemID, itemType)
		rarity, _ := it["rarity"].(string)
		maxStack := 1
		if ms, ok := asFloat64(it["max_stack"]); ok && ms > 0 {
			maxStack = int(ms)
		}
		equipSlotSQL := "NULL"
		if es, ok := it["equip_spec"].(map[string]any); ok && es != nil {
			if slot, ok := es["slot"].(string); ok && slot != "" {
				equipSlotSQL = fmt.Sprintf("'%s'", strings.ReplaceAll(slot, "'", "''"))
			}
		}
		sqlBuf.WriteString(fmt.Sprintf(
			"INSERT INTO item_definitions (item_def_id, item_category, equip_slot, base_rarity, max_stack, content_hash) VALUES ('%s', '%s', %s, '%s', %d, 'e2e_hash');\n",
			strings.ReplaceAll(itemID, "'", "''"),
			strings.ReplaceAll(pgCategory, "'", "''"),
			equipSlotSQL,
			strings.ReplaceAll(rarity, "'", "''"),
			maxStack,
		))
	}
	psqlExecOrFatal(t, cfg, dbName, sqlBuf.String())

	cnt := psqlExecOrFatal(t, cfg, dbName, "SELECT count(*) FROM item_definitions;")
	if cnt != strconv.Itoa(len(items)) {
		t.Fatalf("inserted %s item_definitions rows, expected %d", cnt, len(items))
	}

	// Verify invalid rarity rejected with SQLSTATE 23514 check constraint
	assertPsqlRejectedWith(t, cfg, dbName,
		`INSERT INTO item_definitions (item_def_id, item_category, equip_slot, base_rarity, max_stack, content_hash) VALUES ('item.bad_rarity', 'CONSUMABLE', NULL, 'rarity.invalid', 10, 'h');`,
		"item_definitions",
	)
}

func TestTier3_Pair11_SchemaEnforcementAndVS02Exclusion(t *testing.T) {
	cfg := loadConfig(t)
	schemasDir := filepath.Join(cfg.gamedataDir, "schemas")
	requireDirExists(t, schemasDir, "gamedata/schemas")
	requireDirExists(t, cfg.backendDir, "backend")

	entries, err := os.ReadDir(schemasDir)
	if err != nil {
		t.Fatal(err)
	}
	schemaCount := 0
	for _, e := range entries {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".schema.json") {
			continue
		}
		schemaCount++
		m := readJSONMap(t, filepath.Join(schemasDir, e.Name()))
		if m["$schema"] != "https://json-schema.org/draft/2020-12/schema" {
			t.Fatalf("%s missing Draft 2020-12 $schema", e.Name())
		}
		assertObjectSchemasHaveAdditionalPropertiesFalse(t, e.Name(), "$", m)
	}
	if schemaCount < 10 {
		t.Fatalf("expected >=10 schema files, got %d", schemaCount)
	}

	bin := compileDataCompiler(t, cfg)
	stdout, stderr, code := runDataCompiler(t, bin,
		"--validate",
		"--src", filepath.Join(cfg.gamedataDir, "sources"),
		"--schemas", schemasDir,
	)
	if code != 0 {
		t.Fatalf("datacompiler --validate failed (code=%d): %s %s", code, stdout, stderr)
	}

	for _, mod := range []string{
		"auth", "account", "character", "progression", "inventory",
		"loot", "economy", "quest", "party", "guild", "social", "world", "analytics",
	} {
		requireDirExists(t, filepath.Join(cfg.backendDir, "internal", "modules", mod), "internal/modules/"+mod)
	}
	assertNoVS02Artifacts(t, cfg.repoRoot, cfg.backendDir)
}

func TestTier3_Pair12_PlatformIntegrationSuiteAgainstLiveComposePG(t *testing.T) {
	cfg := loadConfig(t)
	requireDirExists(t, filepath.Join(cfg.backendDir, "internal", "platform"), "internal/platform")
	skipIfDockerDisabled(t, cfg)

	env := map[string]string{
		"IU_TEST_PG_DSN":     cfg.hostPGDSN(),
		"IU_TEST_REDIS_ADDR": cfg.redisAddr,
	}
	stdout, stderr, code, err := runCmdInDir(t, cfg.backendDir, env, 180*time.Second, "go", "test", "-v", "-count=1", "./internal/platform/...")
	if err != nil || code != 0 {
		t.Fatalf("live platform suite failed (code=%d err=%v)\nstdout:\n%s\nstderr:\n%s", code, err, stdout, stderr)
	}
	if !strings.Contains(stdout, "PASS") {
		t.Fatalf("expected PASS in go test output:\n%s", stdout)
	}
}

func collectItemRecords(t *testing.T, root any) []map[string]any {
	t.Helper()
	var items []map[string]any
	var walk func(any)
	walk = func(node any) {
		switch v := node.(type) {
		case map[string]any:
			if _, hasID := v["item_id"].(string); hasID {
				if _, hasType := v["item_type"].(string); hasType {
					items = append(items, v)
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
	return items
}

func mapVS03ItemTypeToPGCategory(t *testing.T, itemID, itemType string) string {
	t.Helper()
	switch itemType {
	case "EQUIPMENT":
		return "EQUIPMENT"
	case "CONSUMABLE":
		return "CONSUMABLE"
	case "ENHANCE_MATERIAL":
		return "MATERIAL"
	case "BOSS_TOKEN":
		return "TOKEN_BOX"
	case "QUEST_ITEM":
		return "QUEST_ITEM"
	default:
		t.Fatalf("unexpected VS-03 item_type %q for item %q (cannot map to item_definitions.item_category)", itemType, itemID)
		return ""
	}
}

var _ = http.StatusOK
