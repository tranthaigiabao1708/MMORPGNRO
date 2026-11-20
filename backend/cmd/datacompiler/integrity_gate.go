package main

import (
	"fmt"
	"regexp"
	"sort"
	"strings"
)

var (
	masterAttrIDs = map[string]bool{
		"attr.hp": true, "attr.max_hp": true,
		"attr.energy": true, "attr.max_energy": true,
		"attr.stamina": true, "attr.max_stamina": true,
		"attr.phys_atk": true, "attr.ki_atk": true, "attr.def": true,
		"attr.phys_dmg_pct": true, "attr.ki_dmg_pct": true,
		"attr.crit_rate": true, "attr.crit_dmg": true,
		"attr.atk_speed": true, "attr.move_speed": true,
		"attr.energy_regen": true, "attr.stamina_regen": true, "attr.cdr": true,
		"pot.form_efficiency": true, "pot.energy_control": true,
		"pot.energy_capacity": true, "pot.flight_efficiency": true,
		"pot.all_resist": true,
	}

	masterSlotIDs = map[string]bool{
		"slot.head": true, "slot.body": true, "slot.hands": true, "slot.legs": true,
		"slot.feet": true, "slot.acc_1": true, "slot.acc_2": true, "slot.special": true,
	}

	masterRarityIDs = map[string]bool{
		"rarity.common": true, "rarity.uncommon": true, "rarity.rare": true,
		"rarity.epic": true, "rarity.legendary": true, "rarity.mythic": true,
	}

	coreExtractCanonicalRegex = regexp.MustCompile(`^(planet|zone|subzone|race|trait|skill|form|mob|boss|item|token|set|affix|loot|effect)\.[a-z0-9_]+$`)
)

func validateIntegrity(ds *LoadedDataset) error {
	raceMap := make(map[string]RaceDef, len(ds.Races))
	for i, r := range ds.Races {
		if _, dup := raceMap[r.RaceID]; dup {
			return fmt.Errorf("[Gate 2: RI] core/races.json | record=%s ($[%d]) | field=race_id: duplicate race_id", r.RaceID, i)
		}
		raceMap[r.RaceID] = r
		for j, mod := range r.InnatePassive.Modifiers {
			if !masterAttrIDs[mod.AttrID] {
				return fmt.Errorf("[Gate 2: RI] core/races.json | record=%s | field=innate_passive.modifiers[%d].attr_id: unknown Master Dictionary attr_id %q", r.RaceID, j, mod.AttrID)
			}
		}
	}

	skillMap := make(map[string]SkillDef, len(ds.Skills))
	for i, s := range ds.Skills {
		if _, dup := skillMap[s.SkillID]; dup {
			return fmt.Errorf("[Gate 2: RI] core/skills.json | record=%s ($[%d]) | field=skill_id: duplicate skill_id", s.SkillID, i)
		}
		skillMap[s.SkillID] = s
	}

	formMap := make(map[string]TransformationDef, len(ds.Forms))
	for i, f := range ds.Forms {
		if _, dup := formMap[f.FormID]; dup {
			return fmt.Errorf("[Gate 2: RI] core/transformations.json | record=%s ($[%d]) | field=form_id: duplicate form_id", f.FormID, i)
		}
		formMap[f.FormID] = f
		for j, mod := range f.StatBonuses {
			if !masterAttrIDs[mod.AttrID] {
				return fmt.Errorf("[Gate 2: RI] core/transformations.json | record=%s | field=stat_bonuses[%d].attr_id: unknown Master Dictionary attr_id %q", f.FormID, j, mod.AttrID)
			}
		}
	}

	itemMap := make(map[string]ItemDef, len(ds.Items))
	for i, it := range ds.Items {
		if _, dup := itemMap[it.ItemID]; dup {
			return fmt.Errorf("[Gate 2: RI] core/items.json | record=%s ($[%d]) | field=item_id: duplicate item_id", it.ItemID, i)
		}
		if !masterRarityIDs[it.Rarity] {
			return fmt.Errorf("[Gate 2: RI] core/items.json | record=%s | field=rarity: unknown rarity %q", it.ItemID, it.Rarity)
		}
		if it.EquipSpec != nil {
			if !masterSlotIDs[it.EquipSpec.Slot] {
				return fmt.Errorf("[Gate 2: RI] core/items.json | record=%s | field=equip_spec.slot: unknown slot %q", it.ItemID, it.EquipSpec.Slot)
			}
			for j, bs := range it.EquipSpec.BaseStats {
				if !masterAttrIDs[bs.AttrID] {
					return fmt.Errorf("[Gate 2: RI] core/items.json | record=%s | field=equip_spec.base_stats[%d].attr_id: unknown attr_id %q", it.ItemID, j, bs.AttrID)
				}
			}
		}
		itemMap[it.ItemID] = it
	}

	setMap := make(map[string]ItemSetDef, len(ds.ItemSets))
	for i, is := range ds.ItemSets {
		if _, dup := setMap[is.SetID]; dup {
			return fmt.Errorf("[Gate 2: RI] core/item_sets.json | record=%s ($[%d]) | field=set_id: duplicate set_id", is.SetID, i)
		}
		setMap[is.SetID] = is
		for j, b := range is.Bonuses {
			for k, mod := range b.StatModifiers {
				if !masterAttrIDs[mod.AttrID] {
					return fmt.Errorf("[Gate 2: RI] core/item_sets.json | record=%s | field=bonuses[%d].stat_modifiers[%d].attr_id: unknown attr_id %q", is.SetID, j, k, mod.AttrID)
				}
			}
		}
	}

	affixMap := make(map[string]AffixDef, len(ds.Affixes))
	for i, a := range ds.Affixes {
		if _, dup := affixMap[a.AffixID]; dup {
			return fmt.Errorf("[Gate 2: RI] core/affix_pools.json | record=%s ($[%d]) | field=affix_id: duplicate affix_id", a.AffixID, i)
		}
		if !masterAttrIDs[a.TargetAttrID] {
			return fmt.Errorf("[Gate 2: RI] core/affix_pools.json | record=%s | field=target_attr_id: unknown attr_id %q", a.AffixID, a.TargetAttrID)
		}
		for j, sl := range a.AllowedSlots {
			if !masterSlotIDs[sl] {
				return fmt.Errorf("[Gate 2: RI] core/affix_pools.json | record=%s | field=allowed_slots[%d]: unknown slot %q", a.AffixID, j, sl)
			}
		}
		affixMap[a.AffixID] = a
	}

	monsterMap := make(map[string]MonsterDef, len(ds.Monsters))
	for i, m := range ds.Monsters {
		if _, dup := monsterMap[m.EntityID]; dup {
			return fmt.Errorf("[Gate 2: RI] core/monsters.json | record=%s ($[%d]) | field=entity_id: duplicate entity_id", m.EntityID, i)
		}
		monsterMap[m.EntityID] = m
	}

	lootMap := make(map[string]LootTableDef, len(ds.LootTables))
	for i, lt := range ds.LootTables {
		if _, dup := lootMap[lt.LootTableID]; dup {
			return fmt.Errorf("[Gate 2: RI] core/loot_tables.json | record=%s ($[%d]) | field=loot_table_id: duplicate loot_table_id", lt.LootTableID, i)
		}
		lootMap[lt.LootTableID] = lt
	}

	// Cross-file Referential Integrity
	for _, r := range ds.Races {
		for _, sid := range r.StarterSkillIDs {
			if _, ok := skillMap[sid]; !ok {
				return fmt.Errorf("[Gate 2: RI] core/races.json | record=%s | field=starter_skill_ids: missing skill %q in core/skills.json", r.RaceID, sid)
			}
		}
		for _, fid := range r.UnlockableFormIDs {
			if _, ok := formMap[fid]; !ok {
				return fmt.Errorf("[Gate 2: RI] core/races.json | record=%s | field=unlockable_form_ids: missing form %q in core/transformations.json", r.RaceID, fid)
			}
		}
	}

	for _, s := range ds.Skills {
		for _, ar := range s.AllowedRaces {
			if ar != "ALL" {
				if _, ok := raceMap[ar]; !ok {
					return fmt.Errorf("[Gate 2: RI] core/skills.json | record=%s | field=allowed_races: missing race %q in core/races.json", s.SkillID, ar)
				}
			}
		}
	}

	for _, f := range ds.Forms {
		if _, ok := raceMap[f.RequiredRaceID]; !ok {
			return fmt.Errorf("[Gate 2: RI] core/transformations.json | record=%s | field=required_race_id: missing race %q in core/races.json", f.FormID, f.RequiredRaceID)
		}
		if f.PrerequisiteFormID != nil && *f.PrerequisiteFormID != "" {
			if _, ok := formMap[*f.PrerequisiteFormID]; !ok {
				return fmt.Errorf("[Gate 2: RI] core/transformations.json | record=%s | field=prerequisite_form_id: missing form %q in core/transformations.json", f.FormID, *f.PrerequisiteFormID)
			}
		}
	}

	for _, it := range ds.Items {
		if it.EquipSpec == nil {
			continue
		}
		if it.EquipSpec.SetID != nil && *it.EquipSpec.SetID != "" {
			if _, ok := setMap[*it.EquipSpec.SetID]; !ok {
				return fmt.Errorf("[Gate 2: RI] core/items.json | record=%s | field=equip_spec.set_id: missing set %q in core/item_sets.json", it.ItemID, *it.EquipSpec.SetID)
			}
		}
		for _, ar := range it.EquipSpec.AllowedRaces {
			if ar != "ALL" {
				if _, ok := raceMap[ar]; !ok {
					return fmt.Errorf("[Gate 2: RI] core/items.json | record=%s | field=equip_spec.allowed_races: missing race %q in core/races.json", it.ItemID, ar)
				}
			}
		}
	}

	for _, is := range ds.ItemSets {
		for _, pid := range is.PieceItemIDs {
			if _, ok := itemMap[pid]; !ok {
				return fmt.Errorf("[Gate 2: RI] core/item_sets.json | record=%s | field=piece_item_ids: missing item %q in core/items.json", is.SetID, pid)
			}
		}
	}

	for _, m := range ds.Monsters {
		if _, ok := lootMap[m.Rewards.LootTableID]; !ok {
			return fmt.Errorf("[Gate 2: RI] core/monsters.json | record=%s | field=rewards.loot_table_id: missing loot table %q in core/loot_tables.json", m.EntityID, m.Rewards.LootTableID)
		}
		if m.Rewards.GuaranteedPityTokenID != nil && *m.Rewards.GuaranteedPityTokenID != "" {
			tokID := *m.Rewards.GuaranteedPityTokenID
			tok, ok := itemMap[tokID]
			if !ok {
				return fmt.Errorf("[Gate 2: RI] core/monsters.json | record=%s | field=rewards.guaranteed_pity_token_id: missing item %q in core/items.json", m.EntityID, tokID)
			}
			if tok.ItemType != "BOSS_TOKEN" {
				return fmt.Errorf("[Gate 2: RI] core/monsters.json | record=%s | field=rewards.guaranteed_pity_token_id: %q has item_type=%q, want BOSS_TOKEN", m.EntityID, tokID, tok.ItemType)
			}
		}
	}

	for _, lt := range ds.LootTables {
		for gi, g := range lt.RollGroups {
			for ei, e := range g.Entries {
				switch e.EntryType {
				case "ITEM":
					if e.RefID == nil {
						return fmt.Errorf("[Gate 2: RI] core/loot_tables.json | record=%s | field=roll_groups[%d].entries[%d].ref_id: ITEM entry must have non-null ref_id", lt.LootTableID, gi, ei)
					}
					if _, ok := itemMap[*e.RefID]; !ok {
						return fmt.Errorf("[Gate 2: RI] core/loot_tables.json | record=%s | field=roll_groups[%d].entries[%d].ref_id: missing item %q in core/items.json", lt.LootTableID, gi, ei, *e.RefID)
					}
				case "SUB_TABLE":
					if e.RefID == nil {
						return fmt.Errorf("[Gate 2: RI] core/loot_tables.json | record=%s | field=roll_groups[%d].entries[%d].ref_id: SUB_TABLE entry must have non-null ref_id", lt.LootTableID, gi, ei)
					}
					if _, ok := lootMap[*e.RefID]; !ok {
						return fmt.Errorf("[Gate 2: RI] core/loot_tables.json | record=%s | field=roll_groups[%d].entries[%d].ref_id: missing sub-table %q in core/loot_tables.json", lt.LootTableID, gi, ei, *e.RefID)
					}
				}
			}
		}
	}

	// Loot sub-table DAG cycle & max depth <= 3
	if err := validateLootDAG(ds.LootTables); err != nil {
		return err
	}

	for _, ec := range ds.EnhanceCurve {
		if _, ok := itemMap[ec.MaterialItemID]; !ok {
			return fmt.Errorf("[Gate 2: RI] core/enhance_curves.json | record=level_%d | field=material_item_id: missing item %q in core/items.json", ec.TargetLevel, ec.MaterialItemID)
		}
	}

	for _, ps := range ds.PityShop {
		tok, ok := itemMap[ps.TokenItemID]
		if !ok {
			return fmt.Errorf("[Gate 2: RI] core/pity_shop.json | record=%s | field=token_item_id: missing token %q in core/items.json", ps.EntryID, ps.TokenItemID)
		}
		if tok.ItemType != "BOSS_TOKEN" {
			return fmt.Errorf("[Gate 2: RI] core/pity_shop.json | record=%s | field=token_item_id: %q has item_type=%q, want BOSS_TOKEN", ps.EntryID, ps.TokenItemID, tok.ItemType)
		}
		if _, ok := itemMap[ps.RewardItemID]; !ok {
			return fmt.Errorf("[Gate 2: RI] core/pity_shop.json | record=%s | field=reward_item_id: missing item %q in core/items.json", ps.EntryID, ps.RewardItemID)
		}
	}

	for _, z := range ds.Zones {
		for _, sz := range z.Subzones {
			for _, sp := range sz.Spawns {
				if _, ok := monsterMap[sp.EntityID]; !ok {
					return fmt.Errorf("[Gate 2: RI] core/zones.json | record=%s/%s | field=spawns.entity_id: missing entity %q in core/monsters.json", z.ZoneID, sz.SubzoneID, sp.EntityID)
				}
			}
		}
	}

	// Pack Consistency & Completeness
	if err := validatePackCompleteness(ds); err != nil {
		return err
	}

	return nil
}

func validateLootDAG(tables []LootTableDef) error {
	adj := make(map[string][]string, len(tables))
	tableIDs := make([]string, 0, len(tables))
	for _, lt := range tables {
		tableIDs = append(tableIDs, lt.LootTableID)
		for _, g := range lt.RollGroups {
			for _, e := range g.Entries {
				if e.EntryType == "SUB_TABLE" && e.RefID != nil {
					adj[lt.LootTableID] = append(adj[lt.LootTableID], *e.RefID)
				}
			}
		}
	}
	sort.Strings(tableIDs)

	const (
		unvisited = 0
		visiting  = 1
		done      = 2
	)
	state := make(map[string]int, len(tables))
	depthMemo := make(map[string]int, len(tables))
	var stack []string

	var dfs func(u string) (int, error)
	dfs = func(u string) (int, error) {
		switch state[u] {
		case visiting:
			cyclePath := append(append([]string(nil), stack...), u)
			return 0, fmt.Errorf("[Gate 2: LootDAG] core/loot_tables.json | record=%s | field=roll_groups: cyclic SUB_TABLE reference detected (%s)",
				u, strings.Join(cyclePath, " -> "))
		case done:
			return depthMemo[u], nil
		}

		state[u] = visiting
		stack = append(stack, u)

		maxChildDepth := 0
		hasSub := false
		for _, v := range adj[u] {
			hasSub = true
			d, err := dfs(v)
			if err != nil {
				return 0, err
			}
			if d > maxChildDepth {
				maxChildDepth = d
			}
		}

		stack = stack[:len(stack)-1]
		state[u] = done

		d := 0
		if hasSub {
			d = 1 + maxChildDepth
		}
		if d > 3 {
			return 0, fmt.Errorf("[Gate 2: LootDAG] core/loot_tables.json | record=%s | field=roll_groups: sub-table nesting depth=%d exceeds maximum allowed depth 3", u, d)
		}
		depthMemo[u] = d
		return d, nil
	}

	for _, id := range tableIDs {
		if state[id] == unvisited {
			if _, err := dfs(id); err != nil {
				return err
			}
		}
	}
	return nil
}

func validatePackCompleteness(ds *LoadedDataset) error {
	if ds.StringsVI.PackID != ds.PackManifest.PackID {
		return fmt.Errorf("[Gate 2: Pack] packs/%s/strings_vi.json | field=pack_id: %q != manifest pack_id %q",
			ds.PackName, ds.StringsVI.PackID, ds.PackManifest.PackID)
	}
	if ds.StringsEN.PackID != ds.PackManifest.PackID {
		return fmt.Errorf("[Gate 2: Pack] packs/%s/strings_en.json | field=pack_id: %q != manifest pack_id %q",
			ds.PackName, ds.StringsEN.PackID, ds.PackManifest.PackID)
	}
	if ds.AssetBind.PackID != ds.PackManifest.PackID {
		return fmt.Errorf("[Gate 2: Pack] packs/%s/asset_bindings.json | field=pack_id: %q != manifest pack_id %q",
			ds.PackName, ds.AssetBind.PackID, ds.PackManifest.PackID)
	}

	viMap := make(map[string]LocalizedStringEntryDef, len(ds.StringsVI.Entries))
	for i, e := range ds.StringsVI.Entries {
		if _, dup := viMap[e.CanonicalID]; dup {
			return fmt.Errorf("[Gate 2: Pack] packs/%s/strings_vi.json | record=%s (entries[%d]): duplicate canonical_id", ds.PackName, e.CanonicalID, i)
		}
		if strings.TrimSpace(e.DisplayName) == "" {
			return fmt.Errorf("[Gate 2: Pack] packs/%s/strings_vi.json | record=%s | field=display_name: empty display_name", ds.PackName, e.CanonicalID)
		}
		viMap[e.CanonicalID] = e
	}

	enMap := make(map[string]LocalizedStringEntryDef, len(ds.StringsEN.Entries))
	for i, e := range ds.StringsEN.Entries {
		if _, dup := enMap[e.CanonicalID]; dup {
			return fmt.Errorf("[Gate 2: Pack] packs/%s/strings_en.json | record=%s (entries[%d]): duplicate canonical_id", ds.PackName, e.CanonicalID, i)
		}
		if strings.TrimSpace(e.DisplayName) == "" {
			return fmt.Errorf("[Gate 2: Pack] packs/%s/strings_en.json | record=%s | field=display_name: empty display_name", ds.PackName, e.CanonicalID)
		}
		enMap[e.CanonicalID] = e
	}

	abMap := make(map[string]AssetBindingEntryDef, len(ds.AssetBind.Entries))
	for i, e := range ds.AssetBind.Entries {
		if _, dup := abMap[e.CanonicalID]; dup {
			return fmt.Errorf("[Gate 2: Pack] packs/%s/asset_bindings.json | record=%s (entries[%d]): duplicate canonical_id", ds.PackName, e.CanonicalID, i)
		}
		abMap[e.CanonicalID] = e
	}

	coreIDs := collectCoreCanonicalIDs(ds)
	for _, cid := range coreIDs {
		if _, ok := viMap[cid]; !ok {
			return fmt.Errorf("[Gate 2: Pack] packs/%s/strings_vi.json | record=%s: missing localization entry for core canonical_id %q", ds.PackName, cid, cid)
		}
		if _, ok := enMap[cid]; !ok {
			return fmt.Errorf("[Gate 2: Pack] packs/%s/strings_en.json | record=%s: missing localization entry for core canonical_id %q", ds.PackName, cid, cid)
		}
		if _, ok := abMap[cid]; !ok {
			return fmt.Errorf("[Gate 2: Pack] packs/%s/asset_bindings.json | record=%s: missing asset binding entry for core canonical_id %q", ds.PackName, cid, cid)
		}
	}
	return nil
}

func collectCoreCanonicalIDs(ds *LoadedDataset) []string {
	seen := make(map[string]bool)
	var walk func(node any)
	walk = func(node any) {
		switch v := node.(type) {
		case map[string]any:
			for _, field := range []string{
				"race_id", "passive_id", "skill_id", "form_id",
				"entity_id", "item_id", "set_id", "affix_id",
				"loot_table_id", "zone_id", "planet_id", "subzone_id",
				"innate_effect_id", "effect_id", "canonical_id",
			} {
				if s, ok := v[field].(string); ok && coreExtractCanonicalRegex.MatchString(s) {
					seen[s] = true
				}
			}
			for k, val := range v {
				if coreExtractCanonicalRegex.MatchString(k) {
					seen[k] = true
				}
				walk(val)
			}
		case []any:
			for _, elem := range v {
				walk(elem)
			}
		}
	}
	for _, fname := range requiredCoreFiles {
		if cf, ok := ds.CoreFiles[fname]; ok {
			walk(cf.Data)
		}
	}
	out := make([]string, 0, len(seen))
	for id := range seen {
		out = append(out, id)
	}
	sort.Strings(out)
	return out
}
