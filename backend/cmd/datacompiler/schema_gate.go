package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"

	"github.com/santhosh-tekuri/jsonschema/v6"
	"golang.org/x/text/language"
	"golang.org/x/text/message"
)

func validateSchemas(ds *LoadedDataset) error {
	entries, err := os.ReadDir(ds.SchemasDir)
	if err != nil {
		return fmt.Errorf("[Gate 1: Schema] read schemas dir %s: %w", ds.SchemasDir, err)
	}

	compiler := jsonschema.NewCompiler()
	schemaByBase := make(map[string]string)

	var schemaFiles []string
	for _, e := range entries {
		if !e.IsDir() && strings.HasSuffix(strings.ToLower(e.Name()), ".schema.json") {
			schemaFiles = append(schemaFiles, e.Name())
		}
	}
	sort.Strings(schemaFiles)
	if len(schemaFiles) == 0 {
		return fmt.Errorf("[Gate 1: Schema] no *.schema.json files found in %s", ds.SchemasDir)
	}

	for _, fname := range schemaFiles {
		fullPath := filepath.Join(ds.SchemasDir, fname)
		raw, err := os.ReadFile(fullPath)
		if err != nil {
			return fmt.Errorf("[Gate 1: Schema] read %s: %w", fname, err)
		}
		doc, err := jsonschema.UnmarshalJSON(bytes.NewReader(raw))
		if err != nil {
			return fmt.Errorf("[Gate 1: Schema] parse %s: %w", fname, err)
		}

		// Extract $id if present
		resourceURL := "https://infinite-universe.dev/schemas/" + fname
		var rawMap map[string]any
		if json.Unmarshal(raw, &rawMap) == nil {
			if idStr, ok := rawMap["$id"].(string); ok && strings.TrimSpace(idStr) != "" {
				resourceURL = idStr
			}
		}

		if err := compiler.AddResource(resourceURL, doc); err != nil {
			return fmt.Errorf("[Gate 1: Schema] register %s (%s): %w", fname, resourceURL, err)
		}
		if resourceURL != fname {
			_ = compiler.AddResource(fname, doc)
		}
		schemaByBase[fname] = resourceURL
	}

	// Validate 13 core files
	coreSchemaMap := []struct {
		coreFile      string
		logicalFamily string
	}{
		{"races.json", "race"},
		{"skills.json", "skill"},
		{"transformations.json", "transformation"},
		{"items.json", "item"},
		{"item_sets.json", "item_set"},
		{"affix_pools.json", "affix"},
		{"monsters.json", "monster_boss"},
		{"loot_tables.json", "loot_table"},
		{"enhance_curves.json", "enhance_curve"},
		{"zones.json", "zone"},
		{"exp_table.json", "exp_table"},
		{"pity_shop.json", "pity_shop"},
		{"global_constants.json", "global_constants"},
	}

	for _, item := range coreSchemaMap {
		cf := ds.CoreFiles[item.coreFile]
		schemaURL, schemaFile, err := resolveLogicalSchemaURL(schemaByBase, item.logicalFamily)
		if err != nil {
			return fmt.Errorf("[Gate 1: Schema] %s: %w", cf.Name, err)
		}
		sch, err := compiler.Compile(schemaURL)
		if err != nil {
			return fmt.Errorf("[Gate 1: Schema] compile %s: %w", schemaFile, err)
		}
		if err := validateAgainstSchema(sch, cf); err != nil {
			return err
		}
	}

	// Validate 4 pack files against presentation_pack
	packURL, packSchemaFile, err := resolveLogicalSchemaURL(schemaByBase, "presentation_pack")
	if err != nil {
		return fmt.Errorf("[Gate 1: Schema] presentation_pack: %w", err)
	}
	packSch, err := compiler.Compile(packURL)
	if err != nil {
		return fmt.Errorf("[Gate 1: Schema] compile %s: %w", packSchemaFile, err)
	}
	for _, fname := range requiredPackFiles {
		cf := ds.PackFiles[fname]
		if err := validateAgainstSchema(packSch, cf); err != nil {
			return err
		}
	}

	return nil
}

func validateAgainstSchema(sch *jsonschema.Schema, cf *CoreFile) error {
	doc, err := jsonschema.UnmarshalJSON(bytes.NewReader(cf.Raw))
	if err != nil {
		return fmt.Errorf("[Gate 1: Schema] %s: %w", cf.Name, err)
	}
	err = sch.Validate(doc)
	if err == nil {
		return nil
	}
	return formatSchemaValidationError(cf, err)
}

func formatSchemaValidationError(cf *CoreFile, err error) error {
	ve, ok := err.(*jsonschema.ValidationError)
	if !ok {
		return fmt.Errorf("[Gate 1: Schema] %s: %v", cf.Name, err)
	}

	leaf := deepestValidationError(ve)
	instanceLoc := "/" + strings.Join(leaf.InstanceLocation, "/")
	recordLabel := resolveRecordLabelFromLocation(cf.Data, leaf.InstanceLocation)
	printer := message.NewPrinter(language.English)
	detail := leaf.ErrorKind.LocalizedString(printer)
	if detail == "" {
		detail = ve.Error()
	}

	if recordLabel != "" {
		return fmt.Errorf("[Gate 1: Schema] %s | record=%s | path=%s: %s", cf.Name, recordLabel, instanceLoc, detail)
	}
	return fmt.Errorf("[Gate 1: Schema] %s | path=%s: %s", cf.Name, instanceLoc, detail)
}

func deepestValidationError(ve *jsonschema.ValidationError) *jsonschema.ValidationError {
	if len(ve.Causes) == 0 {
		return ve
	}
	best := ve
	for _, c := range ve.Causes {
		cand := deepestValidationError(c)
		if len(cand.InstanceLocation) >= len(best.InstanceLocation) {
			best = cand
		}
	}
	return best
}

func resolveRecordLabelFromLocation(data any, loc []string) string {
	if len(loc) == 0 {
		return ""
	}
	if arr, ok := data.([]any); ok {
		if idx, err := strconv.Atoi(loc[0]); err == nil && idx >= 0 && idx < len(arr) {
			rec, _ := arr[idx].(map[string]any)
			baseLabel := ""
			if rec != nil {
				for _, k := range []string{
					"race_id", "skill_id", "form_id", "item_id", "set_id",
					"affix_id", "entity_id", "loot_table_id", "zone_id",
					"entry_id", "canonical_id",
				} {
					if s, ok := rec[k].(string); ok && s != "" {
						baseLabel = s
						break
					}
				}
				if baseLabel == "" {
					if lv, ok := jsonInt(rec["level"]); ok {
						baseLabel = fmt.Sprintf("level_%d", lv)
					} else if tl, ok := jsonInt(rec["target_level"]); ok {
						baseLabel = fmt.Sprintf("level_%d", tl)
					}
				}
			}

			subLabels := resolveNestedRecordLabels(rec, loc[1:])
			var b strings.Builder
			if baseLabel != "" {
				b.WriteString(baseLabel)
				if len(subLabels) > 0 {
					b.WriteString(" (")
					b.WriteString(strings.Join(subLabels, ", "))
					b.WriteString(")")
				}
				fmt.Fprintf(&b, " ($[%d])", idx)
				return b.String()
			}
			if len(subLabels) > 0 {
				return fmt.Sprintf("$[%d] (%s)", idx, strings.Join(subLabels, ", "))
			}
			return fmt.Sprintf("$[%d]", idx)
		}
	}
	if rootMap, ok := data.(map[string]any); ok && len(loc) >= 2 && loc[0] == "entries" {
		if entries, ok := rootMap["entries"].([]any); ok {
			if idx, err := strconv.Atoi(loc[1]); err == nil && idx >= 0 && idx < len(entries) {
				if rec, ok := entries[idx].(map[string]any); ok {
					if cid, ok := rec["canonical_id"].(string); ok && cid != "" {
						return fmt.Sprintf("%s (entries[%d])", cid, idx)
					}
				}
				return fmt.Sprintf("entries[%d]", idx)
			}
		}
	}
	return ""
}

func resolveNestedRecordLabels(rec map[string]any, rest []string) []string {
	if rec == nil || len(rest) < 2 {
		return nil
	}
	var labels []string
	var cur any = rec
	for i := 0; i+1 < len(rest); {
		key := rest[i]
		idxStr := rest[i+1]
		curMap, ok := cur.(map[string]any)
		if !ok {
			break
		}
		arr, ok := curMap[key].([]any)
		if !ok {
			cur = curMap[key]
			i++
			continue
		}
		idx, err := strconv.Atoi(idxStr)
		if err != nil || idx < 0 || idx >= len(arr) {
			break
		}
		elem := arr[idx]
		if elemMap, ok := elem.(map[string]any); ok {
			switch key {
			case "roll_groups":
				if gid, ok := elemMap["group_id"].(string); ok && gid != "" {
					labels = append(labels, "group_id="+gid)
				}
			case "subzones":
				if sid, ok := elemMap["subzone_id"].(string); ok && sid != "" {
					labels = append(labels, "subzone_id="+sid)
				}
			case "phases":
				if pidx, ok := jsonInt(elemMap["phase_index"]); ok {
					labels = append(labels, fmt.Sprintf("phase_index=%d", pidx))
				}
			case "tiers":
				if tier, ok := jsonInt(elemMap["tier"]); ok {
					labels = append(labels, fmt.Sprintf("tier=%d", tier))
				}
			}
		}
		cur = elem
		i += 2
	}
	return labels
}

func jsonInt(v any) (int, bool) {
	switch n := v.(type) {
	case float64:
		return int(n), true
	case int:
		return n, true
	case int64:
		return int(n), true
	case json.Number:
		if i, err := n.Int64(); err == nil {
			return int(i), true
		}
	}
	return 0, false
}

func resolveLogicalSchemaURL(schemaByBase map[string]string, logicalName string) (string, string, error) {
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
		if u, ok := schemaByBase[cand]; ok {
			return u, cand, nil
		}
	}
	return "", "", fmt.Errorf("schema for logical family %q not found (tried %v)", logicalName, candidates)
}
