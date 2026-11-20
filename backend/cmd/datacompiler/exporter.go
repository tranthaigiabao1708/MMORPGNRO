package main

import (
	"bytes"
	"encoding/csv"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/infinite-universe/backend/pkg/gamedata"
)

func computeCoreHash(coreDir string) (string, error) {
	return gamedata.ComputeCanonicalGameDataHash(coreDir)
}

func exportArtifacts(ds *LoadedDataset, outDir, locale string) (string, string, error) {
	coreHash, err := gamedata.ComputeCanonicalGameDataHash(ds.CoreDir)
	if err != nil {
		return "", "", fmt.Errorf("[Gate 4: Export] compute core hash: %w", err)
	}
	packHash, err := gamedata.ComputeCanonicalGameDataHash(ds.PackDir)
	if err != nil {
		return "", "", fmt.Errorf("[Gate 4: Export] compute pack hash: %w", err)
	}

	backendOut := filepath.Join(outDir, "backend")
	unrealOut := filepath.Join(outDir, "unreal")
	if err := os.MkdirAll(backendOut, 0o755); err != nil {
		return "", "", fmt.Errorf("[Gate 4: Export] mkdir %s: %w", backendOut, err)
	}
	if err := os.MkdirAll(unrealOut, 0o755); err != nil {
		return "", "", fmt.Errorf("[Gate 4: Export] mkdir %s: %w", unrealOut, err)
	}

	// 1. Write gamedata_bundle.sha256
	shaPath := filepath.Join(backendOut, "gamedata_bundle.sha256")
	if err := os.WriteFile(shaPath, []byte(coreHash+"\n"), 0o644); err != nil {
		return "", "", fmt.Errorf("[Gate 4: Export] write %s: %w", shaPath, err)
	}

	// 2. Write gamedata_bundle.json (all 13 required sections + zones)
	bundle := buildBackendBundle(ds, coreHash, packHash)
	if err := writeJSONFile(filepath.Join(backendOut, "gamedata_bundle.json"), bundle); err != nil {
		return "", "", err
	}

	// 3. Export all 8 UE5 UDataTable JSON + CSV files
	if err := exportUERaces(unrealOut, ds.Races); err != nil {
		return "", "", err
	}
	if err := exportUESkills(unrealOut, ds.Skills); err != nil {
		return "", "", err
	}
	if err := exportUETransformations(unrealOut, ds.Forms); err != nil {
		return "", "", err
	}
	if err := exportUEItems(unrealOut, ds.Items); err != nil {
		return "", "", err
	}
	if err := exportUEAffixes(unrealOut, ds.Affixes); err != nil {
		return "", "", err
	}
	if err := exportUEMonsters(unrealOut, ds.Monsters); err != nil {
		return "", "", err
	}
	if err := exportUELootTables(unrealOut, ds.LootTables); err != nil {
		return "", "", err
	}
	if err := exportUEPresentationPack(unrealOut, ds, locale); err != nil {
		return "", "", err
	}

	return coreHash, packHash, nil
}

func buildBackendBundle(ds *LoadedDataset, coreHash, packHash string) map[string]any {
	racesMap := make(map[string]RaceDef, len(ds.Races))
	for _, r := range ds.Races {
		racesMap[r.RaceID] = r
	}
	skillsMap := make(map[string]SkillDef, len(ds.Skills))
	for _, s := range ds.Skills {
		skillsMap[s.SkillID] = s
	}
	formsMap := make(map[string]TransformationDef, len(ds.Forms))
	for _, f := range ds.Forms {
		formsMap[f.FormID] = f
	}
	itemsMap := make(map[string]ItemDef, len(ds.Items))
	for _, it := range ds.Items {
		itemsMap[it.ItemID] = it
	}
	setsMap := make(map[string]ItemSetDef, len(ds.ItemSets))
	for _, is := range ds.ItemSets {
		setsMap[is.SetID] = is
	}
	affixesMap := make(map[string]AffixDef, len(ds.Affixes))
	for _, a := range ds.Affixes {
		affixesMap[a.AffixID] = a
	}
	monstersMap := make(map[string]MonsterDef, len(ds.Monsters))
	for _, m := range ds.Monsters {
		monstersMap[m.EntityID] = m
	}
	lootMap := make(map[string]LootTableDef, len(ds.LootTables))
	for _, lt := range ds.LootTables {
		lootMap[lt.LootTableID] = lt
	}
	zonesMap := make(map[string]ZoneDef, len(ds.Zones))
	for _, z := range ds.Zones {
		zonesMap[z.ZoneID] = z
	}

	return map[string]any{
		"manifest": map[string]any{
			"schema_version":    "2.0.0",
			"content_version":   ds.PackManifest.PackVersion,
			"built_at_utc":      time.Now().UTC().Format(time.RFC3339),
			"gamedata_hash":     coreHash,
			"presentation_pack": ds.PackName,
			"presentation_hash": packHash,
		},
		"global_combat_constants": ds.GlobalConst,
		"exp_table":               ds.ExpTable,
		"races":                   racesMap,
		"skills":                  skillsMap,
		"transformations":         formsMap,
		"items":                   itemsMap,
		"item_sets":               setsMap,
		"affixes":                 affixesMap,
		"monsters_and_bosses":     monstersMap,
		"loot_tables":             lootMap,
		"enhance_curves":          ds.EnhanceCurve,
		"pity_shop_entries":       ds.PityShop,
		"zones":                   zonesMap,
	}
}

func exportUERaces(unrealOut string, races []RaceDef) error {
	sorted := append([]RaceDef(nil), races...)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].RaceID < sorted[j].RaceID })

	jsonRows := make([]map[string]any, 0, len(sorted))
	csvRows := make([][]string, 0, len(sorted))
	headers := []string{
		"Name", "RaceId", "GameplayTag", "ScopeStatus",
		"BaseStatsLv1", "GrowthPerLevel", "TierBonusEvery5Levels",
		"InnatePassive", "StarterSkillIds", "UnlockableFormIds",
	}

	for _, r := range sorted {
		row := map[string]any{
			"Name":                  r.RaceID,
			"RaceId":                r.RaceID,
			"GameplayTag":           ueGameplayTag(r.GameplayTag),
			"ScopeStatus":           r.ScopeStatus,
			"BaseStatsLv1":          toPascalKeys(r.BaseStatsLv1),
			"GrowthPerLevel":        toPascalKeys(r.GrowthPerLevel),
			"TierBonusEvery5Levels": toPascalKeys(r.TierBonusEvery5Level),
			"InnatePassive": map[string]any{
				"PassiveId":           r.InnatePassive.PassiveID,
				"TriggerHpRatio":      r.InnatePassive.TriggerHPRatio,
				"DurationSec":         r.InnatePassive.DurationSec,
				"InternalCooldownSec": r.InnatePassive.InternalCooldownSec,
				"Modifiers":           ueModifiers(r.InnatePassive.Modifiers),
			},
			"StarterSkillIds":   r.StarterSkillIDs,
			"UnlockableFormIds": r.UnlockableFormIDs,
		}
		jsonRows = append(jsonRows, row)
		csvRows = append(csvRows, []string{
			r.RaceID,
			r.RaceID,
			ueTagString(r.GameplayTag),
			r.ScopeStatus,
			ueStructText(row["BaseStatsLv1"]),
			ueStructText(row["GrowthPerLevel"]),
			ueStructText(row["TierBonusEvery5Levels"]),
			ueStructText(row["InnatePassive"]),
			ueStringListText(r.StarterSkillIDs),
			ueStringListText(r.UnlockableFormIDs),
		})
	}
	return writeUETable(unrealOut, "DT_Races", jsonRows, headers, csvRows)
}

func exportUESkills(unrealOut string, skills []SkillDef) error {
	sorted := append([]SkillDef(nil), skills...)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].SkillID < sorted[j].SkillID })

	jsonRows := make([]map[string]any, 0, len(sorted))
	csvRows := make([][]string, 0, len(sorted))
	headers := []string{
		"Name", "SkillId", "AbilityTag", "Category", "AllowedRaces",
		"UnlockLevel", "MasteryPointCost", "bCanCastInAir", "bCanCastWhileHitstunned",
		"ResourceCost", "Timing", "TargetingAndHitbox", "DamageProfile",
	}

	for _, s := range sorted {
		row := map[string]any{
			"Name":                    s.SkillID,
			"SkillId":                 s.SkillID,
			"AbilityTag":              ueGameplayTag(s.AbilityTag),
			"Category":                s.Category,
			"AllowedRaces":            s.AllowedRaces,
			"UnlockLevel":             s.UnlockLevel,
			"MasteryPointCost":        s.MasteryPointCost,
			"bCanCastInAir":           s.CanCastInAir,
			"bCanCastWhileHitstunned": s.CanCastWhileHitstun,
			"ResourceCost":            toPascalKeys(s.ResourceCost),
			"Timing":                  toPascalKeys(s.Timing),
			"TargetingAndHitbox":      toPascalKeys(s.TargetingAndHitbox),
			"DamageProfile":           toPascalKeys(s.DamageProfile),
		}
		jsonRows = append(jsonRows, row)
		csvRows = append(csvRows, []string{
			s.SkillID,
			s.SkillID,
			ueTagString(s.AbilityTag),
			s.Category,
			ueStringListText(s.AllowedRaces),
			strconv.Itoa(s.UnlockLevel),
			strconv.Itoa(s.MasteryPointCost),
			ueBool(s.CanCastInAir),
			ueBool(s.CanCastWhileHitstun),
			ueStructText(row["ResourceCost"]),
			ueStructText(row["Timing"]),
			ueStructText(row["TargetingAndHitbox"]),
			ueStructText(row["DamageProfile"]),
		})
	}
	return writeUETable(unrealOut, "DT_Skills", jsonRows, headers, csvRows)
}

func exportUETransformations(unrealOut string, forms []TransformationDef) error {
	sorted := append([]TransformationDef(nil), forms...)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].FormID < sorted[j].FormID })

	jsonRows := make([]map[string]any, 0, len(sorted))
	csvRows := make([][]string, 0, len(sorted))
	headers := []string{
		"Name", "FormId", "StateTag", "RequiredRaceId", "PrerequisiteFormId",
		"UnlockLevel", "MasteryPointCost", "MinEnergyToActivate", "ActivationCastSec",
		"ActivationKnockbackRadiusCm", "ToggleOffIcdSec", "ExhaustionPenaltySec",
		"FormDamageMultiplier", "BaseEnergyDrainPerSec", "BaseHpDrainRatioPerSec",
		"StatBonuses",
	}

	for _, f := range sorted {
		prereq := ueFNamePtr(f.PrerequisiteFormID)
		mods := ueModifiers(f.StatBonuses)
		row := map[string]any{
			"Name":                        f.FormID,
			"FormId":                      f.FormID,
			"StateTag":                    ueGameplayTag(f.StateTag),
			"RequiredRaceId":              f.RequiredRaceID,
			"PrerequisiteFormId":          prereq,
			"UnlockLevel":                 f.UnlockLevel,
			"MasteryPointCost":            f.MasteryPointCost,
			"MinEnergyToActivate":         f.MinEnergyToActivate,
			"ActivationCastSec":           f.ActivationCastSec,
			"ActivationKnockbackRadiusCm": f.ActivationKnockbackRadiusCm,
			"ToggleOffIcdSec":             f.ToggleOffICDSec,
			"ExhaustionPenaltySec":        f.ExhaustionPenaltySec,
			"FormDamageMultiplier":        f.FormDamageMultiplier,
			"BaseEnergyDrainPerSec":       f.BaseEnergyDrainPerSec,
			"BaseHpDrainRatioPerSec":      f.BaseHPDrainRatioPerSec,
			"StatBonuses":                 mods,
		}
		jsonRows = append(jsonRows, row)
		csvRows = append(csvRows, []string{
			f.FormID,
			f.FormID,
			ueTagString(f.StateTag),
			f.RequiredRaceID,
			prereq,
			strconv.Itoa(f.UnlockLevel),
			strconv.Itoa(f.MasteryPointCost),
			ueFloat(f.MinEnergyToActivate),
			ueFloat(f.ActivationCastSec),
			ueFloat(f.ActivationKnockbackRadiusCm),
			ueFloat(f.ToggleOffICDSec),
			ueFloat(f.ExhaustionPenaltySec),
			ueFloat(f.FormDamageMultiplier),
			ueFloat(f.BaseEnergyDrainPerSec),
			ueFloat(f.BaseHPDrainRatioPerSec),
			ueArrayOfStructsText(mods),
		})
	}
	return writeUETable(unrealOut, "DT_Transformations", jsonRows, headers, csvRows)
}

func exportUEItems(unrealOut string, items []ItemDef) error {
	sorted := append([]ItemDef(nil), items...)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].ItemID < sorted[j].ItemID })

	jsonRows := make([]map[string]any, 0, len(sorted))
	csvRows := make([][]string, 0, len(sorted))
	headers := []string{
		"Name", "ItemId", "ItemType", "Rarity", "RequiredLevel",
		"MaxStack", "bIsTradeable", "SellPriceCoin", "EquipSpec",
	}

	for _, it := range sorted {
		var equipObj map[string]any
		if it.EquipSpec != nil {
			baseStats := make([]map[string]any, 0, len(it.EquipSpec.BaseStats))
			for _, bs := range it.EquipSpec.BaseStats {
				baseStats = append(baseStats, map[string]any{
					"AttrId": bs.AttrID,
					"Value":  bs.Value,
				})
			}
			equipObj = map[string]any{
				"bIsEquippable":    true,
				"Slot":             it.EquipSpec.Slot,
				"AllowedRaces":     it.EquipSpec.AllowedRaces,
				"MaxEnhanceLevel":  it.EquipSpec.MaxEnhanceLevel,
				"RandomAffixCount": it.EquipSpec.RandomAffixCount,
				"AffixTierPool":    it.EquipSpec.AffixTierPool,
				"SetId":            ueFNamePtr(it.EquipSpec.SetID),
				"BaseStats":        baseStats,
				"InnateEffectId":   ueFNamePtr(it.EquipSpec.InnateEffectID),
			}
		} else {
			equipObj = map[string]any{
				"bIsEquippable":    false,
				"Slot":             "slot.body",
				"AllowedRaces":     []string{},
				"MaxEnhanceLevel":  0,
				"RandomAffixCount": 0,
				"AffixTierPool":    1,
				"SetId":            "None",
				"BaseStats":        []map[string]any{},
				"InnateEffectId":   "None",
			}
		}

		row := map[string]any{
			"Name":          it.ItemID,
			"ItemId":        it.ItemID,
			"ItemType":      it.ItemType,
			"Rarity":        it.Rarity,
			"RequiredLevel": it.RequiredLevel,
			"MaxStack":      it.MaxStack,
			"bIsTradeable":  it.IsTradeable,
			"SellPriceCoin": it.SellPriceCoin,
			"EquipSpec":     equipObj,
		}
		jsonRows = append(jsonRows, row)
		csvRows = append(csvRows, []string{
			it.ItemID,
			it.ItemID,
			it.ItemType,
			it.Rarity,
			strconv.Itoa(it.RequiredLevel),
			strconv.Itoa(it.MaxStack),
			ueBool(it.IsTradeable),
			strconv.FormatInt(it.SellPriceCoin, 10),
			ueStructText(equipObj),
		})
	}
	return writeUETable(unrealOut, "DT_Items", jsonRows, headers, csvRows)
}

func exportUEAffixes(unrealOut string, affixes []AffixDef) error {
	sorted := append([]AffixDef(nil), affixes...)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].AffixID < sorted[j].AffixID })

	jsonRows := make([]map[string]any, 0, len(sorted))
	csvRows := make([][]string, 0, len(sorted))
	headers := []string{
		"Name", "AffixId", "TargetAttrId", "ModOp", "ExclusiveGroup",
		"RollWeight", "AllowedSlots", "Tiers",
	}

	for _, a := range sorted {
		tiers := make([]map[string]any, 0, len(a.Tiers))
		for _, t := range a.Tiers {
			tiers = append(tiers, map[string]any{
				"Tier":         t.Tier,
				"MinItemLevel": t.MinItemLevel,
				"MaxItemLevel": t.MaxItemLevel,
				"MinValue":     t.MinValue,
				"MaxValue":     t.MaxValue,
				"Step":         t.Step,
			})
		}
		row := map[string]any{
			"Name":           a.AffixID,
			"AffixId":        a.AffixID,
			"TargetAttrId":   a.TargetAttrID,
			"ModOp":          a.ModOp,
			"ExclusiveGroup": a.ExclusiveGroup,
			"RollWeight":     a.RollWeight,
			"AllowedSlots":   a.AllowedSlots,
			"Tiers":          tiers,
		}
		jsonRows = append(jsonRows, row)
		csvRows = append(csvRows, []string{
			a.AffixID,
			a.AffixID,
			a.TargetAttrID,
			a.ModOp,
			a.ExclusiveGroup,
			strconv.Itoa(a.RollWeight),
			ueStringListText(a.AllowedSlots),
			ueArrayOfStructsText(tiers),
		})
	}
	return writeUETable(unrealOut, "DT_Affixes", jsonRows, headers, csvRows)
}

func exportUEMonsters(unrealOut string, monsters []MonsterDef) error {
	sorted := append([]MonsterDef(nil), monsters...)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].EntityID < sorted[j].EntityID })

	jsonRows := make([]map[string]any, 0, len(sorted))
	csvRows := make([][]string, 0, len(sorted))
	headers := []string{
		"Name", "EntityId", "Rank", "Archetype", "Level", "bCanFly3D",
		"CombatStats", "PoiseSpec", "AiPerception", "Rewards", "Phases",
	}

	for _, m := range sorted {
		rewards := map[string]any{
			"BaseExp":                m.Rewards.BaseEXP,
			"CoinMin":                m.Rewards.CoinMin,
			"CoinMax":                m.Rewards.CoinMax,
			"LootTableId":            m.Rewards.LootTableID,
			"MinContributionRatio":   m.Rewards.MinContributionRatio,
			"GuaranteedPityTokenId":  ueFNamePtr(m.Rewards.GuaranteedPityTokenID),
			"GuaranteedPityTokenMin": m.Rewards.GuaranteedPityTokenMin,
			"GuaranteedPityTokenMax": m.Rewards.GuaranteedPityTokenMax,
		}
		phases := make([]map[string]any, 0, len(m.Phases))
		for _, p := range m.Phases {
			abilities := make([]map[string]any, 0, len(p.Abilities))
			for _, ab := range p.Abilities {
				abilities = append(abilities, toPascalKeys(ab).(map[string]any))
			}
			phases = append(phases, map[string]any{
				"PhaseIndex":     p.PhaseIndex,
				"EnterAtHpRatio": p.EnterAtHPRatio,
				"DamageMult":     p.DamageMult,
				"AtkSpeedMult":   p.AtkSpeedMult,
				"Abilities":      abilities,
			})
		}

		row := map[string]any{
			"Name":         m.EntityID,
			"EntityId":     m.EntityID,
			"Rank":         m.Rank,
			"Archetype":    m.Archetype,
			"Level":        m.Level,
			"bCanFly3D":    m.CanFly3D,
			"CombatStats":  toPascalKeys(m.CombatStats),
			"PoiseSpec":    toPascalKeys(m.PoiseSpec),
			"AiPerception": toPascalKeys(m.AIPerception),
			"Rewards":      rewards,
			"Phases":       phases,
		}
		jsonRows = append(jsonRows, row)
		csvRows = append(csvRows, []string{
			m.EntityID,
			m.EntityID,
			m.Rank,
			m.Archetype,
			strconv.Itoa(m.Level),
			ueBool(m.CanFly3D),
			ueStructText(row["CombatStats"]),
			ueStructText(row["PoiseSpec"]),
			ueStructText(row["AiPerception"]),
			ueStructText(rewards),
			ueArrayOfStructsText(phases),
		})
	}
	return writeUETable(unrealOut, "DT_MonstersAndBosses", jsonRows, headers, csvRows)
}

func exportUELootTables(unrealOut string, tables []LootTableDef) error {
	sorted := append([]LootTableDef(nil), tables...)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].LootTableID < sorted[j].LootTableID })

	jsonRows := make([]map[string]any, 0, len(sorted))
	csvRows := make([][]string, 0, len(sorted))
	headers := []string{
		"Name", "LootTableId", "LootDistributionMode", "RollGroups",
	}

	for _, lt := range sorted {
		groups := make([]map[string]any, 0, len(lt.RollGroups))
		for _, g := range lt.RollGroups {
			entries := make([]map[string]any, 0, len(g.Entries))
			for _, e := range g.Entries {
				entries = append(entries, map[string]any{
					"EntryType": e.EntryType,
					"RefId":     ueFNamePtr(e.RefID),
					"Weight":    e.Weight,
					"MinQty":    e.MinQty,
					"MaxQty":    e.MaxQty,
				})
			}
			groups = append(groups, map[string]any{
				"GroupId":            g.GroupID,
				"GroupTriggerChance": g.GroupTriggerChance,
				"PickCount":          g.PickCount,
				"Entries":            entries,
			})
		}
		row := map[string]any{
			"Name":                 lt.LootTableID,
			"LootTableId":          lt.LootTableID,
			"LootDistributionMode": lt.LootDistributionMode,
			"RollGroups":           groups,
		}
		jsonRows = append(jsonRows, row)
		csvRows = append(csvRows, []string{
			lt.LootTableID,
			lt.LootTableID,
			lt.LootDistributionMode,
			ueArrayOfStructsText(groups),
		})
	}
	return writeUETable(unrealOut, "DT_LootTables", jsonRows, headers, csvRows)
}

func exportUEPresentationPack(unrealOut string, ds *LoadedDataset, locale string) error {
	strMap := make(map[string]LocalizedStringEntryDef)
	fallbackMap := make(map[string]LocalizedStringEntryDef)
	if strings.EqualFold(locale, "en") {
		for _, e := range ds.StringsEN.Entries {
			strMap[e.CanonicalID] = e
		}
		for _, e := range ds.StringsVI.Entries {
			fallbackMap[e.CanonicalID] = e
		}
	} else {
		for _, e := range ds.StringsVI.Entries {
			strMap[e.CanonicalID] = e
		}
		for _, e := range ds.StringsEN.Entries {
			fallbackMap[e.CanonicalID] = e
		}
	}

	abMap := make(map[string]AssetBindingEntryDef, len(ds.AssetBind.Entries))
	allIDsSet := make(map[string]bool)
	for _, e := range ds.AssetBind.Entries {
		abMap[e.CanonicalID] = e
		allIDsSet[e.CanonicalID] = true
	}
	for id := range strMap {
		allIDsSet[id] = true
	}
	for id := range fallbackMap {
		allIDsSet[id] = true
	}

	allIDs := make([]string, 0, len(allIDsSet))
	for id := range allIDsSet {
		allIDs = append(allIDs, id)
	}
	sort.Strings(allIDs)

	jsonRows := make([]map[string]any, 0, len(allIDs))
	csvRows := make([][]string, 0, len(allIDs))
	headers := []string{
		"Name", "CanonicalId", "DisplayName", "ShortDescription", "LoreFlavorText",
		"IconTexture", "PrimarySkeletalMesh", "PrimaryMontage", "PrimaryVFX",
		"ImpactVFX", "CastSFX", "ImpactSFX", "ThemeTintColor",
	}

	for _, cid := range allIDs {
		se, ok := strMap[cid]
		if !ok {
			se = fallbackMap[cid]
		}
		ab := abMap[cid]
		tint := map[string]any{"R": 1.0, "G": 1.0, "B": 1.0, "A": 1.0}
		if ab.ThemeTintColor != nil {
			tint = map[string]any{
				"R": ab.ThemeTintColor.R,
				"G": ab.ThemeTintColor.G,
				"B": ab.ThemeTintColor.B,
				"A": ab.ThemeTintColor.A,
			}
		}

		icon := ueSoftPtr(ab.IconTexture)
		mesh := ueSoftPtr(ab.PrimarySkeletalMesh)
		mont := ueSoftPtr(ab.PrimaryMontage)
		pvfx := ueSoftPtr(ab.PrimaryVFX)
		ivfx := ueSoftPtr(ab.ImpactVFX)
		csfx := ueSoftPtr(ab.CastSFX)
		isfx := ueSoftPtr(ab.ImpactSFX)

		row := map[string]any{
			"Name":                cid,
			"CanonicalId":         cid,
			"DisplayName":         se.DisplayName,
			"ShortDescription":    se.ShortDescription,
			"LoreFlavorText":      se.LoreFlavorText,
			"IconTexture":         icon,
			"PrimarySkeletalMesh": mesh,
			"PrimaryMontage":      mont,
			"PrimaryVFX":          pvfx,
			"ImpactVFX":           ivfx,
			"CastSFX":             csfx,
			"ImpactSFX":           isfx,
			"ThemeTintColor":      tint,
		}
		jsonRows = append(jsonRows, row)
		csvRows = append(csvRows, []string{
			cid,
			cid,
			se.DisplayName,
			se.ShortDescription,
			se.LoreFlavorText,
			icon,
			mesh,
			mont,
			pvfx,
			ivfx,
			csfx,
			isfx,
			ueStructText(tint),
		})
	}

	return writeUETable(unrealOut, "DT_PresentationPack", jsonRows, headers, csvRows)
}

func writeUETable(unrealOut, tableName string, jsonRows []map[string]any, headers []string, csvRows [][]string) error {
	if err := writeJSONFile(filepath.Join(unrealOut, tableName+".json"), jsonRows); err != nil {
		return err
	}
	var buf bytes.Buffer
	w := csv.NewWriter(&buf)
	if err := w.Write(headers); err != nil {
		return fmt.Errorf("[Gate 4: Export] csv header %s: %w", tableName, err)
	}
	for _, r := range csvRows {
		if err := w.Write(r); err != nil {
			return fmt.Errorf("[Gate 4: Export] csv row %s: %w", tableName, err)
		}
	}
	w.Flush()
	if err := w.Error(); err != nil {
		return fmt.Errorf("[Gate 4: Export] csv flush %s: %w", tableName, err)
	}
	csvPath := filepath.Join(unrealOut, tableName+".csv")
	if err := os.WriteFile(csvPath, buf.Bytes(), 0o644); err != nil {
		return fmt.Errorf("[Gate 4: Export] write %s: %w", csvPath, err)
	}
	return nil
}

func writeJSONFile(path string, v any) error {
	raw, err := json.MarshalIndent(v, "", "  ")
	if err != nil {
		return fmt.Errorf("[Gate 4: Export] marshal %s: %w", path, err)
	}
	raw = append(raw, '\n')
	if err := os.WriteFile(path, raw, 0o644); err != nil {
		return fmt.Errorf("[Gate 4: Export] write %s: %w", path, err)
	}
	return nil
}

func ueModifiers(mods []StatModifier) []map[string]any {
	out := make([]map[string]any, 0, len(mods))
	for _, m := range mods {
		out = append(out, map[string]any{
			"AttrId": m.AttrID,
			"Op":     m.Op,
			"Value":  m.Value,
		})
	}
	return out
}

func ueGameplayTag(tag string) map[string]string {
	return map[string]string{"TagName": tag}
}

func ueTagString(tag string) string {
	return fmt.Sprintf(`(TagName="%s")`, tag)
}

func ueFNamePtr(p *string) string {
	if p == nil || *p == "" {
		return "None"
	}
	return *p
}

func ueSoftPtr(p *string) string {
	if p == nil || *p == "" {
		return "None"
	}
	return *p
}

func ueBool(b bool) string {
	if b {
		return "True"
	}
	return "False"
}

func ueFloat(f float64) string {
	return strconv.FormatFloat(f, 'f', -1, 64)
}

func toPascalKeys(v any) any {
	switch t := v.(type) {
	case map[string]any:
		out := make(map[string]any, len(t))
		for k, val := range t {
			out[snakeToPascal(k)] = toPascalKeys(val)
		}
		return out
	case []any:
		out := make([]any, len(t))
		for i, elem := range t {
			out[i] = toPascalKeys(elem)
		}
		return out
	default:
		return v
	}
}

func snakeToPascal(s string) string {
	parts := strings.Split(s, "_")
	var b strings.Builder
	for _, p := range parts {
		if p == "" {
			continue
		}
		b.WriteString(strings.ToUpper(p[:1]))
		if len(p) > 1 {
			b.WriteString(strings.ToLower(p[1:]))
		}
	}
	return b.String()
}

func ueStructText(v any) string {
	m, ok := v.(map[string]any)
	if !ok {
		return "()"
	}
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	parts := make([]string, 0, len(keys))
	for _, k := range keys {
		parts = append(parts, fmt.Sprintf("%s=%s", k, uePropValue(m[k])))
	}
	return "(" + strings.Join(parts, ",") + ")"
}

func ueArrayOfStructsText(arr []map[string]any) string {
	parts := make([]string, 0, len(arr))
	for _, item := range arr {
		parts = append(parts, ueStructText(item))
	}
	return "(" + strings.Join(parts, ",") + ")"
}

func ueStringListText(arr []string) string {
	parts := make([]string, 0, len(arr))
	for _, s := range arr {
		parts = append(parts, fmt.Sprintf("%q", s))
	}
	return "(" + strings.Join(parts, ",") + ")"
}

func uePropValue(v any) string {
	switch t := v.(type) {
	case string:
		return fmt.Sprintf("%q", t)
	case bool:
		return ueBool(t)
	case int:
		return strconv.Itoa(t)
	case int64:
		return strconv.FormatInt(t, 10)
	case float64:
		return ueFloat(t)
	case []string:
		return ueStringListText(t)
	case []map[string]any:
		return ueArrayOfStructsText(t)
	case []any:
		parts := make([]string, 0, len(t))
		for _, elem := range t {
			parts = append(parts, uePropValue(elem))
		}
		return "(" + strings.Join(parts, ",") + ")"
	case map[string]any:
		return ueStructText(t)
	case map[string]string:
		m := make(map[string]any, len(t))
		for k, val := range t {
			m[k] = val
		}
		return ueStructText(m)
	case nil:
		return `"None"`
	default:
		return fmt.Sprintf("%q", fmt.Sprintf("%v", t))
	}
}
