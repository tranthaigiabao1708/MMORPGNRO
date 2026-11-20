package inventory

import (
	"context"
	"crypto/rand"
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"math/big"
	"sort"
	"strings"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/modules/economy"
	"github.com/infinite-universe/backend/internal/modules/gamedata"
	"github.com/infinite-universe/backend/internal/modules/progression"
	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

const (
	MaxBagSlots = 48

	ElixirItemID  = "item.consumable_verdant_elixir_s"
	ElixirHealHP  = 150.0
	ElixirHealKi  = 150.0
)

var (
	ErrStaleFencingEpoch  = errors.New("stale fencing_epoch rejected by PostgreSQL lease fence")
	ErrItemNotFound       = errors.New("inventory item instance not found")
	ErrNotEquippable      = errors.New("item is not equippable")
	ErrLevelTooLow        = errors.New("character level does not meet item required_level")
	ErrRaceNotAllowed     = errors.New("character race cannot equip this item")
	ErrBagFull            = errors.New("inventory bag is full (48/48 slots occupied)")
	ErrCannotSellEquipped = errors.New("cannot sell an equipped item; unequip it first")
	ErrItemLocked         = errors.New("item is locked; unlock before selling")
	ErrNotSellable        = errors.New("item cannot be sold to NPC")
	ErrInvalidSlot        = errors.New("invalid equipment slot_id")
	ErrConsumableEmpty    = errors.New("no matching consumable available in bag")
	ErrMaxEnhanceReached  = errors.New("equipment has already reached maximum enhancement level")
	ErrInsufficientMat    = errors.New("insufficient enhancement material in bag")
)

type RolledAffix struct {
	AffixID string  `json:"affix_id"`
	AttrID  string  `json:"attr_id"`
	Tier    int     `json:"tier"`
	Value   float64 `json:"value"`
}

type socketMetaEntry struct {
	Type  string `json:"type"`
	Count int    `json:"count"`
}

type ItemInstanceView struct {
	InstanceID      uuid.UUID          `json:"instance_id"`
	ItemDefID       string             `json:"item_def_id"`
	DisplayName     string             `json:"display_name"`
	Category        string             `json:"category"`
	EquipSlot       string             `json:"equip_slot,omitempty"`
	EquippedSlotID  string             `json:"equipped_slot_id,omitempty"`
	Rarity          string             `json:"rarity"`
	RequiredLevel   int                `json:"required_level"`
	BagType         string             `json:"bag_type"`
	BagSlotIndex    int                `json:"bag_slot_index"`
	StackCount      int                `json:"stack_count"`
	MaxStack        int                `json:"max_stack"`
	EnhanceLevel    int16              `json:"enhance_level"`
	MaxEnhanceLevel int16              `json:"max_enhance_level"`
	EnhancePity     int                `json:"enhance_pity"`
	SellPriceCoin   int64              `json:"sell_price_coin"`
	IsBound         bool               `json:"is_bound"`
	IsPlayerLocked  bool               `json:"is_player_locked"`
	BaseStats       map[string]float64 `json:"base_stats,omitempty"`
	Affixes         []RolledAffix      `json:"affixes,omitempty"`
	NextEnhanceCoin int64              `json:"next_enhance_coin,omitempty"`
	NextEnhanceMat  int                `json:"next_enhance_mat,omitempty"`
	NextEnhanceBP   int64              `json:"next_enhance_bp,omitempty"`
}

type InventorySnapshot struct {
	CharacterID           uuid.UUID          `json:"character_id"`
	Level                 int                `json:"level"`
	StandardCoin          int64              `json:"standard_coin"`
	HP                    float64            `json:"hp"`
	MaxHP                 float64            `json:"max_hp"`
	Energy                float64            `json:"energy"`
	MaxEnergy             float64            `json:"max_energy"`
	AttackPower           float64            `json:"attack_power"`
	AttackPowerMultiplier float64            `json:"attack_power_multiplier"`
	Defense               float64            `json:"defense"`
	UsedBagSlots          int                `json:"used_bag_slots"`
	MaxBagSlots           int                `json:"max_bag_slots"`
	ElixirCount           int                `json:"elixir_count"`
	CrimsonOreCount       int                `json:"crimson_ore_count"`
	BagItems              []ItemInstanceView `json:"bag_items"`
	EquippedItems         []ItemInstanceView `json:"equipped_items"`
	ActionResult          string             `json:"action_result,omitempty"`
	EnhanceSucceeded      *bool              `json:"enhance_succeeded,omitempty"`
	CoinsDelta            int64              `json:"coins_delta,omitempty"`
}

type persistedCharMeta struct {
	HPUpgrades  int32 `json:"hp_upgrades"`
	KiUpgrades  int32 `json:"ki_upgrades"`
	AtkUpgrades int32 `json:"atk_upgrades"`
}

type Service struct {
	pool    *pgxpool.Pool
	catalog *gamedata.Catalog
}

func NewService(pool *pgxpool.Pool, catalog *gamedata.Catalog) *Service {
	return &Service{
		pool:    pool,
		catalog: catalog,
	}
}

func (s *Service) Catalog() *gamedata.Catalog {
	return s.catalog
}

func extractEnhancePity(socketRaw []byte) int {
	var arr []socketMetaEntry
	if err := json.Unmarshal(socketRaw, &arr); err == nil {
		for _, e := range arr {
			if e.Type == "enhance_pity" {
				return e.Count
			}
		}
	}
	return 0
}

func encodeEnhancePity(pity int) []byte {
	if pity <= 0 {
		return []byte("[]")
	}
	b, _ := json.Marshal([]socketMetaEntry{{Type: "enhance_pity", Count: pity}})
	return b
}

// EnsureStarterKitTx grants the Step 4 starter kit once when a brand-new character has 0 inventory items & 0 equipped items.
func EnsureStarterKitTx(ctx context.Context, tx pgx.Tx, catalog *gamedata.Catalog, characterID uuid.UUID) error {
	if catalog == nil {
		return nil
	}
	var existing int
	if err := tx.QueryRow(ctx, `SELECT COUNT(*) FROM inventory_items WHERE character_id = $1`, characterID).Scan(&existing); err != nil {
		return err
	}
	if existing > 0 {
		return nil
	}

	starterItems := []struct {
		defID string
		qty   int
	}{
		{defID: "item.body_recruit_tunic_01", qty: 1},
		{defID: "item.consumable_verdant_elixir_s", qty: 3},
		{defID: "item.mat_crimson_ore_t1", qty: 4},
	}
	for _, st := range starterItems {
		if _, ok := catalog.Items[st.defID]; !ok {
			continue
		}
		if _, _, err := AddOrStackItemTx(ctx, tx, catalog, characterID, st.defID, st.qty, nil, true, "STARTER_KIT"); err != nil {
			return err
		}
	}
	return nil
}

// ComputeEquippedBonusesTx reads all 8 equipped slots inside tx and computes exact stat modifiers (Base * Enhance + Affixes + SetBonus).
func ComputeEquippedBonusesTx(ctx context.Context, tx pgx.Tx, catalog *gamedata.Catalog, characterID uuid.UUID) (progression.EquipmentBonuses, error) {
	var bonuses progression.EquipmentBonuses
	if catalog == nil {
		return bonuses, nil
	}

	rows, err := tx.Query(ctx, `
		SELECT i.item_def_id, i.enhance_level, i.affix_data
		  FROM character_equipment e
		  JOIN inventory_items i ON i.id = e.item_instance_id
		 WHERE e.character_id = $1
	`, characterID)
	if err != nil {
		return bonuses, err
	}
	defer rows.Close()

	setCounts := make(map[string]int)
	applyAttr := func(attrID string, val float64) {
		switch attrID {
		case "attr.max_hp":
			bonuses.FlatHP += val
		case "attr.max_energy":
			bonuses.FlatKi += val
		case "attr.phys_atk":
			bonuses.FlatAtk += val
		case "attr.ki_atk":
			// Combine highest/average offensive power; if item only grants ki_atk (e.g. visor), add full value;
			// if item grants both phys_atk & ki_atk, phys_atk already contributed primary weapon scaling so add 50% synergy!
			bonuses.FlatAtk += val * 0.5
		case "attr.def":
			bonuses.FlatDef += val
		case "attr.phys_dmg_pct", "attr.ki_dmg_pct":
			bonuses.PctDmg += val * 0.5
		}
	}

	for rows.Next() {
		var defID string
		var enhanceLv int16
		var affixRaw []byte
		if err := rows.Scan(&defID, &enhanceLv, &affixRaw); err != nil {
			return bonuses, err
		}

		def, ok := catalog.Items[defID]
		if !ok || def.EquipSpec == nil {
			continue
		}

		enhanceMult := 1.0
		if enhanceLv > 0 {
			if step, ok := catalog.EnhanceByLv[enhanceLv]; ok {
				enhanceMult += step.CumulativeStatBonusPct
			} else {
				enhanceMult += float64(enhanceLv) * 0.05
			}
		}

		hasPhys := false
		for _, bs := range def.EquipSpec.BaseStats {
			if bs.AttrID == "attr.phys_atk" {
				hasPhys = true
			}
		}
		for _, bs := range def.EquipSpec.BaseStats {
			scaled := math.Round(bs.Value * enhanceMult)
			if bs.AttrID == "attr.ki_atk" && !hasPhys {
				bonuses.FlatAtk += scaled
			} else if bs.AttrID == "attr.ki_atk" && hasPhys {
				// Avoid double-counting identical Phys/Ki pair on hybrid weapons: take max excess if KiAtk > PhysAtk
				continue
			} else {
				applyAttr(bs.AttrID, scaled)
			}
		}
		if hasPhys {
			var pVal, kVal float64
			for _, bs := range def.EquipSpec.BaseStats {
				if bs.AttrID == "attr.phys_atk" {
					pVal = bs.Value
				} else if bs.AttrID == "attr.ki_atk" {
					kVal = bs.Value
				}
			}
			if kVal > pVal {
				bonuses.FlatAtk += math.Round((kVal - pVal) * enhanceMult)
			}
		}

		var affixes []RolledAffix
		if err := json.Unmarshal(affixRaw, &affixes); err == nil {
			for _, af := range affixes {
				if af.AttrID == "attr.ki_atk" {
					bonuses.FlatAtk += af.Value
				} else {
					applyAttr(af.AttrID, af.Value)
				}
			}
		}

		if def.EquipSpec.SetID != nil && *def.EquipSpec.SetID != "" {
			setCounts[*def.EquipSpec.SetID]++
		}
	}
	if err := rows.Err(); err != nil {
		return bonuses, err
	}

	// Evaluate Item Set Bonuses (e.g. set.highland_vanguard 2pc / 4pc)
	for setID, count := range setCounts {
		setDef, ok := catalog.ItemSets[setID]
		if !ok {
			continue
		}
		for _, tier := range setDef.Bonuses {
			if count >= tier.RequiredPieces {
				for _, mod := range tier.StatModifiers {
					if mod.AttrID == "attr.max_energy" && mod.Op == "ADD_PCT" {
						bonuses.PctKi += mod.Value
					} else {
						applyAttr(mod.AttrID, mod.Value)
					}
				}
			}
		}
	}

	return bonuses, nil
}

// RecompileAndPersistAttributesTx recomputes TotalStats (Potential + Equipped) and updates character_attributes inside tx.
func RecompileAndPersistAttributesTx(
	ctx context.Context,
	tx pgx.Tx,
	catalog *gamedata.Catalog,
	characterID uuid.UUID,
	hpDelta float64,
	kiDelta float64,
) (progression.ScaledStats, float64, float64, error) {
	var metaRaw []byte
	if err := tx.QueryRow(ctx, `SELECT appearance_data FROM characters WHERE id = $1`, characterID).Scan(&metaRaw); err != nil {
		return progression.ScaledStats{}, 0, 0, err
	}
	var meta persistedCharMeta
	_ = json.Unmarshal(metaRaw, &meta)

	var curHP, curKi float64
	if err := tx.QueryRow(ctx, `SELECT hp, energy FROM character_attributes WHERE character_id = $1 FOR UPDATE`, characterID).Scan(&curHP, &curKi); err != nil {
		return progression.ScaledStats{}, 0, 0, err
	}

	eqBonus, err := ComputeEquippedBonusesTx(ctx, tx, catalog, characterID)
	if err != nil {
		return progression.ScaledStats{}, 0, 0, err
	}
	compiled := progression.CompileTotalCharacterStats(meta.HPUpgrades, meta.KiUpgrades, meta.AtkUpgrades, eqBonus)

	newHP := clampFloat(curHP+hpDelta, 1.0, compiled.MaxHP)
	newKi := clampFloat(curKi+kiDelta, 0.0, compiled.MaxEnergy)

	if _, err := tx.Exec(ctx, `
		UPDATE character_attributes
		   SET hp = $2, max_hp = $3, energy = $4, max_energy = $5,
		       phys_atk = $6, ki_atk = $7, defense = $8, updated_at = NOW()
		 WHERE character_id = $1
	`, characterID, newHP, compiled.MaxHP, newKi, compiled.MaxEnergy, compiled.PhysAtk, compiled.KiAtk, compiled.Defense); err != nil {
		return progression.ScaledStats{}, 0, 0, err
	}

	return compiled, newHP, newKi, nil
}

// AddOrStackItemTx inserts or stacks an item into the character's 48-slot INVENTORY inside tx.
func AddOrStackItemTx(
	ctx context.Context,
	tx pgx.Tx,
	catalog *gamedata.Catalog,
	characterID uuid.UUID,
	itemDefID string,
	quantity int,
	affixes []RolledAffix,
	isBound bool,
	originSource string,
) (uuid.UUID, int, error) {
	if quantity <= 0 {
		return uuid.Nil, -1, nil
	}
	def, ok := catalog.Items[itemDefID]
	if !ok {
		return uuid.Nil, -1, fmt.Errorf("unknown item_def_id %q", itemDefID)
	}
	maxStack := max(1, def.MaxStack)

	// Lock existing bag items for this character to prevent slot collisions under concurrency!
	rows, err := tx.Query(ctx, `
		SELECT id, item_def_id, bag_slot_index, stack_count, is_bound
		  FROM inventory_items
		 WHERE character_id = $1 AND bag_type = 'INVENTORY'
		 ORDER BY bag_slot_index ASC NULLS LAST
		   FOR UPDATE
	`, characterID)
	if err != nil {
		return uuid.Nil, -1, err
	}
	type existingRow struct {
		id      uuid.UUID
		defID   string
		slot    int
		stack   int
		isBound bool
	}
	var bagRows []existingRow
	occupied := make(map[int]bool)
	for rows.Next() {
		var r existingRow
		var slotPtr *int
		if err := rows.Scan(&r.id, &r.defID, &slotPtr, &r.stack, &r.isBound); err != nil {
			rows.Close()
			return uuid.Nil, -1, err
		}
		if slotPtr != nil {
			r.slot = *slotPtr
			occupied[r.slot] = true
		}
		bagRows = append(bagRows, r)
	}
	rows.Close()

	remaining := quantity
	var lastID uuid.UUID
	lastSlot := -1

	// 1. Try stacking into existing partial stacks if maxStack > 1 and bound flag matches!
	if maxStack > 1 {
		for _, r := range bagRows {
			if remaining <= 0 {
				break
			}
			if r.defID == itemDefID && r.isBound == isBound && r.stack < maxStack {
				add := min(remaining, maxStack-r.stack)
				newStack := r.stack + add
				if _, err := tx.Exec(ctx, `
					UPDATE inventory_items SET stack_count = $2, updated_at = NOW() WHERE id = $1
				`, r.id, newStack); err != nil {
					return uuid.Nil, -1, err
				}
				remaining -= add
				lastID = r.id
				lastSlot = r.slot
			}
		}
	}

	// 2. Place remaining items into lowest free bag slots [0 .. MaxBagSlots-1]
	affixBytes := []byte("[]")
	if len(affixes) > 0 {
		affixBytes, _ = json.Marshal(affixes)
	}
	for remaining > 0 {
		freeSlot := -1
		for s := 0; s < MaxBagSlots; s++ {
			if !occupied[s] {
				freeSlot = s
				break
			}
		}
		if freeSlot < 0 {
			return lastID, lastSlot, ErrBagFull
		}
		putQty := min(remaining, maxStack)
		var insertedID uuid.UUID
		if err := tx.QueryRow(ctx, `
			INSERT INTO inventory_items (
				character_id, item_def_id, rarity, bag_type, bag_slot_index,
				stack_count, enhance_level, affix_data, socket_data, is_bound, origin_source
			) VALUES ($1, $2, $3, 'INVENTORY', $4, $5, 0, $6::jsonb, '[]'::jsonb, $7, $8)
			RETURNING id
		`, characterID, itemDefID, def.Rarity, freeSlot, putQty, string(affixBytes), isBound || !def.IsTradeable, originSource).Scan(&insertedID); err != nil {
			return uuid.Nil, -1, err
		}
		occupied[freeSlot] = true
		remaining -= putQty
		lastID = insertedID
		lastSlot = freeSlot
	}

	return lastID, lastSlot, nil
}

func (s *Service) buildViewFromRow(
	id uuid.UUID,
	defID string,
	rarity string,
	bagType string,
	bagSlot *int,
	equippedSlot *string,
	stack int,
	enhanceLv int16,
	affixRaw []byte,
	socketRaw []byte,
	isBound bool,
	isLocked bool,
) ItemInstanceView {
	v := ItemInstanceView{
		InstanceID:     id,
		ItemDefID:      defID,
		DisplayName:    defID,
		Rarity:         rarity,
		BagType:        bagType,
		BagSlotIndex:   -1,
		StackCount:     stack,
		MaxStack:       1,
		EnhanceLevel:   enhanceLv,
		EnhancePity:    extractEnhancePity(socketRaw),
		IsBound:        isBound,
		IsPlayerLocked: isLocked,
	}
	if bagSlot != nil {
		v.BagSlotIndex = *bagSlot
	}
	if equippedSlot != nil {
		v.EquippedSlotID = *equippedSlot
	}
	_ = json.Unmarshal(affixRaw, &v.Affixes)

	if s.catalog != nil {
		if def, ok := s.catalog.Items[defID]; ok {
			v.DisplayName = def.DisplayName
			v.Category = gamedata.MapSQLItemCategory(def.ItemType)
			v.RequiredLevel = def.RequiredLevel
			v.MaxStack = max(1, def.MaxStack)
			v.SellPriceCoin = def.SellPriceCoin
			if def.EquipSpec != nil {
				v.EquipSlot = def.EquipSpec.Slot
				v.MaxEnhanceLevel = def.EquipSpec.MaxEnhanceLevel
				v.BaseStats = make(map[string]float64)
				mult := 1.0
				if enhanceLv > 0 {
					if step, ok := s.catalog.EnhanceByLv[enhanceLv]; ok {
						mult += step.CumulativeStatBonusPct
					}
				}
				for _, bs := range def.EquipSpec.BaseStats {
					v.BaseStats[bs.AttrID] = math.Round(bs.Value * mult)
				}
				if enhanceLv < def.EquipSpec.MaxEnhanceLevel {
					if nextStep, ok := s.catalog.EnhanceByLv[enhanceLv+1]; ok {
						v.NextEnhanceCoin = nextStep.StandardCoinCost
						v.NextEnhanceMat = nextStep.MaterialCount
						effBP := nextStep.SuccessRateBP + int64(v.EnhancePity)*nextStep.BonusBPPerPity
						if nextStep.HardPityThreshold > 0 && v.EnhancePity+1 >= nextStep.HardPityThreshold {
							effBP = 10000
						}
						if effBP > 10000 {
							effBP = 10000
						}
						v.NextEnhanceBP = effBP
					}
				}
			}
		}
	}
	return v
}

// GetInventorySnapshot returns the full authoritative wallet, 48-slot bag, 8-slot equipment, and compiled stats.
func (s *Service) GetInventorySnapshot(ctx context.Context, characterID uuid.UUID) (InventorySnapshot, error) {
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return InventorySnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	snap, err := s.getSnapshotInTx(ctx, tx, characterID)
	if err != nil {
		return InventorySnapshot{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return InventorySnapshot{}, err
	}
	return snap, nil
}

func (s *Service) getSnapshotInTx(ctx context.Context, tx pgx.Tx, characterID uuid.UUID) (InventorySnapshot, error) {
	snap := InventorySnapshot{
		CharacterID:   characterID,
		MaxBagSlots:   MaxBagSlots,
		BagItems:      []ItemInstanceView{},
		EquippedItems: []ItemInstanceView{},
	}

	err := tx.QueryRow(ctx, `
		SELECT c.level, a.hp, a.max_hp, a.energy, a.max_energy, a.phys_atk, a.defense,
		       COALESCE(w.standard_coin, 0)
		  FROM characters c
		  JOIN character_attributes a ON a.character_id = c.id
		  LEFT JOIN character_wallets w ON w.character_id = c.id
		 WHERE c.id = $1 AND c.deleted_at IS NULL
	`, characterID).Scan(
		&snap.Level, &snap.HP, &snap.MaxHP, &snap.Energy, &snap.MaxEnergy,
		&snap.AttackPower, &snap.Defense, &snap.StandardCoin,
	)
	if err != nil {
		return snap, err
	}
	snap.AttackPowerMultiplier = snap.AttackPower / progression.BaseAttackPower

	rows, err := tx.Query(ctx, `
		SELECT i.id, i.item_def_id, i.rarity, i.bag_type, i.bag_slot_index, e.slot_id,
		       i.stack_count, i.enhance_level, i.affix_data, i.socket_data, i.is_bound, i.is_player_locked
		  FROM inventory_items i
		  LEFT JOIN character_equipment e ON e.item_instance_id = i.id
		 WHERE i.character_id = $1 AND i.bag_type IN ('INVENTORY', 'EQUIPPED')
		 ORDER BY i.bag_slot_index ASC NULLS LAST, i.acquired_at ASC
	`, characterID)
	if err != nil {
		return snap, err
	}
	defer rows.Close()

	for rows.Next() {
		var id uuid.UUID
		var defID, rarity, bagType string
		var bagSlot *int
		var eqSlot *string
		var stack int
		var enhanceLv int16
		var affixRaw, socketRaw []byte
		var isBound, isLocked bool
		if err := rows.Scan(&id, &defID, &rarity, &bagType, &bagSlot, &eqSlot, &stack, &enhanceLv, &affixRaw, &socketRaw, &isBound, &isLocked); err != nil {
			return snap, err
		}
		v := s.buildViewFromRow(id, defID, rarity, bagType, bagSlot, eqSlot, stack, enhanceLv, affixRaw, socketRaw, isBound, isLocked)
		if bagType == "EQUIPPED" {
			snap.EquippedItems = append(snap.EquippedItems, v)
		} else {
			snap.BagItems = append(snap.BagItems, v)
			snap.UsedBagSlots++
			if defID == ElixirItemID {
				snap.ElixirCount += stack
			} else if defID == "item.mat_crimson_ore_t1" {
				snap.CrimsonOreCount += stack
			}
		}
	}
	sort.Slice(snap.EquippedItems, func(i, j int) bool {
		return snap.EquippedItems[i].EquippedSlotID < snap.EquippedItems[j].EquippedSlotID
	})
	return snap, rows.Err()
}

func lockCharacterFencedTx(ctx context.Context, tx pgx.Tx, characterID uuid.UUID, fencingEpoch int64) (level int, raceID string, err error) {
	if fencingEpoch < 1 {
		return 0, "", ErrStaleFencingEpoch
	}
	err = tx.QueryRow(ctx, `
		SELECT level, race_id
		  FROM characters
		 WHERE id = $1 AND fencing_epoch = $2 AND deleted_at IS NULL
		   FOR UPDATE
	`, characterID, fencingEpoch).Scan(&level, &raceID)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return 0, "", ErrStaleFencingEpoch
		}
		return 0, "", err
	}
	return level, raceID, nil
}

// EquipItemFenced equips an inventory item into its canonical 8-slot equipment position (swapping if occupied) and recompiles live stats.
func (s *Service) EquipItemFenced(
	ctx context.Context,
	characterID uuid.UUID,
	fencingEpoch int64,
	serverID string,
	itemInstanceID uuid.UUID,
) (InventorySnapshot, error) {
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return InventorySnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	charLv, charRace, err := lockCharacterFencedTx(ctx, tx, characterID, fencingEpoch)
	if err != nil {
		return InventorySnapshot{}, err
	}

	var defID, bagType string
	var bagSlot *int
	err = tx.QueryRow(ctx, `
		SELECT item_def_id, bag_type, bag_slot_index
		  FROM inventory_items
		 WHERE id = $1 AND character_id = $2
		   FOR UPDATE
	`, itemInstanceID, characterID).Scan(&defID, &bagType, &bagSlot)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return InventorySnapshot{}, ErrItemNotFound
		}
		return InventorySnapshot{}, err
	}
	if bagType != "INVENTORY" {
		return InventorySnapshot{}, ErrNotEquippable
	}

	def, ok := s.catalog.Items[defID]
	if !ok || def.EquipSpec == nil || gamedata.MapSQLItemCategory(def.ItemType) != "EQUIPMENT" {
		return InventorySnapshot{}, ErrNotEquippable
	}
	if charLv < def.RequiredLevel {
		return InventorySnapshot{}, ErrLevelTooLow
	}
	if len(def.EquipSpec.AllowedRaces) > 0 {
		allowed := false
		for _, r := range def.EquipSpec.AllowedRaces {
			if strings.EqualFold(r, "ALL") || r == charRace {
				allowed = true
				break
			}
		}
		if !allowed {
			return InventorySnapshot{}, ErrRaceNotAllowed
		}
	}

	targetSlot := def.EquipSpec.Slot
	vacatedBagSlot := 0
	if bagSlot != nil {
		vacatedBagSlot = *bagSlot
	}

	// Check if another item is already equipped in targetSlot -> swap it back into vacatedBagSlot!
	var prevEquippedID uuid.UUID
	errPrev := tx.QueryRow(ctx, `
		SELECT item_instance_id FROM character_equipment WHERE character_id = $1 AND slot_id = $2 FOR UPDATE
	`, characterID, targetSlot).Scan(&prevEquippedID)
	hasPrev := (errPrev == nil)

	if hasPrev {
		if _, err := tx.Exec(ctx, `DELETE FROM character_equipment WHERE character_id = $1 AND slot_id = $2`, characterID, targetSlot); err != nil {
			return InventorySnapshot{}, err
		}
		if _, err := tx.Exec(ctx, `
			UPDATE inventory_items
			   SET bag_type = 'INVENTORY', bag_slot_index = $2, updated_at = NOW()
			 WHERE id = $1
		`, prevEquippedID, vacatedBagSlot); err != nil {
			return InventorySnapshot{}, err
		}
	}

	if _, err := tx.Exec(ctx, `
		UPDATE inventory_items
		   SET bag_type = 'EQUIPPED', bag_slot_index = NULL, is_bound = TRUE, updated_at = NOW()
		 WHERE id = $1
	`, itemInstanceID); err != nil {
		return InventorySnapshot{}, err
	}

	if _, err := tx.Exec(ctx, `
		INSERT INTO character_equipment (character_id, slot_id, item_instance_id, equipped_at)
		VALUES ($1, $2, $3, NOW())
	`, characterID, targetSlot, itemInstanceID); err != nil {
		return InventorySnapshot{}, err
	}

	if _, _, _, err := RecompileAndPersistAttributesTx(ctx, tx, s.catalog, characterID, 0, 0); err != nil {
		return InventorySnapshot{}, err
	}

	snap, err := s.getSnapshotInTx(ctx, tx, characterID)
	if err != nil {
		return InventorySnapshot{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return InventorySnapshot{}, err
	}
	snap.ActionResult = fmt.Sprintf("Da mac [%s] vao o %s!", def.DisplayName, targetSlot)
	return snap, nil
}

// UnequipSlotFenced removes an equipped item from slotID into the lowest free bag slot and recompiles live stats.
func (s *Service) UnequipSlotFenced(
	ctx context.Context,
	characterID uuid.UUID,
	fencingEpoch int64,
	serverID string,
	slotID string,
) (InventorySnapshot, error) {
	slotID = strings.TrimSpace(slotID)
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return InventorySnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, _, err := lockCharacterFencedTx(ctx, tx, characterID, fencingEpoch); err != nil {
		return InventorySnapshot{}, err
	}

	var equippedItemID uuid.UUID
	if err := tx.QueryRow(ctx, `
		SELECT item_instance_id FROM character_equipment WHERE character_id = $1 AND slot_id = $2 FOR UPDATE
	`, characterID, slotID).Scan(&equippedItemID); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return InventorySnapshot{}, ErrInvalidSlot
		}
		return InventorySnapshot{}, err
	}

	rows, err := tx.Query(ctx, `
		SELECT bag_slot_index FROM inventory_items WHERE character_id = $1 AND bag_type = 'INVENTORY' FOR UPDATE
	`, characterID)
	if err != nil {
		return InventorySnapshot{}, err
	}
	occupied := make(map[int]bool)
	for rows.Next() {
		var idx *int
		if err := rows.Scan(&idx); err == nil && idx != nil {
			occupied[*idx] = true
		}
	}
	rows.Close()

	freeSlot := -1
	for i := 0; i < MaxBagSlots; i++ {
		if !occupied[i] {
			freeSlot = i
			break
		}
	}
	if freeSlot < 0 {
		return InventorySnapshot{}, ErrBagFull
	}

	if _, err := tx.Exec(ctx, `DELETE FROM character_equipment WHERE character_id = $1 AND slot_id = $2`, characterID, slotID); err != nil {
		return InventorySnapshot{}, err
	}
	if _, err := tx.Exec(ctx, `
		UPDATE inventory_items SET bag_type = 'INVENTORY', bag_slot_index = $2, updated_at = NOW() WHERE id = $1
	`, equippedItemID, freeSlot); err != nil {
		return InventorySnapshot{}, err
	}

	if _, _, _, err := RecompileAndPersistAttributesTx(ctx, tx, s.catalog, characterID, 0, 0); err != nil {
		return InventorySnapshot{}, err
	}

	snap, err := s.getSnapshotInTx(ctx, tx, characterID)
	if err != nil {
		return InventorySnapshot{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return InventorySnapshot{}, err
	}
	snap.ActionResult = fmt.Sprintf("Da thao trang bi o %s ve tui do (o #%d).", slotID, freeSlot+1)
	return snap, nil
}

// UseConsumableFenced consumes 1x Verdant Elixir from the character's bag and heals +150 HP & +150 Ki.
func (s *Service) UseConsumableFenced(
	ctx context.Context,
	characterID uuid.UUID,
	fencingEpoch int64,
	serverID string,
	itemInstanceID *uuid.UUID,
) (InventorySnapshot, error) {
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return InventorySnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, _, err := lockCharacterFencedTx(ctx, tx, characterID, fencingEpoch); err != nil {
		return InventorySnapshot{}, err
	}

	var targetID uuid.UUID
	var defID string
	var stackCount int
	if itemInstanceID != nil && *itemInstanceID != uuid.Nil {
		err = tx.QueryRow(ctx, `
			SELECT id, item_def_id, stack_count
			  FROM inventory_items
			 WHERE id = $1 AND character_id = $2 AND bag_type = 'INVENTORY'
			   FOR UPDATE
		`, *itemInstanceID, characterID).Scan(&targetID, &defID, &stackCount)
	} else {
		err = tx.QueryRow(ctx, `
			SELECT id, item_def_id, stack_count
			  FROM inventory_items
			 WHERE character_id = $1 AND bag_type = 'INVENTORY' AND item_def_id = $2
			 ORDER BY bag_slot_index ASC
			 LIMIT 1
			   FOR UPDATE
		`, characterID, ElixirItemID).Scan(&targetID, &defID, &stackCount)
	}
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return InventorySnapshot{}, ErrConsumableEmpty
		}
		return InventorySnapshot{}, err
	}

	def, ok := s.catalog.Items[defID]
	if !ok || gamedata.MapSQLItemCategory(def.ItemType) != "CONSUMABLE" {
		return InventorySnapshot{}, ErrConsumableEmpty
	}

	if stackCount <= 1 {
		if _, err := tx.Exec(ctx, `DELETE FROM inventory_items WHERE id = $1`, targetID); err != nil {
			return InventorySnapshot{}, err
		}
	} else {
		if _, err := tx.Exec(ctx, `UPDATE inventory_items SET stack_count = stack_count - 1, updated_at = NOW() WHERE id = $1`, targetID); err != nil {
			return InventorySnapshot{}, err
		}
	}

	if _, _, _, err := RecompileAndPersistAttributesTx(ctx, tx, s.catalog, characterID, ElixirHealHP, ElixirHealKi); err != nil {
		return InventorySnapshot{}, err
	}

	snap, err := s.getSnapshotInTx(ctx, tx, characterID)
	if err != nil {
		return InventorySnapshot{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return InventorySnapshot{}, err
	}
	snap.ActionResult = fmt.Sprintf("Su dung [%s]: Hoi +%.0f HP & +%.0f Ki!", def.DisplayName, ElixirHealHP, ElixirHealKi)
	return snap, nil
}

// ToggleItemLockFenced sets or clears is_player_locked on an inventory item to prevent accidental NPC sale.
func (s *Service) ToggleItemLockFenced(
	ctx context.Context,
	characterID uuid.UUID,
	fencingEpoch int64,
	itemInstanceID uuid.UUID,
	locked bool,
) (InventorySnapshot, error) {
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return InventorySnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, _, err := lockCharacterFencedTx(ctx, tx, characterID, fencingEpoch); err != nil {
		return InventorySnapshot{}, err
	}

	tag, err := tx.Exec(ctx, `
		UPDATE inventory_items SET is_player_locked = $3, updated_at = NOW()
		 WHERE id = $1 AND character_id = $2
	`, itemInstanceID, characterID, locked)
	if err != nil {
		return InventorySnapshot{}, err
	}
	if tag.RowsAffected() == 0 {
		return InventorySnapshot{}, ErrItemNotFound
	}

	snap, err := s.getSnapshotInTx(ctx, tx, characterID)
	if err != nil {
		return InventorySnapshot{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return InventorySnapshot{}, err
	}
	return snap, nil
}

// SellItemFenced sells an unlocked INVENTORY item to NPC for Vàng (standard_coin) with immutable currency_ledger entry.
func (s *Service) SellItemFenced(
	ctx context.Context,
	characterID uuid.UUID,
	fencingEpoch int64,
	serverID string,
	itemInstanceID uuid.UUID,
	quantity int,
	idempotencyKey string,
) (InventorySnapshot, error) {
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return InventorySnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, _, err := lockCharacterFencedTx(ctx, tx, characterID, fencingEpoch); err != nil {
		return InventorySnapshot{}, err
	}

	var defID, bagType string
	var stackCount int
	var isLocked bool
	var lockReason *string
	err = tx.QueryRow(ctx, `
		SELECT item_def_id, bag_type, stack_count, is_player_locked, lock_reason
		  FROM inventory_items
		 WHERE id = $1 AND character_id = $2
		   FOR UPDATE
	`, itemInstanceID, characterID).Scan(&defID, &bagType, &stackCount, &isLocked, &lockReason)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return InventorySnapshot{}, ErrItemNotFound
		}
		return InventorySnapshot{}, err
	}
	if bagType != "INVENTORY" {
		return InventorySnapshot{}, ErrCannotSellEquipped
	}
	if isLocked || lockReason != nil {
		return InventorySnapshot{}, ErrItemLocked
	}

	def, ok := s.catalog.Items[defID]
	if !ok || def.SellPriceCoin <= 0 {
		return InventorySnapshot{}, ErrNotSellable
	}

	sellQty := quantity
	if sellQty <= 0 || sellQty > stackCount {
		sellQty = stackCount
	}
	totalCoin := def.SellPriceCoin * int64(sellQty)

	if sellQty >= stackCount {
		if _, err := tx.Exec(ctx, `DELETE FROM inventory_items WHERE id = $1`, itemInstanceID); err != nil {
			return InventorySnapshot{}, err
		}
	} else {
		if _, err := tx.Exec(ctx, `UPDATE inventory_items SET stack_count = stack_count - $2, updated_at = NOW() WHERE id = $1`, itemInstanceID, sellQty); err != nil {
			return InventorySnapshot{}, err
		}
	}

	idem := strings.TrimSpace(idempotencyKey)
	if idem == "" {
		idem = fmt.Sprintf("sell:%s:q%d", itemInstanceID.String(), stackCount)
	}
	if _, err := economy.ApplyCoinDeltaTx(ctx, tx, characterID, fencingEpoch, serverID, totalCoin, "NPC_SELL", "ITEM", itemInstanceID.String(), idem); err != nil {
		return InventorySnapshot{}, err
	}

	snap, err := s.getSnapshotInTx(ctx, tx, characterID)
	if err != nil {
		return InventorySnapshot{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return InventorySnapshot{}, err
	}
	snap.CoinsDelta = totalCoin
	snap.ActionResult = fmt.Sprintf("Da ban %dx [%s] nhan +%d Vang!", sellQty, def.DisplayName, totalCoin)
	return snap, nil
}

// EnhanceEquipmentFenced upgrades an equipment item (+1..+10) using Vàng + Crimson Ore with CSPRNG + Safe Floor + Pity protection.
func (s *Service) EnhanceEquipmentFenced(
	ctx context.Context,
	characterID uuid.UUID,
	fencingEpoch int64,
	serverID string,
	itemInstanceID uuid.UUID,
	idempotencyKey string,
) (InventorySnapshot, error) {
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return InventorySnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, _, err := lockCharacterFencedTx(ctx, tx, characterID, fencingEpoch); err != nil {
		return InventorySnapshot{}, err
	}

	var defID, bagType string
	var curLv int16
	var socketRaw []byte
	err = tx.QueryRow(ctx, `
		SELECT item_def_id, bag_type, enhance_level, socket_data
		  FROM inventory_items
		 WHERE id = $1 AND character_id = $2
		   FOR UPDATE
	`, itemInstanceID, characterID).Scan(&defID, &bagType, &curLv, &socketRaw)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return InventorySnapshot{}, ErrItemNotFound
		}
		return InventorySnapshot{}, err
	}

	def, ok := s.catalog.Items[defID]
	if !ok || def.EquipSpec == nil {
		return InventorySnapshot{}, ErrNotEquippable
	}
	if curLv >= def.EquipSpec.MaxEnhanceLevel {
		return InventorySnapshot{}, ErrMaxEnhanceReached
	}

	targetLv := curLv + 1
	curve, ok := s.catalog.EnhanceByLv[targetLv]
	if !ok {
		return InventorySnapshot{}, ErrMaxEnhanceReached
	}

	// Lock & verify required material (e.g. item.mat_crimson_ore_t1) in INVENTORY
	matRows, err := tx.Query(ctx, `
		SELECT id, stack_count
		  FROM inventory_items
		 WHERE character_id = $1 AND bag_type = 'INVENTORY' AND item_def_id = $2
		 ORDER BY bag_slot_index ASC
		   FOR UPDATE
	`, characterID, curve.MaterialItemID)
	if err != nil {
		return InventorySnapshot{}, err
	}
	type matStack struct {
		id    uuid.UUID
		count int
	}
	var mats []matStack
	totalMat := 0
	for matRows.Next() {
		var m matStack
		if err := matRows.Scan(&m.id, &m.count); err != nil {
			matRows.Close()
			return InventorySnapshot{}, err
		}
		totalMat += m.count
		mats = append(mats, m)
	}
	matRows.Close()

	if totalMat < curve.MaterialCount {
		return InventorySnapshot{}, ErrInsufficientMat
	}

	// Debit Vàng via append-only currency_ledger
	idem := strings.TrimSpace(idempotencyKey)
	if idem == "" {
		idem = fmt.Sprintf("enhance:%s:lv%d:pity%d", itemInstanceID.String(), targetLv, extractEnhancePity(socketRaw))
	}
	if _, err := economy.ApplyCoinDeltaTx(ctx, tx, characterID, fencingEpoch, serverID, -curve.StandardCoinCost, "ENHANCE_COST", "ITEM", itemInstanceID.String(), idem); err != nil {
		return InventorySnapshot{}, err
	}

	// Consume material stacks
	needMat := curve.MaterialCount
	for _, m := range mats {
		if needMat <= 0 {
			break
		}
		take := min(needMat, m.count)
		if take >= m.count {
			if _, err := tx.Exec(ctx, `DELETE FROM inventory_items WHERE id = $1`, m.id); err != nil {
				return InventorySnapshot{}, err
			}
		} else {
			if _, err := tx.Exec(ctx, `UPDATE inventory_items SET stack_count = stack_count - $2, updated_at = NOW() WHERE id = $1`, m.id, take); err != nil {
				return InventorySnapshot{}, err
			}
		}
		needMat -= take
	}

	// CSPRNG Roll + Hard Pity / Safe Downgrade Floor (NEVER destroys equipment!)
	curPity := extractEnhancePity(socketRaw)
	effectiveBP := curve.SuccessRateBP + int64(curPity)*curve.BonusBPPerPity
	guaranteedByPity := (curve.HardPityThreshold > 0 && curPity+1 >= curve.HardPityThreshold)
	rollN, err := rand.Int(rand.Reader, big.NewInt(10000))
	if err != nil {
		return InventorySnapshot{}, err
	}
	succeeded := guaranteedByPity || (rollN.Int64() < effectiveBP)

	newLv := curLv
	newPity := curPity
	if succeeded {
		newLv = targetLv
		newPity = 0
	} else {
		newPity = curPity + 1
		if curve.DowngradeOnFail > 0 && curLv > curve.DowngradeFloorLevel {
			newLv = max(curve.DowngradeFloorLevel, curLv-curve.DowngradeOnFail)
		}
	}

	newSocketBytes := encodeEnhancePity(newPity)
	if _, err := tx.Exec(ctx, `
		UPDATE inventory_items
		   SET enhance_level = $2, socket_data = $3::jsonb, is_bound = TRUE, updated_at = NOW()
		 WHERE id = $1
	`, itemInstanceID, newLv, string(newSocketBytes)); err != nil {
		return InventorySnapshot{}, err
	}

	if bagType == "EQUIPPED" {
		if _, _, _, err := RecompileAndPersistAttributesTx(ctx, tx, s.catalog, characterID, 0, 0); err != nil {
			return InventorySnapshot{}, err
		}
	}

	snap, err := s.getSnapshotInTx(ctx, tx, characterID)
	if err != nil {
		return InventorySnapshot{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return InventorySnapshot{}, err
	}

	snap.EnhanceSucceeded = &succeeded
	snap.CoinsDelta = -curve.StandardCoinCost
	if succeeded {
		snap.ActionResult = fmt.Sprintf("CUONG HOA THANH CONG! [%s] len +%d (-%d Vang)", def.DisplayName, newLv, curve.StandardCoinCost)
	} else {
		snap.ActionResult = fmt.Sprintf("Cuong hoa that bai (Khong vo do! Cap +%d, Bao hiem Pity %d)", newLv, newPity)
	}
	return snap, nil
}

func clampFloat(v, minV, maxV float64) float64 {
	if v < minV {
		return minV
	}
	if v > maxV {
		return maxV
	}
	return v
}
