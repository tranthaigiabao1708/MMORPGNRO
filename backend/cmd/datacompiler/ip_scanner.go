package main

import (
	"fmt"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"unicode"
)

var (
	// Boundary-matched keywords (token/camelCase split; avoids matching e.g. "zenith" for "zeni" or "kiên" for "ki")
	boundaryOnlyBrandKeywords = []string{
		"zeni", "ssj", "dbz", "nro", "ki",
	}

	// Unambiguous brand roots matched both by token boundaries AND case-insensitive substring
	substringBrandKeywords = []string{
		"saiyan", "super saiyan", "goku", "vegeta", "piccolo", "gohan",
		"krillin", "frieza", "freezer", "majin", "namekian", "namek",
		"earthling", "kamehameha", "kaioken", "senzu", "oozaru",
		"dragon ball", "dragonball", "ngoc rong", "ngọc rồng",
		"genki", "kienzan", "kikoho", "makankosappo", "saibaman",
		"scouter", "kakarot", "shenron",
	}

	// Exact Master Dictionary whitelist for approved canonical tokens containing "ki" or "scouter"
	exactWhitelistedTokens = map[string]bool{
		"attr.ki_atk":                true,
		"attr.ki_dmg_pct":            true,
		"skill.ki_volley_01":         true,
		"ki_projectile":              true,
		"ki_beam_charged":            true,
		"ki_aoe_burst":               true,
		"alpha_ki":                   true,
		"ki_atk":                     true,
		"ki_dmg_pct":                 true,
		"mob.raider_scouter_01":      true,
		"item.acc2_scouter_charm_10": true,
		"loot.mob_raider_scouter":    true,
		"affix.flat_ki_atk":          true,
		"affix.ki_dmg_pct":           true,
	}

	whitelistedAbilityKiRegex = regexp.MustCompile(`^Ability\.Ki\.[A-Za-z0-9]+$`)

	forbiddenCorePresentationKeys = map[string]bool{
		"display_name":          true,
		"short_description":     true,
		"lore_flavor_text":      true,
		"icon_texture":          true,
		"primary_skeletal_mesh": true,
		"primary_montage":       true,
		"primary_vfx":           true,
		"impact_vfx":            true,
		"cast_sfx":              true,
		"impact_sfx":            true,
	}

	dbRefExemptDisplayFields = map[string]bool{
		"display_name":      true,
		"short_description": true,
		"lore_flavor_text":  true,
		"pack_name":         true,
		"description":       true,
		"subtitle":          true,
		"lore":              true,
		"flavor_text":       true,
	}

	dbRefExemptAssetFields = map[string]bool{
		"asset_root_path":       true,
		"icon_texture":          true,
		"primary_skeletal_mesh": true,
		"primary_montage":       true,
		"primary_vfx":           true,
		"impact_vfx":            true,
		"cast_sfx":              true,
		"impact_sfx":            true,
	}
)

func scanIPHygiene(ds *LoadedDataset) error {
	// 1. Scan all core/ files (keys, values, filenames, forbidden presentation keys & /Game/ paths)
	coreKeys := make([]string, 0, len(ds.CoreFiles))
	for k := range ds.CoreFiles {
		coreKeys = append(coreKeys, k)
	}
	sort.Strings(coreKeys)

	for _, k := range coreKeys {
		cf := ds.CoreFiles[k]
		if kw := matchForbiddenBrand(filepath.Base(cf.Name), true); kw != "" {
			return fmt.Errorf("[Gate IP] %s | field=filename: forbidden brand keyword %q in filename %q", cf.Name, kw, cf.Name)
		}
		if err := scanCoreIPNode(cf.Name, "$", "", cf.Data); err != nil {
			return err
		}
	}

	// 2. Scan selected presentation pack
	isDBRef := ds.PackManifest.IPMode == "REFERENCE_INTERNAL" || ds.PackName == "Pack_DB_Reference"
	packKeys := make([]string, 0, len(ds.PackFiles))
	for k := range ds.PackFiles {
		packKeys = append(packKeys, k)
	}
	sort.Strings(packKeys)

	for _, k := range packKeys {
		cf := ds.PackFiles[k]
		if kw := matchForbiddenBrand(filepath.Base(cf.Name), true); kw != "" {
			return fmt.Errorf("[Gate IP] %s | field=filename: forbidden brand keyword %q in filename %q", cf.Name, kw, cf.Name)
		}
		if err := scanPackIPNode(cf.Name, "$", "", "", cf.Data, isDBRef); err != nil {
			return err
		}
	}

	return nil
}

func scanCoreIPNode(fileName, jsonPath, recordHint string, node any) error {
	switch v := node.(type) {
	case map[string]any:
		recID := extractRecordHint(v, recordHint)
		keys := sortedMapKeys(v)
		for _, k := range keys {
			if forbiddenCorePresentationKeys[k] {
				return fmt.Errorf("[Gate IP] %s | record=%s | field=%s.%s: core gamedata must not contain presentation/asset key %q",
					fileName, nonEmptyOrDash(recID), jsonPath, k, k)
			}
			if kw := matchForbiddenBrand(k, true); kw != "" {
				return fmt.Errorf("[Gate IP] %s | record=%s | field=%s.%s: forbidden brand keyword %q in key %q",
					fileName, nonEmptyOrDash(recID), jsonPath, k, kw, k)
			}
			if err := scanCoreIPNode(fileName, jsonPath+"."+k, recID, v[k]); err != nil {
				return err
			}
		}
	case []any:
		for i, elem := range v {
			if err := scanCoreIPNode(fileName, fmt.Sprintf("%s[%d]", jsonPath, i), recordHint, elem); err != nil {
				return err
			}
		}
	case string:
		if strings.Contains(v, "/Game/") {
			return fmt.Errorf("[Gate IP] %s | record=%s | field=%s: core gamedata must not contain UE5 asset path %q",
				fileName, nonEmptyOrDash(recordHint), jsonPath, v)
		}
		if kw := matchForbiddenBrand(v, true); kw != "" {
			return fmt.Errorf("[Gate IP] %s | record=%s | field=%s: forbidden brand keyword %q in %q",
				fileName, nonEmptyOrDash(recordHint), jsonPath, kw, v)
		}
	}
	return nil
}

func scanPackIPNode(fileName, jsonPath, fieldName, recordHint string, node any, isDBRef bool) error {
	switch v := node.(type) {
	case map[string]any:
		recID := extractRecordHint(v, recordHint)
		keys := sortedMapKeys(v)
		for _, k := range keys {
			// Technical JSON keys are NEVER exempt, even in Pack_DB_Reference
			if kw := matchForbiddenBrand(k, true); kw != "" {
				return fmt.Errorf("[Gate IP] %s | record=%s | field=%s.%s: forbidden brand keyword %q in key %q",
					fileName, nonEmptyOrDash(recID), jsonPath, k, kw, k)
			}
			if err := scanPackIPNode(fileName, jsonPath+"."+k, k, recID, v[k], isDBRef); err != nil {
				return err
			}
		}
	case []any:
		for i, elem := range v {
			if err := scanPackIPNode(fileName, fmt.Sprintf("%s[%d]", jsonPath, i), fieldName, recordHint, elem, isDBRef); err != nil {
				return err
			}
		}
	case string:
		if isDBRef {
			// Exempt human-readable display text and DB_Reference asset paths;
			// NEVER exempt canonical_id, id, pack_id, *_id, *_tag, or other technical fields.
			if dbRefExemptDisplayFields[fieldName] || dbRefExemptAssetFields[fieldName] {
				return nil
			}
			if fieldName == "pack_id" && v == "Pack_DB_Reference" {
				return nil
			}
			if kw := matchForbiddenBrand(v, true); kw != "" {
				return fmt.Errorf("[Gate IP] %s | record=%s | field=%s: forbidden brand keyword %q in technical value %q",
					fileName, nonEmptyOrDash(recordHint), jsonPath, kw, v)
			}
			return nil
		}

		// Pack_Original_Commercial:
		// Only exact canonical_id / id fields may use exact whitelisted Master Dictionary tokens.
		// Display text and /Game/... asset paths are NEVER whitelisted!
		allowWhitelist := fieldName == "canonical_id" || fieldName == "id"
		if kw := matchForbiddenBrand(v, allowWhitelist); kw != "" {
			return fmt.Errorf("[Gate IP] %s | record=%s | field=%s: forbidden brand keyword %q in %q",
				fileName, nonEmptyOrDash(recordHint), jsonPath, kw, v)
		}
	}
	return nil
}

func isWhitelistedToken(val string) bool {
	trimmed := strings.TrimSpace(val)
	lower := strings.ToLower(trimmed)
	if exactWhitelistedTokens[lower] {
		return true
	}
	if whitelistedAbilityKiRegex.MatchString(trimmed) {
		// Ensure the suffix after "Ability.Ki." does not hide another brand keyword
		suffix := trimmed[len("Ability.Ki."):]
		return matchForbiddenBrand(suffix, false) == ""
	}
	return false
}

// matchForbiddenBrand returns the matched forbidden brand keyword, or "" if clean.
func matchForbiddenBrand(val string, allowWhitelist bool) string {
	trimmed := strings.TrimSpace(val)
	if trimmed == "" {
		return ""
	}
	if allowWhitelist && isWhitelistedToken(trimmed) {
		return ""
	}

	lower := strings.ToLower(trimmed)
	tokens := splitIdentifierTokens(trimmed)
	joined := strings.Join(tokens, " ")

	// 1. Substring check for unambiguous brand roots (on lowered string and joined camelCase tokens)
	for _, kw := range substringBrandKeywords {
		if strings.Contains(lower, kw) || strings.Contains(joined, kw) {
			return kw
		}
	}

	// 2. Token-boundary check for short/ambiguous keywords (zeni, ssj, dbz, nro, ki)
	for _, tok := range tokens {
		for _, kw := range boundaryOnlyBrandKeywords {
			if tok == kw {
				return kw
			}
		}
	}

	return ""
}

// splitIdentifierTokens splits a string into lowercase Unicode word tokens,
// respecting delimiters (., _, -, /, whitespace, punctuation) AND camelCase transitions.
func splitIdentifierTokens(s string) []string {
	var tokens []string
	var cur []rune
	runes := []rune(s)

	flush := func() {
		if len(cur) > 0 {
			tokens = append(tokens, strings.ToLower(string(cur)))
			cur = cur[:0]
		}
	}

	for i, r := range runes {
		if !unicode.IsLetter(r) && !unicode.IsDigit(r) {
			flush()
			continue
		}
		if i > 0 && len(cur) > 0 {
			prev := runes[i-1]
			// CamelCase boundary: lower/digit -> Upper, or ACRONYM -> Upper+lower
			if unicode.IsUpper(r) && (unicode.IsLower(prev) || unicode.IsDigit(prev)) {
				flush()
			} else if unicode.IsUpper(r) && unicode.IsUpper(prev) && i+1 < len(runes) && unicode.IsLower(runes[i+1]) {
				flush()
			}
		}
		cur = append(cur, r)
	}
	flush()
	return tokens
}

func extractRecordHint(m map[string]any, fallback string) string {
	for _, k := range []string{
		"race_id", "skill_id", "form_id", "item_id", "set_id",
		"affix_id", "entity_id", "loot_table_id", "zone_id",
		"entry_id", "canonical_id", "pack_id",
	} {
		if s, ok := m[k].(string); ok && s != "" {
			return s
		}
	}
	return fallback
}

func sortedMapKeys(m map[string]any) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

func nonEmptyOrDash(s string) string {
	if s == "" {
		return "-"
	}
	return s
}
