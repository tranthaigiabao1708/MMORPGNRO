package main

import (
	"bytes"
	"encoding/csv"
	"encoding/json"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/infinite-universe/backend/pkg/gamedata"
)

var (
	compiledBinOnce sync.Once
	compiledBinPath string
	compiledBinErr  error
)

func getCompiledBinary(t *testing.T) string {
	t.Helper()
	compiledBinOnce.Do(func() {
		dir, err := os.MkdirTemp("", "datacompiler-test-bin-*")
		if err != nil {
			compiledBinErr = err
			return
		}
		bin := filepath.Join(dir, "datacompiler.exe")
		cmd := exec.Command("go", "build", "-o", bin, ".")
		var stderr bytes.Buffer
		cmd.Stderr = &stderr
		if err := cmd.Run(); err != nil {
			compiledBinErr = err
			return
		}
		compiledBinPath = bin
	})
	if compiledBinErr != nil {
		t.Fatalf("compile datacompiler binary: %v", compiledBinErr)
	}
	return compiledBinPath
}

func runCompiledCLI(t *testing.T, args ...string) (string, string, int) {
	t.Helper()
	bin := getCompiledBinary(t)
	cmd := exec.Command(bin, args...)
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	err := cmd.Run()
	code := 0
	if err != nil {
		if ee, ok := err.(*exec.ExitError); ok {
			code = ee.ExitCode()
		} else {
			t.Fatalf("exec %v: %v", args, err)
		}
	}
	return stdout.String(), stderr.String(), code
}

func repoGamedataDirs(t *testing.T) (srcDir, schemasDir string) {
	t.Helper()
	gdRoot := detectGamedataRoot()
	if gdRoot == "" {
		t.Fatalf("repo gamedata directory not found from cwd")
	}
	srcDir = filepath.Join(gdRoot, "sources")
	schemasDir = filepath.Join(gdRoot, "schemas")
	if _, err := os.Stat(srcDir); err != nil {
		t.Fatalf("repo gamedata/sources not found at %s: %v", srcDir, err)
	}
	if _, err := os.Stat(schemasDir); err != nil {
		t.Fatalf("repo gamedata/schemas not found at %s: %v", schemasDir, err)
	}
	return srcDir, schemasDir
}

func copyRepoGamedataToTemp(t *testing.T) (tmpSrc, tmpSchemas, tmpOut string) {
	t.Helper()
	srcDir, schemasDir := repoGamedataDirs(t)
	root := t.TempDir()
	tmpSrc = filepath.Join(root, "sources")
	tmpSchemas = filepath.Join(root, "schemas")
	tmpOut = filepath.Join(root, "build")
	copyTree(t, srcDir, tmpSrc)
	copyTree(t, schemasDir, tmpSchemas)
	return tmpSrc, tmpSchemas, tmpOut
}

func copyTree(t *testing.T, src, dst string) {
	t.Helper()
	err := filepath.WalkDir(src, func(path string, d fs.DirEntry, walkErr error) error {
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
		return os.WriteFile(target, raw, 0o644)
	})
	if err != nil {
		t.Fatalf("copyTree %s -> %s: %v", src, dst, err)
	}
}

func mutateJSONArrayRecord(t *testing.T, path, idField, targetID string, fn func(map[string]any)) {
	t.Helper()
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var rows []map[string]any
	if err := json.Unmarshal(raw, &rows); err != nil {
		t.Fatal(err)
	}
	found := false
	for _, r := range rows {
		if s, _ := r[idField].(string); s == targetID {
			fn(r)
			found = true
			break
		}
	}
	if !found {
		t.Fatalf("record %s=%s not found in %s", idField, targetID, path)
	}
	out, err := json.MarshalIndent(rows, "", "  ")
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, append(out, '\n'), 0o644); err != nil {
		t.Fatal(err)
	}
}

func mutatePackEntry(t *testing.T, path, targetID string, fn func(map[string]any)) {
	t.Helper()
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var doc map[string]any
	if err := json.Unmarshal(raw, &doc); err != nil {
		t.Fatal(err)
	}
	entries := doc["entries"].([]any)
	found := false
	for _, rawE := range entries {
		e := rawE.(map[string]any)
		if s, _ := e["canonical_id"].(string); s == targetID {
			fn(e)
			found = true
			break
		}
	}
	if !found {
		t.Fatalf("pack entry %s not found in %s", targetID, path)
	}
	out, err := json.MarshalIndent(doc, "", "  ")
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, append(out, '\n'), 0o644); err != nil {
		t.Fatal(err)
	}
}

func TestDataCompiler_ValidRepoDataAndHashParity(t *testing.T) {
	srcDir, schemasDir := repoGamedataDirs(t)
	outDir := t.TempDir()

	start := time.Now()
	stdout, stderr, code := runCompiledCLI(t,
		"--validate", "--export",
		"--src", srcDir,
		"--schemas", schemasDir,
		"--out", outDir,
	)
	elapsed := time.Since(start)

	if code != 0 {
		t.Fatalf("expected exit 0, got %d\nstdout:%s\nstderr:%s", code, stdout, stderr)
	}
	if elapsed > 2*time.Second {
		t.Fatalf("compiled binary took %s (> 2s)", elapsed)
	}

	wantHash, err := gamedata.ComputeCanonicalGameDataHash(filepath.Join(srcDir, "core"))
	if err != nil {
		t.Fatal(err)
	}

	shaRaw, err := os.ReadFile(filepath.Join(outDir, "backend", "gamedata_bundle.sha256"))
	if err != nil {
		t.Fatal(err)
	}
	gotHash := strings.TrimSpace(string(shaRaw))
	if gotHash != wantHash {
		t.Fatalf("gamedata_bundle.sha256=%q != ComputeCanonicalGameDataHash=%q", gotHash, wantHash)
	}

	bundleRaw, err := os.ReadFile(filepath.Join(outDir, "backend", "gamedata_bundle.json"))
	if err != nil {
		t.Fatal(err)
	}
	var bundle map[string]any
	if err := json.Unmarshal(bundleRaw, &bundle); err != nil {
		t.Fatal(err)
	}
	manifest := bundle["manifest"].(map[string]any)
	if manifest["gamedata_hash"] != wantHash {
		t.Fatalf("manifest.gamedata_hash=%v != %q", manifest["gamedata_hash"], wantHash)
	}
	for _, key := range []string{
		"manifest", "global_combat_constants", "exp_table", "races", "skills",
		"transformations", "items", "item_sets", "affixes", "monsters_and_bosses",
		"loot_tables", "enhance_curves", "pity_shop_entries", "zones",
	} {
		if _, ok := bundle[key]; !ok {
			t.Fatalf("gamedata_bundle.json missing key %q", key)
		}
	}

	for _, tbl := range []string{
		"DT_Races", "DT_Skills", "DT_Transformations", "DT_Items",
		"DT_Affixes", "DT_MonstersAndBosses", "DT_LootTables", "DT_PresentationPack",
	} {
		jBytes, err := os.ReadFile(filepath.Join(outDir, "unreal", tbl+".json"))
		if err != nil {
			t.Fatalf("read %s.json: %v", tbl, err)
		}
		var jRows []map[string]any
		if err := json.Unmarshal(jBytes, &jRows); err != nil || len(jRows) == 0 {
			t.Fatalf("parse %s.json: err=%v len=%d", tbl, err, len(jRows))
		}

		cBytes, err := os.ReadFile(filepath.Join(outDir, "unreal", tbl+".csv"))
		if err != nil {
			t.Fatalf("read %s.csv: %v", tbl, err)
		}
		recs, err := csv.NewReader(bytes.NewReader(cBytes)).ReadAll()
		if err != nil || len(recs) < 2 {
			t.Fatalf("parse %s.csv: err=%v lines=%d", tbl, err, len(recs))
		}
		if recs[0][0] != "Name" {
			t.Fatalf("%s.csv first header=%q, want Name", tbl, recs[0][0])
		}
		if len(recs)-1 != len(jRows) {
			t.Fatalf("%s row count mismatch: csv=%d json=%d", tbl, len(recs)-1, len(jRows))
		}
		for i := range jRows {
			if jRows[i]["Name"] != recs[i+1][0] {
				t.Fatalf("%s row %d Name mismatch: json=%v csv=%q", tbl, i, jRows[i]["Name"], recs[i+1][0])
			}
		}
	}
}

func TestDataCompiler_CRLFAndIPPackHashInvariance(t *testing.T) {
	srcLF, schLF, outLF := copyRepoGamedataToTemp(t)
	srcCRLF, schCRLF, outCRLF := copyRepoGamedataToTemp(t)

	// Rewrite CRLF core files with \r\n
	err := filepath.WalkDir(filepath.Join(srcCRLF, "core"), func(path string, d fs.DirEntry, walkErr error) error {
		if walkErr != nil || d.IsDir() || !strings.HasSuffix(d.Name(), ".json") {
			return walkErr
		}
		b, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		lf := strings.ReplaceAll(string(b), "\r\n", "\n")
		crlf := strings.ReplaceAll(lf, "\n", "\r\n")
		return os.WriteFile(path, []byte(crlf), 0o644)
	})
	if err != nil {
		t.Fatal(err)
	}

	if _, se, c := runCompiledCLI(t, "--validate", "--export", "--src", srcLF, "--schemas", schLF, "--out", outLF); c != 0 {
		t.Fatalf("LF export failed: %s", se)
	}
	if _, se, c := runCompiledCLI(t, "--validate", "--export", "--src", srcCRLF, "--schemas", schCRLF, "--out", outCRLF); c != 0 {
		t.Fatalf("CRLF export failed: %s", se)
	}

	shaLF, _ := os.ReadFile(filepath.Join(outLF, "backend", "gamedata_bundle.sha256"))
	shaCRLF, _ := os.ReadFile(filepath.Join(outCRLF, "backend", "gamedata_bundle.sha256"))
	if strings.TrimSpace(string(shaLF)) != strings.TrimSpace(string(shaCRLF)) {
		t.Fatalf("CRLF hash mismatch: %q vs %q", string(shaLF), string(shaCRLF))
	}

	// Dual IP Pack invariance
	outDB := filepath.Join(t.TempDir(), "db")
	srcRepo, schRepo := repoGamedataDirs(t)
	if _, se, c := runCompiledCLI(t, "--validate", "--export", "--ip-pack", "Pack_DB_Reference", "--src", srcRepo, "--schemas", schRepo, "--out", outDB); c != 0 {
		t.Fatalf("Pack_DB_Reference export failed: %s", se)
	}
	shaDB, _ := os.ReadFile(filepath.Join(outDB, "backend", "gamedata_bundle.sha256"))
	if strings.TrimSpace(string(shaDB)) != strings.TrimSpace(string(shaLF)) {
		t.Fatalf("hash changed across IP packs: DB=%q Orig=%q", string(shaDB), string(shaLF))
	}
	presDB, _ := os.ReadFile(filepath.Join(outDB, "unreal", "DT_PresentationPack.json"))
	presOrig, _ := os.ReadFile(filepath.Join(outLF, "unreal", "DT_PresentationPack.json"))
	if string(presDB) == string(presOrig) {
		t.Fatal("expected DT_PresentationPack.json to differ between Pack_DB_Reference and Pack_Original_Commercial")
	}
}

func TestDataCompiler_ValidateOnlyWritesNoFiles(t *testing.T) {
	srcDir, schemasDir := repoGamedataDirs(t)
	emptyOut := t.TempDir()

	_, stderr, code := runCompiledCLI(t,
		"--validate",
		"--src", srcDir,
		"--schemas", schemasDir,
		"--out", emptyOut,
	)
	if code != 0 {
		t.Fatalf("validate failed: %s", stderr)
	}
	entries, err := os.ReadDir(emptyOut)
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) != 0 {
		t.Fatalf("--validate wrote unexpected files into %s: %v", emptyOut, entries)
	}
}

func TestDataCompiler_RejectsForbiddenIPKeywordsWithExactLocation(t *testing.T) {
	cases := []struct {
		name     string
		pack     string
		mutate   func(t *testing.T, src string)
		wantFile string
		wantLoc  string
	}{
		{
			name:     "skill.kamehameha in core/skills.json",
			pack:     "Pack_Original_Commercial",
			wantFile: "skills.json",
			wantLoc:  "skill.kamehameha",
			mutate: func(t *testing.T, src string) {
				mutateJSONArrayRecord(t, filepath.Join(src, "core", "skills.json"), "skill_id", "skill.beam_charged_01", func(m map[string]any) {
					m["skill_id"] = "skill.kamehameha"
				})
			},
		},
		{
			name:     "race.saiyan in core/races.json",
			pack:     "Pack_Original_Commercial",
			wantFile: "races.json",
			wantLoc:  "race.saiyan",
			mutate: func(t *testing.T, src string) {
				mutateJSONArrayRecord(t, filepath.Join(src, "core", "races.json"), "race_id", "race.zenith_warrior", func(m map[string]any) {
					m["race_id"] = "race.saiyan"
				})
			},
		},
		{
			name:     "item.zeni_coin_01 in core/items.json",
			pack:     "Pack_Original_Commercial",
			wantFile: "items.json",
			wantLoc:  "item.zeni_coin_01",
			mutate: func(t *testing.T, src string) {
				mutateJSONArrayRecord(t, filepath.Join(src, "core", "items.json"), "item_id", "token.boss_highland_titan", func(m map[string]any) {
					m["item_id"] = "item.zeni_coin_01"
				})
			},
		},
		{
			name:     "currency.zeni in Pack_Original_Commercial strings_en.json",
			pack:     "Pack_Original_Commercial",
			wantFile: "strings_en.json",
			wantLoc:  "currency.standard_coin",
			mutate: func(t *testing.T, src string) {
				mutatePackEntry(t, filepath.Join(src, "packs", "Pack_Original_Commercial", "strings_en.json"), "currency.standard_coin", func(m map[string]any) {
					m["display_name"] = "currency.zeni"
				})
			},
		},
		{
			name:     "item.scouter_v2 in core/items.json",
			pack:     "Pack_Original_Commercial",
			wantFile: "items.json",
			wantLoc:  "item.scouter_v2",
			mutate: func(t *testing.T, src string) {
				mutateJSONArrayRecord(t, filepath.Join(src, "core", "items.json"), "item_id", "item.acc2_scouter_charm_10", func(m map[string]any) {
					m["item_id"] = "item.scouter_v2"
				})
			},
		},
		{
			name:     "DragonBall in Pack_Original_Commercial strings_en.json",
			pack:     "Pack_Original_Commercial",
			wantFile: "strings_en.json",
			wantLoc:  "skill.beam_charged_01",
			mutate: func(t *testing.T, src string) {
				mutatePackEntry(t, filepath.Join(src, "packs", "Pack_Original_Commercial", "strings_en.json"), "skill.beam_charged_01", func(m map[string]any) {
					m["display_name"] = "DragonBall Wave"
				})
			},
		},
		{
			name:     "scouter in Pack_Original_Commercial asset_bindings.json",
			pack:     "Pack_Original_Commercial",
			wantFile: "asset_bindings.json",
			wantLoc:  "item.acc2_scouter_charm_10",
			mutate: func(t *testing.T, src string) {
				mutatePackEntry(t, filepath.Join(src, "packs", "Pack_Original_Commercial", "asset_bindings.json"), "item.acc2_scouter_charm_10", func(m map[string]any) {
					m["icon_texture"] = "/Game/PresentationPacks/Original_Commercial/Icons/T_Orig_Scouter"
				})
			},
		},
		{
			name:     "standalone ki in Pack_Original_Commercial strings_en.json",
			pack:     "Pack_Original_Commercial",
			wantFile: "strings_en.json",
			wantLoc:  "skill.ki_volley_01",
			mutate: func(t *testing.T, src string) {
				mutatePackEntry(t, filepath.Join(src, "packs", "Pack_Original_Commercial", "strings_en.json"), "skill.ki_volley_01", func(m map[string]any) {
					m["display_name"] = "Ki Blast Volley"
				})
			},
		},
		{
			name:     "skill.kamehameha canonical_id in Pack_DB_Reference strings_en.json",
			pack:     "Pack_DB_Reference",
			wantFile: "strings_en.json",
			wantLoc:  "skill.kamehameha",
			mutate: func(t *testing.T, src string) {
				p := filepath.Join(src, "packs", "Pack_DB_Reference", "strings_en.json")
				raw, _ := os.ReadFile(p)
				var doc map[string]any
				_ = json.Unmarshal(raw, &doc)
				entries := doc["entries"].([]any)
				entries = append(entries, map[string]any{
					"canonical_id": "skill.kamehameha",
					"display_name": "Kamehameha",
				})
				doc["entries"] = entries
				out, _ := json.MarshalIndent(doc, "", "  ")
				_ = os.WriteFile(p, append(out, '\n'), 0o644)
			},
		},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			src, sch, out := copyRepoGamedataToTemp(t)
			tc.mutate(t, src)
			stdout, stderr, code := runCompiledCLI(t, "--validate", "--ip-pack", tc.pack, "--src", src, "--schemas", sch, "--out", out)
			combined := stdout + "\n" + stderr
			if code != 1 {
				t.Fatalf("expected exit 1, got %d\n%s", code, combined)
			}
			if !strings.Contains(combined, tc.wantFile) || !strings.Contains(combined, tc.wantLoc) {
				t.Fatalf("expected stderr citing %q and %q, got:\n%s", tc.wantFile, tc.wantLoc, combined)
			}
		})
	}
}

func TestDataCompiler_PositiveWhitelistNoFalsePositives(t *testing.T) {
	srcDir, schemasDir := repoGamedataDirs(t)
	for _, pack := range []string{"Pack_Original_Commercial", "Pack_DB_Reference"} {
		stdout, stderr, code := runCompiledCLI(t, "--validate", "--ip-pack", pack, "--src", srcDir, "--schemas", schemasDir)
		if code != 0 {
			t.Fatalf("pack %s should pass without false positives, got code=%d\nstdout:%s\nstderr:%s", pack, code, stdout, stderr)
		}
	}
}

func TestDataCompiler_RejectsInvalidLootWeightsWithExactLocation(t *testing.T) {
	t.Run("zero_weight", func(t *testing.T) {
		src, sch, out := copyRepoGamedataToTemp(t)
		mutateJSONArrayRecord(t, filepath.Join(src, "core", "loot_tables.json"), "loot_table_id", "loot.mob_highland_wolf", func(m map[string]any) {
			g := m["roll_groups"].([]any)[0].(map[string]any)
			for _, eRaw := range g["entries"].([]any) {
				e := eRaw.(map[string]any)
				e["weight"] = 0
			}
		})
		_, stderr, code := runCompiledCLI(t, "--validate", "--src", src, "--schemas", sch, "--out", out)
		if code != 1 {
			t.Fatalf("expected exit 1, got %d (%s)", code, stderr)
		}
		for _, want := range []string{"loot_tables.json", "loot.mob_highland_wolf", "group_id=consumables_and_mat", "weight"} {
			if !strings.Contains(stderr, want) {
				t.Fatalf("missing expected token %q in stderr: %s", want, stderr)
			}
		}
	})

	t.Run("single_entry_zero_weight", func(t *testing.T) {
		src, sch, out := copyRepoGamedataToTemp(t)
		mutateJSONArrayRecord(t, filepath.Join(src, "core", "loot_tables.json"), "loot_table_id", "loot.mob_highland_wolf", func(m map[string]any) {
			g := m["roll_groups"].([]any)[0].(map[string]any)
			e := g["entries"].([]any)[0].(map[string]any)
			e["weight"] = 0
		})
		_, stderr, code := runCompiledCLI(t, "--validate", "--src", src, "--schemas", sch, "--out", out)
		if code != 1 {
			t.Fatalf("expected exit 1, got %d (%s)", code, stderr)
		}
		for _, want := range []string{"loot_tables.json", "loot.mob_highland_wolf", "group_id=consumables_and_mat", "weight"} {
			if !strings.Contains(stderr, want) {
				t.Fatalf("missing expected token %q in stderr: %s", want, stderr)
			}
		}
	})

	t.Run("math_gate_direct_zero_and_negative_weight", func(t *testing.T) {
		ref := "item.mat_crimson_ore_t1"
		for _, badWeight := range []int{0, -10} {
			ds := &LoadedDataset{
				LootTables: []LootTableDef{
					{
						LootTableID:          "loot.mob_highland_wolf",
						LootDistributionMode: "PERSONAL_INSTANCED",
						RollGroups: []LootRollGroupDef{
							{
								GroupID:            "consumables_and_mat",
								GroupTriggerChance: 1.0,
								PickCount:          1,
								Entries: []LootEntryDef{
									{
										EntryType: "ITEM",
										RefID:     &ref,
										Weight:    badWeight,
										MinQty:    1,
										MaxQty:    1,
									},
								},
							},
						},
					},
				},
				ExpTable: []ExpLevelDef{{Level: 1, ExpToNext: 0, CumulativeExp: 0}},
				GlobalConst: GlobalConstantsDef{
					BaseCritRate: 0.05,
					CritRateCap:  0.75,
				},
			}
			err := validateMath(ds)
			if err == nil {
				t.Fatalf("expected validateMath error for weight=%d, got nil", badWeight)
			}
			msg := err.Error()
			for _, want := range []string{"loot_tables.json", "loot.mob_highland_wolf", "group_id=consumables_and_mat", "weight"} {
				if !strings.Contains(msg, want) {
					t.Fatalf("validateMath(weight=%d) missing %q in error: %s", badWeight, want, msg)
				}
			}
		}
	})

	t.Run("item_min_qty_zero", func(t *testing.T) {
		src, sch, out := copyRepoGamedataToTemp(t)
		mutateJSONArrayRecord(t, filepath.Join(src, "core", "loot_tables.json"), "loot_table_id", "loot.mob_highland_wolf", func(m map[string]any) {
			g := m["roll_groups"].([]any)[0].(map[string]any)
			e := g["entries"].([]any)[0].(map[string]any)
			e["min_qty"] = 0
		})
		_, stderr, code := runCompiledCLI(t, "--validate", "--src", src, "--schemas", sch, "--out", out)
		if code != 1 || !strings.Contains(stderr, "loot_tables.json") {
			t.Fatalf("expected exit 1 citing loot_tables.json, got code=%d err=%s", code, stderr)
		}
	})

	t.Run("nothing_with_max_qty_positive", func(t *testing.T) {
		src, sch, out := copyRepoGamedataToTemp(t)
		mutateJSONArrayRecord(t, filepath.Join(src, "core", "loot_tables.json"), "loot_table_id", "loot.boss_highland_titan", func(m map[string]any) {
			g := m["roll_groups"].([]any)[1].(map[string]any)
			for _, eRaw := range g["entries"].([]any) {
				e := eRaw.(map[string]any)
				if e["entry_type"] == "NOTHING" {
					e["max_qty"] = 1
				}
			}
		})
		_, stderr, code := runCompiledCLI(t, "--validate", "--src", src, "--schemas", sch, "--out", out)
		if code != 1 || !strings.Contains(stderr, "loot_tables.json") {
			t.Fatalf("expected exit 1 citing loot_tables.json, got code=%d err=%s", code, stderr)
		}
	})
}

func TestDataCompiler_RejectsDAGCycleAndDepthOver3(t *testing.T) {
	t.Run("self_cycle", func(t *testing.T) {
		src, sch, out := copyRepoGamedataToTemp(t)
		mutateJSONArrayRecord(t, filepath.Join(src, "core", "loot_tables.json"), "loot_table_id", "loot.mob_highland_wolf", func(m map[string]any) {
			g := m["roll_groups"].([]any)[0].(map[string]any)
			entries := g["entries"].([]any)
			entries = append(entries, map[string]any{
				"entry_type": "SUB_TABLE",
				"ref_id":     "loot.mob_highland_wolf",
				"weight":     100,
				"min_qty":    1,
				"max_qty":    1,
			})
			g["entries"] = entries
		})
		_, stderr, code := runCompiledCLI(t, "--validate", "--src", src, "--schemas", sch, "--out", out)
		if code != 1 || !strings.Contains(stderr, "loot_tables.json") || !strings.Contains(stderr, "cyclic") {
			t.Fatalf("expected exit 1 citing cyclic loot_tables.json, got code=%d err=%s", code, stderr)
		}
	})

	t.Run("schema_valid_depth_4_rejected_and_depth_3_accepted", func(t *testing.T) {
		buildChain := func(maxIdx int) (string, string, string) {
			src, sch, out := copyRepoGamedataToTemp(t)
			lootPath := filepath.Join(src, "core", "loot_tables.json")
			raw, _ := os.ReadFile(lootPath)
			var tables []map[string]any
			_ = json.Unmarshal(raw, &tables)

			ids := make([]string, 0, maxIdx+1)
			for i := 0; i <= maxIdx; i++ {
				id := "loot.sub_d" + string(rune('0'+i))
				ids = append(ids, id)
				entry := map[string]any{
					"entry_type": "ITEM",
					"ref_id":     "item.mat_crimson_ore_t1",
					"weight":     100,
					"min_qty":    1,
					"max_qty":    1,
				}
				if i < maxIdx {
					entry["entry_type"] = "SUB_TABLE"
					entry["ref_id"] = "loot.sub_d" + string(rune('0'+i+1))
				}
				tables = append(tables, map[string]any{
					"loot_table_id":          id,
					"loot_distribution_mode": "PERSONAL_INSTANCED",
					"roll_groups": []any{
						map[string]any{
							"group_id":             "chain",
							"group_trigger_chance": 1.0,
							"pick_count":           1,
							"entries":              []any{entry},
						},
					},
				})
			}
			b, _ := json.MarshalIndent(tables, "", "  ")
			_ = os.WriteFile(lootPath, append(b, '\n'), 0o644)

			// Register in Pack_Original_Commercial
			for _, fname := range []string{"strings_vi.json", "strings_en.json", "asset_bindings.json"} {
				p := filepath.Join(src, "packs", "Pack_Original_Commercial", fname)
				pr, _ := os.ReadFile(p)
				var doc map[string]any
				_ = json.Unmarshal(pr, &doc)
				entries := doc["entries"].([]any)
				for _, id := range ids {
					if strings.HasPrefix(fname, "strings_") {
						entries = append(entries, map[string]any{"canonical_id": id, "display_name": "Sub " + id})
					} else {
						entries = append(entries, map[string]any{"canonical_id": id})
					}
				}
				doc["entries"] = entries
				pb, _ := json.MarshalIndent(doc, "", "  ")
				_ = os.WriteFile(p, append(pb, '\n'), 0o644)
			}
			return src, sch, out
		}

		// Depth 3 (d0 -> d1 -> d2 -> d3) must pass
		src3, sch3, out3 := buildChain(3)
		if _, stderr, code := runCompiledCLI(t, "--validate", "--src", src3, "--schemas", sch3, "--out", out3); code != 0 {
			t.Fatalf("depth 3 chain should pass, got code=%d err=%s", code, stderr)
		}

		// Depth 4 (d0 -> d1 -> d2 -> d3 -> d4) must fail at Gate 2 LootDAG
		src4, sch4, out4 := buildChain(4)
		_, stderr, code := runCompiledCLI(t, "--validate", "--src", src4, "--schemas", sch4, "--out", out4)
		if code != 1 || !strings.Contains(stderr, "loot_tables.json") || !strings.Contains(stderr, "depth=4") {
			t.Fatalf("expected depth 4 rejection citing loot_tables.json & depth=4, got code=%d err=%s", code, stderr)
		}
	})
}

func TestDataCompiler_RejectsMissingRefSchemaViolationNonMonotonicExpAndMissingPack(t *testing.T) {
	t.Run("missing_loot_ref", func(t *testing.T) {
		src, sch, out := copyRepoGamedataToTemp(t)
		mutateJSONArrayRecord(t, filepath.Join(src, "core", "monsters.json"), "entity_id", "mob.highland_wolf_01", func(m map[string]any) {
			m["rewards"].(map[string]any)["loot_table_id"] = "loot.does_not_exist_99"
		})
		_, stderr, code := runCompiledCLI(t, "--validate", "--src", src, "--schemas", sch, "--out", out)
		if code != 1 || !strings.Contains(stderr, "monsters.json") || !strings.Contains(stderr, "loot.does_not_exist_99") {
			t.Fatalf("expected missing ref rejection, got code=%d err=%s", code, stderr)
		}
	})

	t.Run("schema_unknown_field", func(t *testing.T) {
		src, sch, out := copyRepoGamedataToTemp(t)
		mutateJSONArrayRecord(t, filepath.Join(src, "core", "skills.json"), "skill_id", "skill.beam_charged_01", func(m map[string]any) {
			m["unexpected_extra_field"] = true
		})
		_, stderr, code := runCompiledCLI(t, "--validate", "--src", src, "--schemas", sch, "--out", out)
		if code != 1 || !strings.Contains(stderr, "skills.json") || !strings.Contains(stderr, "unexpected_extra_field") {
			t.Fatalf("expected schema unknown field rejection, got code=%d err=%s", code, stderr)
		}
	})

	t.Run("non_monotonic_exp_table", func(t *testing.T) {
		src, sch, out := copyRepoGamedataToTemp(t)
		p := filepath.Join(src, "core", "exp_table.json")
		raw, _ := os.ReadFile(p)
		var rows []map[string]any
		_ = json.Unmarshal(raw, &rows)
		rows[4]["exp_to_next"] = 100
		b, _ := json.MarshalIndent(rows, "", "  ")
		_ = os.WriteFile(p, append(b, '\n'), 0o644)

		_, stderr, code := runCompiledCLI(t, "--validate", "--src", src, "--schemas", sch, "--out", out)
		if code != 1 || !strings.Contains(stderr, "exp_table.json") || !strings.Contains(stderr, "level_5") {
			t.Fatalf("expected non-monotonic exp_table rejection, got code=%d err=%s", code, stderr)
		}
	})

	t.Run("missing_pack_mapping", func(t *testing.T) {
		src, sch, out := copyRepoGamedataToTemp(t)
		p := filepath.Join(src, "packs", "Pack_Original_Commercial", "strings_vi.json")
		raw, _ := os.ReadFile(p)
		var doc map[string]any
		_ = json.Unmarshal(raw, &doc)
		entries := doc["entries"].([]any)
		filtered := make([]any, 0, len(entries))
		for _, e := range entries {
			if e.(map[string]any)["canonical_id"] != "boss.highland_titan_01" {
				filtered = append(filtered, e)
			}
		}
		doc["entries"] = filtered
		b, _ := json.MarshalIndent(doc, "", "  ")
		_ = os.WriteFile(p, append(b, '\n'), 0o644)

		_, stderr, code := runCompiledCLI(t, "--validate", "--src", src, "--schemas", sch, "--out", out)
		if code != 1 || !strings.Contains(stderr, "strings_vi.json") || !strings.Contains(stderr, "boss.highland_titan_01") {
			t.Fatalf("expected missing pack mapping rejection, got code=%d err=%s", code, stderr)
		}
	})
}
