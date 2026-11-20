package main

import (
	"fmt"
	"sort"
)

func validateMath(ds *LoadedDataset) error {
	// 1. Loot Table weights & entry bounds
	for _, lt := range ds.LootTables {
		if len(lt.RollGroups) == 0 {
			return fmt.Errorf("[Gate 3: Math] core/loot_tables.json | record=%s | field=roll_groups: must have >= 1 roll_group", lt.LootTableID)
		}
		for gi, g := range lt.RollGroups {
			if g.GroupTriggerChance < 0 || g.GroupTriggerChance > 1 {
				return fmt.Errorf("[Gate 3: Math] core/loot_tables.json | record=%s | field=roll_groups[%d].group_trigger_chance: %v out of [0,1]",
					lt.LootTableID, gi, g.GroupTriggerChance)
			}
			if g.PickCount < 1 || g.PickCount > 10 {
				return fmt.Errorf("[Gate 3: Math] core/loot_tables.json | record=%s | field=roll_groups[%d].pick_count: %d out of [1,10]",
					lt.LootTableID, gi, g.PickCount)
			}
			if len(g.Entries) == 0 {
				return fmt.Errorf("[Gate 3: Math] core/loot_tables.json | record=%s | field=roll_groups[%d].entries: must have >= 1 entry",
					lt.LootTableID, gi)
			}
			totalWeight := 0
			for ei, e := range g.Entries {
				if e.Weight < 1 || e.Weight > 100000 {
					return fmt.Errorf("[Gate 3: Math] core/loot_tables.json | record=%s | field=roll_groups[%d].entries[%d].weight (group_id=%s): weight=%d must be in [1, 100000]",
						lt.LootTableID, gi, ei, g.GroupID, e.Weight)
				}
				totalWeight += e.Weight
				switch e.EntryType {
				case "NOTHING":
					if e.RefID != nil {
						return fmt.Errorf("[Gate 3: Math] core/loot_tables.json | record=%s | field=roll_groups[%d].entries[%d].ref_id: NOTHING entry must have null ref_id",
							lt.LootTableID, gi, ei)
					}
					if e.MinQty != 0 || e.MaxQty != 0 {
						return fmt.Errorf("[Gate 3: Math] core/loot_tables.json | record=%s | field=roll_groups[%d].entries[%d]: NOTHING entry must have min_qty=0 and max_qty=0",
							lt.LootTableID, gi, ei)
					}
				case "ITEM", "SUB_TABLE":
					if e.RefID == nil || *e.RefID == "" {
						return fmt.Errorf("[Gate 3: Math] core/loot_tables.json | record=%s | field=roll_groups[%d].entries[%d].ref_id: %s entry requires ref_id",
							lt.LootTableID, gi, ei, e.EntryType)
					}
					if e.MinQty < 1 {
						return fmt.Errorf("[Gate 3: Math] core/loot_tables.json | record=%s | field=roll_groups[%d].entries[%d].min_qty: %s entry min_qty=%d must be >= 1",
							lt.LootTableID, gi, ei, e.EntryType, e.MinQty)
					}
					if e.MinQty > e.MaxQty {
						return fmt.Errorf("[Gate 3: Math] core/loot_tables.json | record=%s | field=roll_groups[%d].entries[%d]: min_qty (%d) > max_qty (%d)",
							lt.LootTableID, gi, ei, e.MinQty, e.MaxQty)
					}
				}
			}
			if totalWeight <= 0 {
				return fmt.Errorf("[Gate 3: Math] core/loot_tables.json | record=%s | field=roll_groups[%d].entries (group_id=%s): total weight=%d must be > 0",
					lt.LootTableID, gi, g.GroupID, totalWeight)
			}
		}
	}

	// 2. Affix bounds
	for _, a := range ds.Affixes {
		if a.RollWeight < 1 || a.RollWeight > 10000 {
			return fmt.Errorf("[Gate 3: Math] core/affix_pools.json | record=%s | field=roll_weight: %d out of [1,10000]", a.AffixID, a.RollWeight)
		}
		for ti, t := range a.Tiers {
			if t.MinItemLevel > t.MaxItemLevel {
				return fmt.Errorf("[Gate 3: Math] core/affix_pools.json | record=%s | field=tiers[%d]: min_item_level (%d) > max_item_level (%d)",
					a.AffixID, ti, t.MinItemLevel, t.MaxItemLevel)
			}
			if t.MinValue > t.MaxValue {
				return fmt.Errorf("[Gate 3: Math] core/affix_pools.json | record=%s | field=tiers[%d]: min_value (%v) > max_value (%v)",
					a.AffixID, ti, t.MinValue, t.MaxValue)
			}
			if t.Step <= 0 {
				return fmt.Errorf("[Gate 3: Math] core/affix_pools.json | record=%s | field=tiers[%d].step: %v must be > 0",
					a.AffixID, ti, t.Step)
			}
		}
	}

	// 3. Item equipment consistency
	for _, it := range ds.Items {
		if it.ItemType == "EQUIPMENT" {
			if it.EquipSpec == nil {
				return fmt.Errorf("[Gate 3: Math] core/items.json | record=%s | field=equip_spec: EQUIPMENT item must have non-null equip_spec", it.ItemID)
			}
			if it.MaxStack != 1 {
				return fmt.Errorf("[Gate 3: Math] core/items.json | record=%s | field=max_stack: EQUIPMENT item must have max_stack=1 (got %d)", it.ItemID, it.MaxStack)
			}
		} else if it.EquipSpec != nil {
			return fmt.Errorf("[Gate 3: Math] core/items.json | record=%s | field=equip_spec: non-EQUIPMENT item (%s) must have null equip_spec", it.ItemID, it.ItemType)
		}
	}

	// 4. Monster rewards & phases
	for _, m := range ds.Monsters {
		if m.Rewards.CoinMin > m.Rewards.CoinMax {
			return fmt.Errorf("[Gate 3: Math] core/monsters.json | record=%s | field=rewards: coin_min (%d) > coin_max (%d)",
				m.EntityID, m.Rewards.CoinMin, m.Rewards.CoinMax)
		}
		if m.Rewards.GuaranteedPityTokenMin > m.Rewards.GuaranteedPityTokenMax {
			return fmt.Errorf("[Gate 3: Math] core/monsters.json | record=%s | field=rewards: guaranteed_pity_token_min (%d) > max (%d)",
				m.EntityID, m.Rewards.GuaranteedPityTokenMin, m.Rewards.GuaranteedPityTokenMax)
		}
		seenPhase := make(map[int]bool, len(m.Phases))
		for pi, ph := range m.Phases {
			if seenPhase[ph.PhaseIndex] {
				return fmt.Errorf("[Gate 3: Math] core/monsters.json | record=%s | field=phases[%d].phase_index: duplicate phase_index %d",
					m.EntityID, pi, ph.PhaseIndex)
			}
			seenPhase[ph.PhaseIndex] = true
		}
	}

	// 5. Monotonic EXP Table
	if len(ds.ExpTable) == 0 {
		return fmt.Errorf("[Gate 3: Math] core/exp_table.json: empty exp_table")
	}
	sortedExp := make([]ExpLevelDef, len(ds.ExpTable))
	copy(sortedExp, ds.ExpTable)
	sort.Slice(sortedExp, func(i, j int) bool { return sortedExp[i].Level < sortedExp[j].Level })

	prevExpToNext := 0
	prevCum := 0
	for i, row := range sortedExp {
		expectedLv := i + 1
		if row.Level != expectedLv {
			return fmt.Errorf("[Gate 3: Math] core/exp_table.json | record=level_%d | field=level: expected contiguous level %d, got %d",
				row.Level, expectedLv, row.Level)
		}
		isCap := i == len(sortedExp)-1
		if isCap {
			if row.ExpToNext != 0 {
				return fmt.Errorf("[Gate 3: Math] core/exp_table.json | record=level_%d | field=exp_to_next: cap level %d must have exp_to_next=0 (got %d)",
					row.Level, row.Level, row.ExpToNext)
			}
		} else {
			if row.ExpToNext <= 0 {
				return fmt.Errorf("[Gate 3: Math] core/exp_table.json | record=level_%d | field=exp_to_next: level %d exp_to_next=%d must be > 0",
					row.Level, row.Level, row.ExpToNext)
			}
			if row.ExpToNext < prevExpToNext {
				return fmt.Errorf("[Gate 3: Math] core/exp_table.json | record=level_%d | field=exp_to_next: non-monotonic curve (%d < previous %d)",
					row.Level, row.ExpToNext, prevExpToNext)
			}
			prevExpToNext = row.ExpToNext
		}
		if row.CumulativeExp < prevCum {
			return fmt.Errorf("[Gate 3: Math] core/exp_table.json | record=level_%d | field=cumulative_exp: non-monotonic cumulative_exp (%d < %d)",
				row.Level, row.CumulativeExp, prevCum)
		}
		prevCum = row.CumulativeExp
	}

	// 6. Enhance Curves
	sortedEnh := make([]EnhanceTierDef, len(ds.EnhanceCurve))
	copy(sortedEnh, ds.EnhanceCurve)
	sort.Slice(sortedEnh, func(i, j int) bool { return sortedEnh[i].TargetLevel < sortedEnh[j].TargetLevel })
	prevBonus := 0.0
	for i, ec := range sortedEnh {
		if ec.TargetLevel != i+1 {
			return fmt.Errorf("[Gate 3: Math] core/enhance_curves.json | record=level_%d | field=target_level: expected contiguous level %d, got %d",
				ec.TargetLevel, i+1, ec.TargetLevel)
		}
		if ec.CumulativeStatBonusPct <= prevBonus {
			return fmt.Errorf("[Gate 3: Math] core/enhance_curves.json | record=level_%d | field=cumulative_stat_bonus_pct: must strictly increase (%v <= %v)",
				ec.TargetLevel, ec.CumulativeStatBonusPct, prevBonus)
		}
		prevBonus = ec.CumulativeStatBonusPct
		if ec.DowngradeFloorLevel > ec.TargetLevel {
			return fmt.Errorf("[Gate 3: Math] core/enhance_curves.json | record=level_%d | field=downgrade_floor_level: %d > target_level %d",
				ec.TargetLevel, ec.DowngradeFloorLevel, ec.TargetLevel)
		}
	}

	// 7. Zones
	for _, z := range ds.Zones {
		if z.RecommendedLevelMin > z.RecommendedLevelMax {
			return fmt.Errorf("[Gate 3: Math] core/zones.json | record=%s | field=recommended_level_min: %d > max %d",
				z.ZoneID, z.RecommendedLevelMin, z.RecommendedLevelMax)
		}
		if z.SoftCapCCU > z.HardCapCCU {
			return fmt.Errorf("[Gate 3: Math] core/zones.json | record=%s | field=soft_cap_ccu: %d > hard_cap_ccu %d",
				z.ZoneID, z.SoftCapCCU, z.HardCapCCU)
		}
	}

	// 8. Global Constants
	if ds.GlobalConst.BaseCritRate > ds.GlobalConst.CritRateCap {
		return fmt.Errorf("[Gate 3: Math] core/global_constants.json | field=base_crit_rate: %v > crit_rate_cap %v",
			ds.GlobalConst.BaseCritRate, ds.GlobalConst.CritRateCap)
	}

	return nil
}
