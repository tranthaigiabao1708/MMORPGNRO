package e2e_test

import (
	"encoding/json"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

func TestHarness_ComputeCoreGameDataHash_CRLFvsLFAndSortedPaths(t *testing.T) {
	dirLF := t.TempDir()
	dirCRLF := t.TempDir()

	writeFile := func(base, rel, content string) {
		full := filepath.Join(base, filepath.FromSlash(rel))
		if err := os.MkdirAll(filepath.Dir(full), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(full, []byte(content), 0o644); err != nil {
			t.Fatal(err)
		}
	}

	aLF := "{\n  \"id\": \"skill.ki_volley_01\",\n  \"cost\": 20\n}\n"
	bLF := "{\n  \"id\": \"race.zenith_warrior\",\n  \"hp\": 500\n}\n"
	aCRLF := strings.ReplaceAll(aLF, "\n", "\r\n")
	bCRLF := strings.ReplaceAll(bLF, "\n", "\r\n")

	writeFile(dirLF, "sub/a.json", aLF)
	writeFile(dirLF, "b.json", bLF)
	writeFile(dirLF, "ignore.txt", "should not affect hash LF")

	writeFile(dirCRLF, "b.json", bCRLF)
	writeFile(dirCRLF, "sub/a.json", aCRLF)
	writeFile(dirCRLF, "ignore.txt", "different ignored content CRLF")

	hashLF := computeCoreGameDataHash(t, dirLF)
	hashCRLF := computeCoreGameDataHash(t, dirCRLF)

	if len(hashLF) != 64 {
		t.Fatalf("expected 64-char sha256 hex, got %q (len=%d)", hashLF, len(hashLF))
	}
	if hashLF != hashCRLF {
		t.Fatalf("CRLF vs LF hash mismatch: LF=%s CRLF=%s", hashLF, hashCRLF)
	}

	emptyDir := t.TempDir()
	if _, err := computeCoreGameDataHashErr(emptyDir); err == nil {
		t.Fatalf("expected error for empty core dir, got nil")
	}
}

func TestHarness_ParseUE5CSVAndJSON(t *testing.T) {
	tmp := t.TempDir()
	csvPath := filepath.Join(tmp, "DT_Skills.csv")
	// Include UTF-8 BOM to verify BOM stripping
	csvContent := "\xEF\xBB\xBFName,SkillId,Cooldown\r\nskill.beam_charged_01,skill.beam_charged_01,10.0\r\nskill.ki_volley_01,skill.ki_volley_01,4.0\r\n"
	if err := os.WriteFile(csvPath, []byte(csvContent), 0o644); err != nil {
		t.Fatal(err)
	}

	headers, rows := parseUE5CSV(t, csvPath)
	if len(headers) != 3 || headers[0] != "Name" {
		t.Fatalf("unexpected CSV headers: %v", headers)
	}
	if len(rows) != 2 || rows[0]["Name"] != "skill.beam_charged_01" || rows[1]["Cooldown"] != "4.0" {
		t.Fatalf("unexpected CSV rows: %+v", rows)
	}

	badCSVPath := filepath.Join(tmp, "bad.csv")
	if err := os.WriteFile(badCSVPath, []byte("WrongHeader,Col2\nval1,val2\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if _, _, err := parseUE5CSVErr(badCSVPath); err == nil {
		t.Fatalf("expected error for CSV without Name/RowName first column")
	}

	jsonPath := filepath.Join(tmp, "DT_Skills.json")
	// Include UTF-8 BOM to verify BOM stripping in parseUE5JSONErr
	jsonContent := "\xEF\xBB\xBF" + `[
  {"Name": "skill.beam_charged_01", "Cooldown": 10.0},
  {"Name": "skill.ki_volley_01", "Cooldown": 4.0}
]`
	if err := os.WriteFile(jsonPath, []byte(jsonContent), 0o644); err != nil {
		t.Fatal(err)
	}
	jRows, jNames := parseUE5JSON(t, jsonPath)
	if len(jRows) != 2 || jNames[0] != "skill.beam_charged_01" || jNames[1] != "skill.ki_volley_01" {
		t.Fatalf("unexpected UE5 JSON result: rows=%d names=%v", len(jRows), jNames)
	}
}

func TestHarness_AssertNoVS02Artifacts_CleanVsPolluted(t *testing.T) {
	cleanRoot := t.TempDir()
	cleanBackend := filepath.Join(cleanRoot, "backend")
	if err := os.MkdirAll(filepath.Join(cleanBackend, "cmd", "gateway"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(cleanBackend, "cmd", "gateway", "main.go"), []byte("package main\nfunc main() {}\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(cleanRoot, "01_GDD"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(cleanRoot, "01_GDD", "design_note.proto"), []byte("syntax = \"proto3\";"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(cleanRoot, "tests", "e2e"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(cleanRoot, "tests", "e2e", "fixture.proto"), []byte("syntax = \"proto3\";"), 0o644); err != nil {
		t.Fatal(err)
	}
	assertNoVS02Artifacts(t, cleanRoot, cleanBackend)

	pollutedRoot := t.TempDir()
	pollutedBackend := filepath.Join(pollutedRoot, "backend")
	if err := os.MkdirAll(filepath.Join(pollutedBackend, "api", "proto"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(pollutedBackend, "api", "proto", "service.proto"), []byte("syntax = \"proto3\";"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(pollutedBackend, "buf.yaml"), []byte("version: v1"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(pollutedBackend, "internal"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(pollutedBackend, "internal", "bad.go"), []byte(`package bad
const x = "iu.backend.v1"
`), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(pollutedRoot, "proto"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(pollutedRoot, "proto", "sneaky.proto"), []byte("syntax = \"proto3\";"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(pollutedBackend, "scripts"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(pollutedBackend, "scripts", "gen.sh"), []byte("#!/bin/sh\necho iu.backend.v1\n"), 0o644); err != nil {
		t.Fatal(err)
	}

	hits, err := findVS02Artifacts(pollutedRoot, pollutedBackend)
	if err != nil {
		t.Fatalf("findVS02Artifacts error: %v", err)
	}
	if len(hits) < 5 {
		t.Fatalf("expected >= 5 VS-02 violations detected (including repoRoot .proto & backend .sh), got %d: %v", len(hits), hits)
	}
}

func TestHarness_MutateAndDeleteRecordInJSONFile(t *testing.T) {
	tmp := t.TempDir()

	// Shape 1: Top-level array
	arrPath := filepath.Join(tmp, "array.json")
	if err := os.WriteFile(arrPath, []byte(`[{"skill_id":"skill.beam_charged_01","unlock_level":8},{"skill_id":"skill.ki_volley_01","unlock_level":1}]`), 0o644); err != nil {
		t.Fatal(err)
	}
	mutateRecordInJSONFile(t, arrPath, "skill.beam_charged_01", func(obj map[string]any) error {
		obj["unlock_level"] = 0
		return nil
	})
	rawArr, _ := os.ReadFile(arrPath)
	if !strings.Contains(string(rawArr), `"unlock_level": 0`) {
		t.Fatalf("array mutation did not update unlock_level: %s", string(rawArr))
	}
	deleteRecordInJSONFile(t, arrPath, "skill.ki_volley_01")
	var parsedArr []map[string]any
	if err := json.Unmarshal(mustReadBytes(t, arrPath), &parsedArr); err != nil {
		t.Fatal(err)
	}
	if len(parsedArr) != 1 || parsedArr[0]["skill_id"] != "skill.beam_charged_01" {
		t.Fatalf("unexpected array after delete: %+v", parsedArr)
	}

	// Shape 2: ID-keyed map
	mapPath := filepath.Join(tmp, "map.json")
	if err := os.WriteFile(mapPath, []byte(`{"skill.beam_charged_01":{"display_name":"Old"},"skill.ki_volley_01":{"display_name":"Volley"}}`), 0o644); err != nil {
		t.Fatal(err)
	}
	mutateRecordInJSONFile(t, mapPath, "skill.beam_charged_01", func(obj map[string]any) error {
		obj["display_name"] = "Mutated"
		return nil
	})
	deleteRecordInJSONFile(t, mapPath, "skill.ki_volley_01")
	parsedMap := readJSONMap(t, mapPath)
	if _, ok := parsedMap["skill.ki_volley_01"]; ok {
		t.Fatalf("expected skill.ki_volley_01 to be deleted from map")
	}
	entry := parsedMap["skill.beam_charged_01"].(map[string]any)
	if entry["display_name"] != "Mutated" {
		t.Fatalf("map mutation failed: %+v", entry)
	}

	// Shape 3: Wrapped object
	wrapPath := filepath.Join(tmp, "wrapped.json")
	if err := os.WriteFile(wrapPath, []byte(`{"schema_version":"2.0.0","skills":[{"skill_id":"skill.beam_charged_01","unlock_level":8}]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	mutateRecordInJSONFile(t, wrapPath, "skill.beam_charged_01", func(obj map[string]any) error {
		obj["unexpected_extra_field"] = "bogus"
		return nil
	})
	rawWrap := string(mustReadBytes(t, wrapPath))
	if !strings.Contains(rawWrap, `"unexpected_extra_field": "bogus"`) {
		t.Fatalf("wrapped mutation failed: %s", rawWrap)
	}

	// Shape 4: Shared-key edge case — aliases/categories share targetID, canonical record must win
	sharedMutPath := filepath.Join(tmp, "shared_mutate.json")
	if err := os.WriteFile(sharedMutPath, []byte(`{
  "aliases": {"skill.beam_charged_01": "beam"},
  "categories": {"skill.beam_charged_01": {"note": "tag"}},
  "skills": [{"skill_id": "skill.beam_charged_01", "unlock_level": 3}]
}`), 0o644); err != nil {
		t.Fatal(err)
	}
	mutateRecordInJSONFile(t, sharedMutPath, "skill.beam_charged_01", func(obj map[string]any) error {
		obj["unlock_level"] = 0
		return nil
	})
	sharedMutDoc := readJSONMap(t, sharedMutPath)
	catObj := sharedMutDoc["categories"].(map[string]any)["skill.beam_charged_01"].(map[string]any)
	if _, leaked := catObj["unlock_level"]; leaked {
		t.Fatalf("mutateRecordInJSONFile mutated metadata category instead of canonical record: %+v", sharedMutDoc)
	}
	skill0 := sharedMutDoc["skills"].([]any)[0].(map[string]any)
	if ul, _ := asFloat64(skill0["unlock_level"]); ul != 0 {
		t.Fatalf("expected skills[0].unlock_level=0, got %+v", skill0)
	}

	// Shape 5: Shared-key deletion — scalar alias must not shadow object entry
	sharedDelPath := filepath.Join(tmp, "shared_delete.json")
	if err := os.WriteFile(sharedDelPath, []byte(`{
  "aliases": {"skill.beam_charged_01": "beam"},
  "entries": {"skill.beam_charged_01": {"display_name": "Beam"}}
}`), 0o644); err != nil {
		t.Fatal(err)
	}
	deleteRecordInJSONFile(t, sharedDelPath, "skill.beam_charged_01")
	sharedDelDoc := readJSONMap(t, sharedDelPath)
	if _, aliasPreserved := sharedDelDoc["aliases"].(map[string]any)["skill.beam_charged_01"]; !aliasPreserved {
		t.Fatalf("expected scalar alias preserved when deleting object entry: %+v", sharedDelDoc)
	}
	if _, entryStillPresent := sharedDelDoc["entries"].(map[string]any)["skill.beam_charged_01"]; entryStillPresent {
		t.Fatalf("expected object entry deleted, still present: %+v", sharedDelDoc)
	}
}

func TestHarness_ResolveSchemaFile(t *testing.T) {
	schemasDir := t.TempDir()
	files := []string{
		"common_defs.schema.json",
		"race_definition.schema.json",
		"skill.schema.json",
		"monster_boss_definition.schema.json",
		"presentation_pack.schema.json",
		"global_combat_constants.schema.json",
	}
	for _, f := range files {
		if err := os.WriteFile(filepath.Join(schemasDir, f), []byte(`{"type":"object"}`), 0o644); err != nil {
			t.Fatal(err)
		}
	}

	for _, logical := range []string{"common_defs", "race", "skill", "monster_boss", "presentation_pack", "global_constants"} {
		resolved := resolveSchemaFile(t, schemasDir, logical)
		if !strings.HasSuffix(resolved, ".schema.json") {
			t.Fatalf("unexpected resolved path for %s: %s", logical, resolved)
		}
	}

	if _, err := resolveSchemaFileErr(schemasDir, "nonexistent_schema_family"); err == nil {
		t.Fatalf("expected error for missing schema family")
	}
}

func TestHarness_ShortRunTagAndSQLIdentifierLengths(t *testing.T) {
	tagRe := regexp.MustCompile(`^[0-9a-z]{8}$`)
	seen := make(map[string]bool, 200)
	usernamePrefixes := []string{"u_", "w_", "tx_", "lg_", "nw_", "wu_", "fe_", "im_", "st_", "rb_", "dup_", "CaseHero_", "p7_", "conc_"}
	charnamePrefixes := []string{"c_", "wc_", "txc_", "lgc_", "nwc_", "wuc_", "fec_", "imc_", "stc_", "rbc_", "dupc_", "ZenithOne_", "p7c_", "concc_"}

	for i := 0; i < 200; i++ {
		tag := shortRunTag(t)
		if !tagRe.MatchString(tag) {
			t.Fatalf("shortRunTag(%d) = %q, want 8-char [0-9a-z]", i, tag)
		}
		if seen[tag] {
			t.Fatalf("duplicate shortRunTag generated: %q", tag)
		}
		seen[tag] = true

		if i == 0 {
			for _, p := range usernamePrefixes {
				u := p + tag
				if len(u) < 3 || len(u) > 32 {
					t.Fatalf("username %q len=%d violates CHECK BETWEEN 3 AND 32", u, len(u))
				}
			}
			for _, p := range charnamePrefixes {
				c := p + tag
				if len(c) < 3 || len(c) > 20 {
					t.Fatalf("character_name %q len=%d violates CHECK BETWEEN 3 AND 20", c, len(c))
				}
			}
		}
	}
}

func TestHarness_SchemaRefAndNullableObjectChecks(t *testing.T) {
	schemasDir := t.TempDir()
	common := `{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://infinite-universe.dev/schemas/common_defs.schema.json",
  "$defs": {
    "SkillId": { "type": "string", "pattern": "^skill\\.[a-z0-9_]+$" },
    "ScopeStatus": { "type": "string", "enum": ["VERTICAL_SLICE_FULL", "MVP_STUB_IN_VS", "POST_MVP"] },
    "CritRate": { "type": "number", "minimum": 0, "maximum": 1 },
    "NodeA": { "$ref": "#/$defs/NodeB" },
    "NodeB": { "$ref": "#/$defs/NodeA" }
  }
}`
	itemValid := `{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://infinite-universe.dev/schemas/item_definition.schema.json",
  "type": "object",
  "additionalProperties": false,
  "$defs": {
    "LocalBounds": { "type": "integer", "minimum": 1, "maximum": 100 }
  },
  "properties": {
    "skill_id": { "$ref": "common_defs.schema.json#/$defs/SkillId" },
    "scope_status": { "$ref": "https://infinite-universe.dev/schemas/common_defs.schema.json#/$defs/ScopeStatus" },
    "crit_rate": { "$ref": "common_defs.schema.json#/$defs/CritRate" },
    "unlock_level": { "$ref": "#/$defs/LocalBounds" },
    "cyclic_node": { "$ref": "common_defs.schema.json#/$defs/NodeA" },
    "equip_spec": {
      "type": ["object", "null"],
      "additionalProperties": false,
      "properties": {
        "slot": { "type": "string", "enum": ["slot.head"] }
      }
    }
  }
}`
	if err := os.WriteFile(filepath.Join(schemasDir, "common_defs.schema.json"), []byte(common), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(schemasDir, "item_definition.schema.json"), []byte(itemValid), 0o644); err != nil {
		t.Fatal(err)
	}

	patterns := collectSchemaPatterns(t, schemasDir)
	if len(patterns["skill_id"]) == 0 || patterns["skill_id"][0] != `^skill\.[a-z0-9_]+$` {
		t.Fatalf("expected skill_id pattern resolved across $ref, got %v", patterns["skill_id"])
	}

	props := collectSchemaPropertyNodes(t, schemasDir)
	foundScopeEnum := false
	for _, n := range props["scope_status"] {
		if _, ok := n["enum"]; ok {
			foundScopeEnum = true
		}
	}
	if !foundScopeEnum {
		t.Fatalf("expected scope_status enum resolved across $id $ref, got %+v", props["scope_status"])
	}
	foundLocalBounds := false
	for _, n := range props["unlock_level"] {
		mn, ok1 := asFloat64(n["minimum"])
		mx, ok2 := asFloat64(n["maximum"])
		if ok1 && ok2 && mn == 1 && mx == 100 {
			foundLocalBounds = true
		}
	}
	if !foundLocalBounds {
		t.Fatalf("expected unlock_level bounds [1,100] resolved via local #/$defs $ref, got %+v", props["unlock_level"])
	}

	validDoc := readJSONMap(t, filepath.Join(schemasDir, "item_definition.schema.json"))
	assertObjectSchemasHaveAdditionalPropertiesFalse(t, "item_definition.schema.json", "$", validDoc)

	// Verify nullable ["object", "null"] WITHOUT additionalProperties: false fails loudly
	var invalidDoc map[string]any
	if err := json.Unmarshal([]byte(`{
  "type": "object",
  "additionalProperties": false,
  "properties": {
    "equip_spec": {
      "type": ["object", "null"],
      "properties": { "slot": { "type": "string" } }
    }
  }
}`), &invalidDoc); err != nil {
		t.Fatal(err)
	}
	if err := checkObjectSchemasHaveAdditionalPropertiesFalse("bad.schema.json", "$", invalidDoc); err == nil {
		t.Fatalf("expected checkObjectSchemasHaveAdditionalPropertiesFalse to reject [\"object\", \"null\"] missing additionalProperties: false")
	}
}

func mustReadBytes(t *testing.T, p string) []byte {
	t.Helper()
	b, err := os.ReadFile(p)
	if err != nil {
		t.Fatal(err)
	}
	return b
}
