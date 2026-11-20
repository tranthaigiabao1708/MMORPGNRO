// Package gamedata loads canonical item, set, affix, enhance, monster, and loot definitions
// from gamedata/sources/core/*.json and synchronizes item_definitions into PostgreSQL without altering SQL DDL.
package gamedata

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/jackc/pgx/v5/pgxpool"
)

type BaseStatEntry struct {
	AttrID string  `json:"attr_id"`
	Value  float64 `json:"value"`
}

type EquipSpec struct {
	Slot             string          `json:"slot"`
	AllowedRaces     []string        `json:"allowed_races"`
	MaxEnhanceLevel  int16           `json:"max_enhance_level"`
	RandomAffixCount int             `json:"random_affix_count"`
	AffixTierPool    int             `json:"affix_tier_pool"`
	SetID            *string         `json:"set_id"`
	BaseStats        []BaseStatEntry `json:"base_stats"`
	InnateEffectID   *string         `json:"innate_effect_id"`
}

type ItemDef struct {
	ItemID        string     `json:"item_id"`
	DisplayName   string     `json:"display_name,omitempty"`
	ItemType      string     `json:"item_type"`
	Rarity        string     `json:"rarity"`
	RequiredLevel int        `json:"required_level"`
	MaxStack      int        `json:"max_stack"`
	IsTradeable   bool       `json:"is_tradeable"`
	SellPriceCoin int64      `json:"sell_price_coin"`
	EquipSpec     *EquipSpec `json:"equip_spec"`
}

type SetStatModifier struct {
	AttrID string  `json:"attr_id"`
	Op     string  `json:"op"`
	Value  float64 `json:"value"`
}

type SetBonusTier struct {
	RequiredPieces int               `json:"required_pieces"`
	StatModifiers  []SetStatModifier `json:"stat_modifiers"`
	EffectID       *string           `json:"effect_id"`
}

type ItemSetDef struct {
	SetID         string         `json:"set_id"`
	RequiredLevel int            `json:"required_level"`
	PieceItemIDs  []string       `json:"piece_item_ids"`
	Bonuses       []SetBonusTier `json:"bonuses"`
}

type AffixTier struct {
	Tier         int     `json:"tier"`
	MinItemLevel int     `json:"min_item_level"`
	MaxItemLevel int     `json:"max_item_level"`
	MinValue     float64 `json:"min_value"`
	MaxValue     float64 `json:"max_value"`
	Step         float64 `json:"step"`
}

type AffixDef struct {
	AffixID        string      `json:"affix_id"`
	TargetAttrID   string      `json:"target_attr_id"`
	ModOp          string      `json:"mod_op"`
	ExclusiveGroup string      `json:"exclusive_group"`
	RollWeight     int64       `json:"roll_weight"`
	AllowedSlots   []string    `json:"allowed_slots"`
	Tiers          []AffixTier `json:"tiers"`
}

type EnhanceStepDef struct {
	TargetLevel            int16   `json:"target_level"`
	CumulativeStatBonusPct float64 `json:"cumulative_stat_bonus_pct"`
	SuccessRateBP          int64   `json:"success_rate_bp"`
	BonusBPPerPity         int64   `json:"bonus_bp_per_pity"`
	HardPityThreshold      int     `json:"hard_pity_threshold"`
	StandardCoinCost       int64   `json:"standard_coin_cost"`
	MaterialItemID         string  `json:"material_item_id"`
	MaterialCount          int     `json:"material_count"`
	DowngradeOnFail        int16   `json:"downgrade_on_fail"`
	DowngradeFloorLevel    int16   `json:"downgrade_floor_level"`
}

type LootEntry struct {
	EntryType string `json:"entry_type"`
	RefID     string `json:"ref_id"`
	Weight    int64  `json:"weight"`
	MinQty    int    `json:"min_qty"`
	MaxQty    int    `json:"max_qty"`
}

type LootRollGroup struct {
	GroupID            string      `json:"group_id"`
	GroupTriggerChance float64     `json:"group_trigger_chance"`
	PickCount          int         `json:"pick_count"`
	Entries            []LootEntry `json:"entries"`
}

type LootTableDef struct {
	LootTableID          string          `json:"loot_table_id"`
	LootDistributionMode string          `json:"loot_distribution_mode"`
	RollGroups           []LootRollGroup `json:"roll_groups"`
}

type MonsterRewards struct {
	BaseExp                int64   `json:"base_exp"`
	CoinMin                int64   `json:"coin_min"`
	CoinMax                int64   `json:"coin_max"`
	LootTableID            string  `json:"loot_table_id"`
	MinContributionRatio   float64 `json:"min_contribution_ratio"`
	GuaranteedPityTokenID  *string `json:"guaranteed_pity_token_id"`
	GuaranteedPityTokenMin int     `json:"guaranteed_pity_token_min"`
	GuaranteedPityTokenMax int     `json:"guaranteed_pity_token_max"`
}

type MonsterDef struct {
	EntityID string         `json:"entity_id"`
	Rank     string         `json:"rank"`
	Level    int            `json:"level"`
	Rewards  MonsterRewards `json:"rewards"`
}

type PityShopEntryDef struct {
	EntryID          string `json:"entry_id"`
	TokenItemID      string `json:"token_item_id"`
	TokenCost        int    `json:"token_cost"`
	StandardCoinCost int64  `json:"standard_coin_cost"`
	RewardItemID     string `json:"reward_item_id"`
	RewardName       string `json:"reward_name,omitempty"`
	RewardRarity     string `json:"reward_rarity,omitempty"`
	RewardQuantity   int    `json:"reward_quantity"`
	DailyLimit       *int   `json:"daily_limit"`
}

type ZoneDef struct {
	ZoneID              string `json:"zone_id"`
	PlanetID            string `json:"planet_id"`
	UE5LevelName        string `json:"ue5_level_name"`
	RecommendedLevelMin int    `json:"recommended_level_min"`
	RecommendedLevelMax int    `json:"recommended_level_max"`
	SoftCapCCU          int    `json:"soft_cap_ccu"`
	HardCapCCU          int    `json:"hard_cap_ccu"`
}

type Catalog struct {
	Items       map[string]ItemDef
	ItemSets    map[string]ItemSetDef
	Affixes     []AffixDef
	AffixByID   map[string]AffixDef
	EnhanceByLv map[int16]EnhanceStepDef
	LootTables  map[string]LootTableDef
	Monsters    map[string]MonsterDef
	PityShop    []PityShopEntryDef
	PityByID    map[string]PityShopEntryDef
	Zones       map[string]ZoneDef
}

var itemDisplayNames = map[string]string{
	"item.body_recruit_tunic_01":      "Ao Vo Phuc Tan Thu",
	"item.hands_recruit_gloves_01":    "Gang Tay Tap Luyen",
	"item.legs_recruit_pants_01":      "Quan Vo Phuc Tan Thu",
	"item.acc2_training_scouter_01":   "Rada Do Ki So Cap",
	"item.hands_wolfhide_wraps_05":    "Gang Da Soi Cao Nguyen",
	"item.legs_wolfhide_trousers_06":  "Quan Da Soi Cao Nguyen",
	"item.feet_raider_thruster_09":    "Giay Phun Lua Raider",
	"item.acc2_scouter_charm_10":      "Rada Tinh Anh Raider",
	"item.legs_crimson_plates_14":     "Giap Chan Crimson",
	"item.head_vanguard_visor_15":     "Mu Giap Tien Phong",
	"item.hands_vanguard_gauntlets_15": "Gang Sat Tien Phong",
	"item.body_vanguard_cuirass_15":   "Giap Nguc Tien Phong",
	"item.feet_vanguard_greaves_15":   "Giay Chien Tien Phong",
	"item.acc1_titan_crest_ring_18":   "Nhan Huy Hieu Titan",
	"item.special_titan_core_18":      "Loi Nang Luong Titan",
	"item.mat_crimson_ore_t1":         "Da Cuong Hoa Crimson",
	"item.consumable_verdant_elixir_s": "Dau Than Hoi Phuc (+150 HP/Ki)",
	"token.boss_highland_titan":       "Huy Chuong Titan",
}

func ResolveDefaultCoreDir() string {
	if envDir := strings.TrimSpace(os.Getenv("IU_GAMEDATA_CORE_DIR")); envDir != "" {
		return envDir
	}
	_, callerFile, _, _ := runtime.Caller(0)
	repoRoot := filepath.Clean(filepath.Join(filepath.Dir(callerFile), "..", "..", "..", ".."))
	cand := filepath.Join(repoRoot, "gamedata", "sources", "core")
	if st, err := os.Stat(cand); err == nil && st.IsDir() {
		return cand
	}
	return filepath.Join("..", "gamedata", "sources", "core")
}

func LoadDefaultCatalog() (*Catalog, error) {
	return LoadCatalogFromDir(ResolveDefaultCoreDir())
}

func LoadCatalogFromDir(coreDir string) (*Catalog, error) {
	cat := &Catalog{
		Items:       make(map[string]ItemDef),
		ItemSets:    make(map[string]ItemSetDef),
		AffixByID:   make(map[string]AffixDef),
		EnhanceByLv: make(map[int16]EnhanceStepDef),
		LootTables:  make(map[string]LootTableDef),
		Monsters:    make(map[string]MonsterDef),
		PityByID:    make(map[string]PityShopEntryDef),
		Zones:       make(map[string]ZoneDef),
	}

	readJSON := func(filename string, dst any) error {
		raw, err := os.ReadFile(filepath.Join(coreDir, filename))
		if err != nil {
			return fmt.Errorf("read %s: %w", filename, err)
		}
		if err := json.Unmarshal(raw, dst); err != nil {
			return fmt.Errorf("parse %s: %w", filename, err)
		}
		return nil
	}

	var items []ItemDef
	if err := readJSON("items.json", &items); err != nil {
		return nil, err
	}
	for _, it := range items {
		if dn, ok := itemDisplayNames[it.ItemID]; ok {
			it.DisplayName = dn
		} else {
			it.DisplayName = it.ItemID
		}
		cat.Items[it.ItemID] = it
	}

	// Register low-level starter/prototype combat drops (Lv.1..2) so players in Lvl_Greybox_Prototype
	// can immediately loot & equip offensive/energy gear alongside testing req_level >= 5 gating on Wolf/Raider gear!
	prototypeItems := []ItemDef{
		{
			ItemID:        "item.hands_recruit_gloves_01",
			DisplayName:   itemDisplayNames["item.hands_recruit_gloves_01"],
			ItemType:      "EQUIPMENT",
			Rarity:        "rarity.uncommon",
			RequiredLevel: 1,
			MaxStack:      1,
			IsTradeable:   true,
			SellPriceCoin: 30,
			EquipSpec: &EquipSpec{
				Slot:             "slot.hands",
				AllowedRaces:     []string{"ALL"},
				MaxEnhanceLevel:  10,
				RandomAffixCount: 1,
				AffixTierPool:    1,
				BaseStats: []BaseStatEntry{
					{AttrID: "attr.phys_atk", Value: 15},
					{AttrID: "attr.ki_atk", Value: 15},
				},
			},
		},
		{
			ItemID:        "item.legs_recruit_pants_01",
			DisplayName:   itemDisplayNames["item.legs_recruit_pants_01"],
			ItemType:      "EQUIPMENT",
			Rarity:        "rarity.uncommon",
			RequiredLevel: 2,
			MaxStack:      1,
			IsTradeable:   true,
			SellPriceCoin: 35,
			EquipSpec: &EquipSpec{
				Slot:             "slot.legs",
				AllowedRaces:     []string{"ALL"},
				MaxEnhanceLevel:  10,
				RandomAffixCount: 1,
				AffixTierPool:    1,
				BaseStats: []BaseStatEntry{
					{AttrID: "attr.max_hp", Value: 90},
					{AttrID: "attr.def", Value: 10},
				},
			},
		},
		{
			ItemID:        "item.acc2_training_scouter_01",
			DisplayName:   itemDisplayNames["item.acc2_training_scouter_01"],
			ItemType:      "EQUIPMENT",
			Rarity:        "rarity.rare",
			RequiredLevel: 2,
			MaxStack:      1,
			IsTradeable:   true,
			SellPriceCoin: 60,
			EquipSpec: &EquipSpec{
				Slot:             "slot.acc_2",
				AllowedRaces:     []string{"ALL"},
				MaxEnhanceLevel:  10,
				RandomAffixCount: 2,
				AffixTierPool:    1,
				BaseStats: []BaseStatEntry{
					{AttrID: "attr.max_energy", Value: 60},
					{AttrID: "attr.phys_atk", Value: 8},
				},
			},
		},
	}
	for _, pit := range prototypeItems {
		if _, exists := cat.Items[pit.ItemID]; !exists {
			cat.Items[pit.ItemID] = pit
		}
	}

	var sets []ItemSetDef
	if err := readJSON("item_sets.json", &sets); err != nil {
		return nil, err
	}
	for _, s := range sets {
		cat.ItemSets[s.SetID] = s
	}

	var affixes []AffixDef
	if err := readJSON("affix_pools.json", &affixes); err != nil {
		return nil, err
	}
	cat.Affixes = affixes
	for _, af := range affixes {
		cat.AffixByID[af.AffixID] = af
	}

	var curves []EnhanceStepDef
	if err := readJSON("enhance_curves.json", &curves); err != nil {
		return nil, err
	}
	for _, c := range curves {
		cat.EnhanceByLv[c.TargetLevel] = c
	}

	var lootTables []LootTableDef
	if err := readJSON("loot_tables.json", &lootTables); err != nil {
		return nil, err
	}
	for _, lt := range lootTables {
		cat.LootTables[lt.LootTableID] = lt
	}

	// Prototype Raider table guarantees Enhance Material / Elixir + high chance of Lv.1..5 Equipment for live Vertical Slice raids
	cat.LootTables["loot.mob_prototype_raider"] = LootTableDef{
		LootTableID:          "loot.mob_prototype_raider",
		LootDistributionMode: "PERSONAL_INSTANCED",
		RollGroups: []LootRollGroup{
			{
				GroupID:            "proto_supplies",
				GroupTriggerChance: 1.0,
				PickCount:          1,
				Entries: []LootEntry{
					{EntryType: "ITEM", RefID: "item.mat_crimson_ore_t1", Weight: 650, MinQty: 2, MaxQty: 3},
					{EntryType: "ITEM", RefID: "item.consumable_verdant_elixir_s", Weight: 350, MinQty: 1, MaxQty: 2},
				},
			},
			{
				GroupID:            "proto_gear",
				GroupTriggerChance: 0.85,
				PickCount:          1,
				Entries: []LootEntry{
					{EntryType: "ITEM", RefID: "item.hands_recruit_gloves_01", Weight: 360, MinQty: 1, MaxQty: 1},
					{EntryType: "ITEM", RefID: "item.legs_recruit_pants_01", Weight: 280, MinQty: 1, MaxQty: 1},
					{EntryType: "ITEM", RefID: "item.acc2_training_scouter_01", Weight: 220, MinQty: 1, MaxQty: 1},
					{EntryType: "ITEM", RefID: "item.hands_wolfhide_wraps_05", Weight: 140, MinQty: 1, MaxQty: 1},
				},
			},
		},
	}

	var monsters []MonsterDef
	if err := readJSON("monsters.json", &monsters); err != nil {
		return nil, err
	}
	for _, m := range monsters {
		cat.Monsters[m.EntityID] = m
	}
	cat.Monsters["mob.prototype_raider"] = MonsterDef{
		EntityID: "mob.prototype_raider",
		Rank:     "NORMAL",
		Level:    3,
		Rewards: MonsterRewards{
			BaseExp:     55,
			CoinMin:     85,
			CoinMax:     135,
			LootTableID: "loot.mob_prototype_raider",
		},
	}

	var pityEntries []PityShopEntryDef
	if err := readJSON("pity_shop.json", &pityEntries); err != nil {
		return nil, err
	}
	for i := range pityEntries {
		if it, ok := cat.Items[pityEntries[i].RewardItemID]; ok {
			pityEntries[i].RewardName = it.DisplayName
			pityEntries[i].RewardRarity = it.Rarity
		} else {
			pityEntries[i].RewardName = pityEntries[i].RewardItemID
			pityEntries[i].RewardRarity = "rarity.rare"
		}
		cat.PityByID[pityEntries[i].EntryID] = pityEntries[i]
	}
	cat.PityShop = pityEntries

	var zones []ZoneDef
	if err := readJSON("zones.json", &zones); err != nil {
		return nil, err
	}
	for _, z := range zones {
		cat.Zones[z.ZoneID] = z
	}
	if _, exists := cat.Zones["zone.p01_west_hub"]; !exists {
		cat.Zones["zone.p01_west_hub"] = ZoneDef{
			ZoneID:              "zone.p01_west_hub",
			PlanetID:            "planet.p01_terra",
			UE5LevelName:        "Lvl_Greybox_Prototype",
			RecommendedLevelMin: 1,
			RecommendedLevelMax: 20,
			SoftCapCCU:          20,
			HardCapCCU:          25,
		}
	}

	return cat, nil
}

// MapSQLItemCategory maps gamedata item_type strings to the exact CHECK constraint values in 000001_init_core_schema.up.sql.
func MapSQLItemCategory(itemType string) string {
	switch strings.ToUpper(strings.TrimSpace(itemType)) {
	case "EQUIPMENT":
		return "EQUIPMENT"
	case "CONSUMABLE":
		return "CONSUMABLE"
	case "MATERIAL", "ENHANCE_MATERIAL":
		return "MATERIAL"
	case "GEM_CORE":
		return "GEM_CORE"
	case "QUEST_ITEM":
		return "QUEST_ITEM"
	case "TOKEN_BOX", "BOSS_TOKEN":
		return "TOKEN_BOX"
	default:
		return "MATERIAL"
	}
}

// SyncItemDefinitions upserts all catalog items into PostgreSQL item_definitions table cleanly.
func (c *Catalog) SyncItemDefinitions(ctx context.Context, pool *pgxpool.Pool) error {
	for _, it := range c.Items {
		sqlCat := MapSQLItemCategory(it.ItemType)
		var equipSlot *string
		var setID *string
		var affixPoolID *string
		allowedRaces := []string{}
		maxEnhance := int16(0)
		maxStack := it.MaxStack
		if maxStack < 1 {
			maxStack = 1
		}
		baseStatsMap := make(map[string]float64)

		if sqlCat == "EQUIPMENT" && it.EquipSpec != nil {
			slotCopy := it.EquipSpec.Slot
			equipSlot = &slotCopy
			maxStack = 1
			maxEnhance = it.EquipSpec.MaxEnhanceLevel
			if it.EquipSpec.SetID != nil && *it.EquipSpec.SetID != "" {
				sCopy := *it.EquipSpec.SetID
				setID = &sCopy
			}
			if it.EquipSpec.AffixTierPool > 0 {
				p := fmt.Sprintf("tier_%d", it.EquipSpec.AffixTierPool)
				affixPoolID = &p
			}
			for _, r := range it.EquipSpec.AllowedRaces {
				if r != "" && !strings.EqualFold(r, "ALL") {
					allowedRaces = append(allowedRaces, r)
				}
			}
			for _, bs := range it.EquipSpec.BaseStats {
				baseStatsMap[bs.AttrID] = bs.Value
			}
		}

		baseStatsBytes, _ := json.Marshal(baseStatsMap)
		rawDefBytes, _ := json.Marshal(it)
		sum := sha256.Sum256(rawDefBytes)
		contentHash := hex.EncodeToString(sum[:])

		_, err := pool.Exec(ctx, `
			INSERT INTO item_definitions (
				item_def_id, item_category, equip_slot, base_rarity, req_level,
				allowed_races, set_id, max_stack, max_enhance_level,
				is_tradeable_default, npc_sell_price, base_stats, affix_pool_id, content_hash, updated_at
			) VALUES (
				$1, $2, $3, $4, $5,
				$6, $7, $8, $9,
				$10, $11, $12::jsonb, $13, $14, NOW()
			)
			ON CONFLICT (item_def_id) DO UPDATE SET
				item_category = EXCLUDED.item_category,
				equip_slot = EXCLUDED.equip_slot,
				base_rarity = EXCLUDED.base_rarity,
				req_level = EXCLUDED.req_level,
				allowed_races = EXCLUDED.allowed_races,
				set_id = EXCLUDED.set_id,
				max_stack = EXCLUDED.max_stack,
				max_enhance_level = EXCLUDED.max_enhance_level,
				is_tradeable_default = EXCLUDED.is_tradeable_default,
				npc_sell_price = EXCLUDED.npc_sell_price,
				base_stats = EXCLUDED.base_stats,
				affix_pool_id = EXCLUDED.affix_pool_id,
				content_hash = EXCLUDED.content_hash,
				updated_at = NOW()
		`,
			it.ItemID, sqlCat, equipSlot, it.Rarity, max(1, it.RequiredLevel),
			allowedRaces, setID, maxStack, maxEnhance,
			it.IsTradeable, max(int64(0), it.SellPriceCoin), string(baseStatsBytes), affixPoolID, contentHash,
		)
		if err != nil {
			return fmt.Errorf("upsert item_definition %s: %w", it.ItemID, err)
		}
	}
	return nil
}
