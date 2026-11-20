package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

type Options struct {
	Validate   bool
	Export     bool
	SrcDir     string
	SchemasDir string
	IPPack     string
	OutDir     string
	Locale     string
}

type CompileResult struct {
	GameDataHash     string
	PresentationHash string
	IPPack           string
	Elapsed          time.Duration
}

type CoreFile struct {
	Name string
	Path string
	Raw  []byte
	Data any
}

type LoadedDataset struct {
	SrcDir       string
	CoreDir      string
	PacksDir     string
	PackDir      string
	PackName     string
	SchemasDir   string
	CoreFiles    map[string]*CoreFile
	PackFiles    map[string]*CoreFile
	Races        []RaceDef
	Skills       []SkillDef
	Forms        []TransformationDef
	Items        []ItemDef
	ItemSets     []ItemSetDef
	Affixes      []AffixDef
	Monsters     []MonsterDef
	LootTables   []LootTableDef
	EnhanceCurve []EnhanceTierDef
	Zones        []ZoneDef
	ExpTable     []ExpLevelDef
	PityShop     []PityShopEntryDef
	GlobalConst  GlobalConstantsDef
	PackManifest PackManifestDef
	StringsVI    LocalizationFileDef
	StringsEN    LocalizationFileDef
	AssetBind    AssetBindingsFileDef
}

type StatModifier struct {
	AttrID string  `json:"attr_id"`
	Op     string  `json:"op"`
	Value  float64 `json:"value"`
}

type RaceDef struct {
	RaceID               string         `json:"race_id"`
	GameplayTag          string         `json:"gameplay_tag"`
	ScopeStatus          string         `json:"scope_status"`
	BaseStatsLv1         map[string]any `json:"base_stats_lv1"`
	GrowthPerLevel       map[string]any `json:"growth_per_level"`
	TierBonusEvery5Level map[string]any `json:"tier_bonus_every_5_levels"`
	InnatePassive        struct {
		PassiveID           string         `json:"passive_id"`
		TriggerHPRatio      float64        `json:"trigger_hp_ratio"`
		DurationSec         float64        `json:"duration_sec"`
		InternalCooldownSec float64        `json:"internal_cooldown_sec"`
		Modifiers           []StatModifier `json:"modifiers"`
	} `json:"innate_passive"`
	StarterSkillIDs   []string `json:"starter_skill_ids"`
	UnlockableFormIDs []string `json:"unlockable_form_ids"`
}

type SkillDef struct {
	SkillID               string   `json:"skill_id"`
	AbilityTag            string   `json:"ability_tag"`
	Category              string   `json:"category"`
	AllowedRaces          []string `json:"allowed_races"`
	UnlockLevel           int      `json:"unlock_level"`
	MasteryPointCost      int      `json:"mastery_point_cost"`
	CanCastInAir          bool     `json:"can_cast_in_air"`
	CanCastWhileHitstun   bool     `json:"can_cast_while_hitstunned"`
	ResourceCost          map[string]any `json:"resource_cost"`
	Timing                map[string]any `json:"timing"`
	TargetingAndHitbox    map[string]any `json:"targeting_and_hitbox"`
	DamageProfile         map[string]any `json:"damage_profile"`
}

type TransformationDef struct {
	FormID                      string         `json:"form_id"`
	StateTag                    string         `json:"state_tag"`
	RequiredRaceID              string         `json:"required_race_id"`
	PrerequisiteFormID          *string        `json:"prerequisite_form_id"`
	UnlockLevel                 int            `json:"unlock_level"`
	MasteryPointCost            int            `json:"mastery_point_cost"`
	MinEnergyToActivate         float64        `json:"min_energy_to_activate"`
	ActivationCastSec           float64        `json:"activation_cast_sec"`
	ActivationKnockbackRadiusCm float64        `json:"activation_knockback_radius_cm"`
	ToggleOffICDSec             float64        `json:"toggle_off_icd_sec"`
	ExhaustionPenaltySec        float64        `json:"exhaustion_penalty_sec"`
	FormDamageMultiplier        float64        `json:"form_damage_multiplier"`
	BaseEnergyDrainPerSec       float64        `json:"base_energy_drain_per_sec"`
	BaseHPDrainRatioPerSec      float64        `json:"base_hp_drain_ratio_per_sec"`
	StatBonuses                 []StatModifier `json:"stat_bonuses"`
}

type BaseStatEntry struct {
	AttrID string  `json:"attr_id"`
	Value  float64 `json:"value"`
}

type EquipSpecDef struct {
	Slot             string          `json:"slot"`
	AllowedRaces     []string        `json:"allowed_races"`
	MaxEnhanceLevel  int             `json:"max_enhance_level"`
	RandomAffixCount int             `json:"random_affix_count"`
	AffixTierPool    int             `json:"affix_tier_pool"`
	SetID            *string         `json:"set_id"`
	BaseStats        []BaseStatEntry `json:"base_stats"`
	InnateEffectID   *string         `json:"innate_effect_id"`
}

type ItemDef struct {
	ItemID        string        `json:"item_id"`
	ItemType      string        `json:"item_type"`
	Rarity        string        `json:"rarity"`
	RequiredLevel int           `json:"required_level"`
	MaxStack      int           `json:"max_stack"`
	IsTradeable   bool          `json:"is_tradeable"`
	SellPriceCoin int64         `json:"sell_price_coin"`
	EquipSpec     *EquipSpecDef `json:"equip_spec"`
}

type ItemSetBonusDef struct {
	RequiredPieces int            `json:"required_pieces"`
	StatModifiers  []StatModifier `json:"stat_modifiers"`
	EffectID       *string        `json:"effect_id"`
}

type ItemSetDef struct {
	SetID         string            `json:"set_id"`
	RequiredLevel int               `json:"required_level"`
	PieceItemIDs  []string          `json:"piece_item_ids"`
	Bonuses       []ItemSetBonusDef `json:"bonuses"`
}

type AffixTierDef struct {
	Tier         int     `json:"tier"`
	MinItemLevel int     `json:"min_item_level"`
	MaxItemLevel int     `json:"max_item_level"`
	MinValue     float64 `json:"min_value"`
	MaxValue     float64 `json:"max_value"`
	Step         float64 `json:"step"`
}

type AffixDef struct {
	AffixID        string         `json:"affix_id"`
	TargetAttrID   string         `json:"target_attr_id"`
	ModOp          string         `json:"mod_op"`
	ExclusiveGroup string         `json:"exclusive_group"`
	RollWeight     int            `json:"roll_weight"`
	AllowedSlots   []string       `json:"allowed_slots"`
	Tiers          []AffixTierDef `json:"tiers"`
}

type MonsterDef struct {
	EntityID     string         `json:"entity_id"`
	Rank         string         `json:"rank"`
	Archetype    string         `json:"archetype"`
	Level        int            `json:"level"`
	CanFly3D     bool           `json:"can_fly_3d"`
	CombatStats  map[string]any `json:"combat_stats"`
	PoiseSpec    map[string]any `json:"poise_spec"`
	AIPerception map[string]any `json:"ai_perception"`
	Rewards      struct {
		BaseEXP                int     `json:"base_exp"`
		CoinMin                int     `json:"coin_min"`
		CoinMax                int     `json:"coin_max"`
		LootTableID            string  `json:"loot_table_id"`
		MinContributionRatio   float64 `json:"min_contribution_ratio"`
		GuaranteedPityTokenID  *string `json:"guaranteed_pity_token_id"`
		GuaranteedPityTokenMin int     `json:"guaranteed_pity_token_min"`
		GuaranteedPityTokenMax int     `json:"guaranteed_pity_token_max"`
	} `json:"rewards"`
	Phases []struct {
		PhaseIndex     int              `json:"phase_index"`
		EnterAtHPRatio float64          `json:"enter_at_hp_ratio"`
		DamageMult     float64          `json:"damage_mult"`
		AtkSpeedMult   float64          `json:"atk_speed_mult"`
		Abilities      []map[string]any `json:"abilities"`
	} `json:"phases"`
}

type LootEntryDef struct {
	EntryType string  `json:"entry_type"`
	RefID     *string `json:"ref_id"`
	Weight    int     `json:"weight"`
	MinQty    int     `json:"min_qty"`
	MaxQty    int     `json:"max_qty"`
}

type LootRollGroupDef struct {
	GroupID            string         `json:"group_id"`
	GroupTriggerChance float64        `json:"group_trigger_chance"`
	PickCount          int            `json:"pick_count"`
	Entries            []LootEntryDef `json:"entries"`
}

type LootTableDef struct {
	LootTableID          string             `json:"loot_table_id"`
	LootDistributionMode string             `json:"loot_distribution_mode"`
	RollGroups           []LootRollGroupDef `json:"roll_groups"`
}

type EnhanceTierDef struct {
	TargetLevel            int     `json:"target_level"`
	CumulativeStatBonusPct float64 `json:"cumulative_stat_bonus_pct"`
	SuccessRateBP          int     `json:"success_rate_bp"`
	BonusBPPerPity         int     `json:"bonus_bp_per_pity"`
	HardPityThreshold      int     `json:"hard_pity_threshold"`
	StandardCoinCost       int64   `json:"standard_coin_cost"`
	MaterialItemID         string  `json:"material_item_id"`
	MaterialCount          int     `json:"material_count"`
	DowngradeOnFail        int     `json:"downgrade_on_fail"`
	DowngradeFloorLevel    int     `json:"downgrade_floor_level"`
}

type ZoneDef struct {
	ZoneID              string         `json:"zone_id"`
	PlanetID            string         `json:"planet_id"`
	UE5LevelName        string         `json:"ue5_level_name"`
	ScopeStatus         string         `json:"scope_status"`
	RecommendedLevelMin int            `json:"recommended_level_min"`
	RecommendedLevelMax int            `json:"recommended_level_max"`
	SoftCapCCU          int            `json:"soft_cap_ccu"`
	HardCapCCU          int            `json:"hard_cap_ccu"`
	WorldBoundsCm       map[string]any `json:"world_bounds_cm"`
	Subzones            []struct {
		SubzoneID           string         `json:"subzone_id"`
		IsSafeZone          bool           `json:"is_safe_zone"`
		RecommendedLevelMin int            `json:"recommended_level_min"`
		RecommendedLevelMax int            `json:"recommended_level_max"`
		BoundsCm            map[string]any `json:"bounds_cm"`
		Spawns              []struct {
			EntityID         string  `json:"entity_id"`
			GroupCount       int     `json:"group_count"`
			EntitiesPerGroup int     `json:"entities_per_group"`
			RespawnTimeSec   float64 `json:"respawn_time_sec"`
		} `json:"spawns"`
	} `json:"subzones"`
}

type ExpLevelDef struct {
	Level         int `json:"level"`
	ExpToNext     int `json:"exp_to_next"`
	CumulativeExp int `json:"cumulative_exp"`
	MasteryReward int `json:"mastery_reward"`
}

type PityShopEntryDef struct {
	EntryID          string `json:"entry_id"`
	TokenItemID      string `json:"token_item_id"`
	TokenCost        int    `json:"token_cost"`
	StandardCoinCost int64  `json:"standard_coin_cost"`
	RewardItemID     string `json:"reward_item_id"`
	RewardQuantity   int    `json:"reward_quantity"`
	DailyLimit       *int   `json:"daily_limit"`
}

type GlobalConstantsDef struct {
	WalkSpeedCms               float64 `json:"walk_speed_cms"`
	RunSpeedCms                float64 `json:"run_speed_cms"`
	HoverFlightSpeedCms        float64 `json:"hover_flight_speed_cms"`
	BoostFlightSpeedCms        float64 `json:"boost_flight_speed_cms"`
	PursuitDashSpeedCms        float64 `json:"pursuit_dash_speed_cms"`
	BoostFlightEnergyPerSec    float64 `json:"boost_flight_energy_per_sec"`
	DefenseFormulaLevelFactor  float64 `json:"defense_formula_level_factor"`
	DefenseFormulaFlatConstant float64 `json:"defense_formula_flat_constant"`
	BaseCritRate               float64 `json:"base_crit_rate"`
	BaseCritDamage             float64 `json:"base_crit_damage"`
	CritRateCap                float64 `json:"crit_rate_cap"`
	StaggerDamageMultiplier    float64 `json:"stagger_damage_multiplier"`
	MaxLevelVS                 int     `json:"max_level_vs"`
	BossMinContributionRatio   float64 `json:"boss_min_contribution_ratio"`
}

type PackManifestDef struct {
	PackID              string   `json:"pack_id"`
	PackName            string   `json:"pack_name"`
	PackVersion         string   `json:"pack_version"`
	IPMode              string   `json:"ip_mode"`
	IsCommercialSafe    bool     `json:"is_commercial_safe"`
	SupportedLocales    []string `json:"supported_locales"`
	AssetRootPath       string   `json:"asset_root_path"`
	DefaultThemeTintHex string   `json:"default_theme_tint_hex"`
	Description         string   `json:"description,omitempty"`
}

type LocalizedStringEntryDef struct {
	CanonicalID      string `json:"canonical_id"`
	DisplayName      string `json:"display_name"`
	ShortDescription string `json:"short_description,omitempty"`
	LoreFlavorText   string `json:"lore_flavor_text,omitempty"`
}

type LocalizationFileDef struct {
	PackID  string                    `json:"pack_id"`
	Locale  string                    `json:"locale"`
	Entries []LocalizedStringEntryDef `json:"entries"`
}

type LinearColorDef struct {
	R float64 `json:"r"`
	G float64 `json:"g"`
	B float64 `json:"b"`
	A float64 `json:"a"`
}

type AssetBindingEntryDef struct {
	CanonicalID         string          `json:"canonical_id"`
	IconTexture         *string         `json:"icon_texture,omitempty"`
	PrimarySkeletalMesh *string         `json:"primary_skeletal_mesh,omitempty"`
	PrimaryMontage      *string         `json:"primary_montage,omitempty"`
	PrimaryVFX          *string         `json:"primary_vfx,omitempty"`
	ImpactVFX           *string         `json:"impact_vfx,omitempty"`
	CastSFX             *string         `json:"cast_sfx,omitempty"`
	ImpactSFX           *string         `json:"impact_sfx,omitempty"`
	ThemeTintColor      *LinearColorDef `json:"theme_tint_color,omitempty"`
}

type AssetBindingsFileDef struct {
	PackID                 string                 `json:"pack_id"`
	AssetPackSchemaVersion string                 `json:"asset_pack_schema_version"`
	Entries                []AssetBindingEntryDef `json:"entries"`
}

var requiredCoreFiles = []string{
	"races.json",
	"skills.json",
	"transformations.json",
	"items.json",
	"item_sets.json",
	"affix_pools.json",
	"monsters.json",
	"loot_tables.json",
	"enhance_curves.json",
	"zones.json",
	"exp_table.json",
	"pity_shop.json",
	"global_constants.json",
}

var requiredPackFiles = []string{
	"pack_manifest.json",
	"strings_vi.json",
	"strings_en.json",
	"asset_bindings.json",
}

func Run(opts Options) (*CompileResult, error) {
	start := time.Now()
	if !opts.Validate && !opts.Export {
		return nil, fmt.Errorf("at least one of --validate or --export must be specified")
	}
	if opts.IPPack == "" {
		opts.IPPack = "Pack_Original_Commercial"
	}
	if opts.Locale == "" {
		opts.Locale = "vi"
	}

	ds, err := loadDataset(opts)
	if err != nil {
		return nil, err
	}

	// Gate 1: Draft 2020-12 JSON Schema validation
	if err := validateSchemas(ds); err != nil {
		return nil, err
	}

	// Unmarshal typed representations after Gate 1 confirms structure
	if err := decodeTypedDataset(ds); err != nil {
		return nil, err
	}

	// Gate IP: IP Hygiene Scanner (run before/alongside RI so brand IDs are caught clearly)
	if err := scanIPHygiene(ds); err != nil {
		return nil, err
	}

	// Gate 2: Canonical IDs, Master Dictionary, Referential Integrity, Loot DAG, Pack Completeness
	if err := validateIntegrity(ds); err != nil {
		return nil, err
	}

	// Gate 3: Math Sanity & Loot Weight Checks
	if err := validateMath(ds); err != nil {
		return nil, err
	}

	res := &CompileResult{
		IPPack:  ds.PackName,
		Elapsed: time.Since(start),
	}

	if opts.Export {
		if strings.TrimSpace(opts.OutDir) == "" {
			return nil, fmt.Errorf("--out directory cannot be empty when --export is enabled")
		}
		coreHash, packHash, err := exportArtifacts(ds, opts.OutDir, opts.Locale)
		if err != nil {
			return nil, err
		}
		res.GameDataHash = coreHash
		res.PresentationHash = packHash
	} else {
		// Compute hash read-only for reporting
		coreHash, err := computeCoreHash(ds.CoreDir)
		if err != nil {
			return nil, err
		}
		res.GameDataHash = coreHash
	}
	res.Elapsed = time.Since(start)
	return res, nil
}

func loadDataset(opts Options) (*LoadedDataset, error) {
	if err := requireDir(opts.SrcDir, "--src"); err != nil {
		return nil, err
	}
	if err := requireDir(opts.SchemasDir, "--schemas"); err != nil {
		return nil, err
	}
	coreDir := filepath.Join(opts.SrcDir, "core")
	if err := requireDir(coreDir, "core gamedata dir"); err != nil {
		return nil, err
	}
	packsDir := filepath.Join(opts.SrcDir, "packs")
	if err := requireDir(packsDir, "packs dir"); err != nil {
		return nil, err
	}
	packDir := filepath.Join(packsDir, opts.IPPack)
	if err := requireDir(packDir, "ip-pack "+opts.IPPack); err != nil {
		return nil, err
	}

	ds := &LoadedDataset{
		SrcDir:     opts.SrcDir,
		CoreDir:    coreDir,
		PacksDir:   packsDir,
		PackDir:    packDir,
		PackName:   opts.IPPack,
		SchemasDir: opts.SchemasDir,
		CoreFiles:  make(map[string]*CoreFile, len(requiredCoreFiles)),
		PackFiles:  make(map[string]*CoreFile, len(requiredPackFiles)),
	}

	for _, fname := range requiredCoreFiles {
		fp := filepath.Join(coreDir, fname)
		cf, err := readAndParseJSON(filepath.ToSlash(filepath.Join("core", fname)), fp)
		if err != nil {
			return nil, err
		}
		ds.CoreFiles[fname] = cf
	}

	// Also walk any extra .json files under core/ if present (for completeness/hygiene)
	_ = filepath.WalkDir(coreDir, func(path string, d os.DirEntry, walkErr error) error {
		if walkErr != nil || d.IsDir() || !strings.HasSuffix(strings.ToLower(d.Name()), ".json") {
			return walkErr
		}
		rel, _ := filepath.Rel(coreDir, path)
		relSlash := filepath.ToSlash(rel)
		if _, ok := ds.CoreFiles[relSlash]; !ok {
			cf, err := readAndParseJSON("core/"+relSlash, path)
			if err != nil {
				return err
			}
			ds.CoreFiles[relSlash] = cf
		}
		return nil
	})

	for _, fname := range requiredPackFiles {
		fp := filepath.Join(packDir, fname)
		cf, err := readAndParseJSON(filepath.ToSlash(filepath.Join("packs", opts.IPPack, fname)), fp)
		if err != nil {
			return nil, err
		}
		ds.PackFiles[fname] = cf
	}

	return ds, nil
}

func readAndParseJSON(relName, fullPath string) (*CoreFile, error) {
	raw, err := os.ReadFile(fullPath)
	if err != nil {
		return nil, fmt.Errorf("[Load] %s: %w", relName, err)
	}
	var data any
	if err := json.Unmarshal(raw, &data); err != nil {
		return nil, fmt.Errorf("[Load] %s: malformed JSON syntax: %w", relName, err)
	}
	return &CoreFile{
		Name: relName,
		Path: fullPath,
		Raw:  raw,
		Data: data,
	}, nil
}

func decodeTypedDataset(ds *LoadedDataset) error {
	decode := func(cf *CoreFile, target any) error {
		if err := json.Unmarshal(cf.Raw, target); err != nil {
			return fmt.Errorf("[Decode] %s: %w", cf.Name, err)
		}
		return nil
	}
	if err := decode(ds.CoreFiles["races.json"], &ds.Races); err != nil {
		return err
	}
	if err := decode(ds.CoreFiles["skills.json"], &ds.Skills); err != nil {
		return err
	}
	if err := decode(ds.CoreFiles["transformations.json"], &ds.Forms); err != nil {
		return err
	}
	if err := decode(ds.CoreFiles["items.json"], &ds.Items); err != nil {
		return err
	}
	if err := decode(ds.CoreFiles["item_sets.json"], &ds.ItemSets); err != nil {
		return err
	}
	if err := decode(ds.CoreFiles["affix_pools.json"], &ds.Affixes); err != nil {
		return err
	}
	if err := decode(ds.CoreFiles["monsters.json"], &ds.Monsters); err != nil {
		return err
	}
	if err := decode(ds.CoreFiles["loot_tables.json"], &ds.LootTables); err != nil {
		return err
	}
	if err := decode(ds.CoreFiles["enhance_curves.json"], &ds.EnhanceCurve); err != nil {
		return err
	}
	if err := decode(ds.CoreFiles["zones.json"], &ds.Zones); err != nil {
		return err
	}
	if err := decode(ds.CoreFiles["exp_table.json"], &ds.ExpTable); err != nil {
		return err
	}
	if err := decode(ds.CoreFiles["pity_shop.json"], &ds.PityShop); err != nil {
		return err
	}
	if err := decode(ds.CoreFiles["global_constants.json"], &ds.GlobalConst); err != nil {
		return err
	}
	if err := decode(ds.PackFiles["pack_manifest.json"], &ds.PackManifest); err != nil {
		return err
	}
	if err := decode(ds.PackFiles["strings_vi.json"], &ds.StringsVI); err != nil {
		return err
	}
	if err := decode(ds.PackFiles["strings_en.json"], &ds.StringsEN); err != nil {
		return err
	}
	if err := decode(ds.PackFiles["asset_bindings.json"], &ds.AssetBind); err != nil {
		return err
	}
	return nil
}

func requireDir(path, label string) error {
	if strings.TrimSpace(path) == "" {
		return fmt.Errorf("missing %s path", label)
	}
	info, err := os.Stat(path)
	if err != nil {
		return fmt.Errorf("%s not found (%s): %w", label, path, err)
	}
	if !info.IsDir() {
		return fmt.Errorf("%s is not a directory: %s", label, path)
	}
	return nil
}
