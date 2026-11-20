package loot

import (
	"context"
	"crypto/rand"
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"math/big"
	"strings"
	"sync"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/modules/economy"
	"github.com/infinite-universe/backend/internal/modules/gamedata"
	"github.com/infinite-universe/backend/internal/modules/inventory"
	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

var ErrStaleFencingEpoch = errors.New("stale fencing_epoch rejected by PostgreSQL lease fence")

type DroppedItemSummary struct {
	InstanceID  uuid.UUID               `json:"instance_id"`
	ItemDefID   string                  `json:"item_def_id"`
	DisplayName string                  `json:"display_name"`
	Rarity      string                  `json:"rarity"`
	Quantity    int                     `json:"quantity"`
	Affixes     []inventory.RolledAffix `json:"affixes,omitempty"`
}

type MobLootResult struct {
	CharacterID    uuid.UUID            `json:"character_id"`
	SourceEntityID string               `json:"source_entity_id"`
	CoinsGranted   int64                `json:"coins_granted"`
	NewCoinBalance int64                `json:"standard_coin"`
	ItemsDropped   []DroppedItemSummary `json:"items_dropped"`
	BagFullSkipped bool                 `json:"bag_full_skipped"`
	Idempotent     bool                 `json:"idempotent"`
}

type Service struct {
	pool    *pgxpool.Pool
	catalog *gamedata.Catalog

	idemMu   sync.Mutex
	seenLoot map[string]MobLootResult
	inFlight map[string]chan struct{}
}

func NewService(pool *pgxpool.Pool, catalog *gamedata.Catalog) *Service {
	return &Service{
		pool:     pool,
		catalog:  catalog,
		seenLoot: make(map[string]MobLootResult),
		inFlight: make(map[string]chan struct{}),
	}
}

func cryptoRandInt64(maxExclusive int64) (int64, error) {
	if maxExclusive <= 1 {
		return 0, nil
	}
	n, err := rand.Int(rand.Reader, big.NewInt(maxExclusive))
	if err != nil {
		return 0, err
	}
	return n.Int64(), nil
}

func cryptoRandRange(minIncl, maxIncl int64) (int64, error) {
	if maxIncl <= minIncl {
		return minIncl, nil
	}
	offset, err := cryptoRandInt64(maxIncl - minIncl + 1)
	if err != nil {
		return minIncl, err
	}
	return minIncl + offset, nil
}

// RollAffixesCSPRNG generates `count` non-duplicate affixes (by exclusive_group) valid for `slot` and `tierPool` using crypto/rand.
func (s *Service) RollAffixesCSPRNG(slot string, requiredLevel int, count int, tierPool int) ([]inventory.RolledAffix, error) {
	if count <= 0 || s.catalog == nil {
		return nil, nil
	}

	var candidates []gamedata.AffixDef
	for _, af := range s.catalog.Affixes {
		slotOK := false
		for _, allowed := range af.AllowedSlots {
			if allowed == slot {
				slotOK = true
				break
			}
		}
		if slotOK && af.RollWeight > 0 {
			candidates = append(candidates, af)
		}
	}

	usedGroups := make(map[string]bool)
	var out []inventory.RolledAffix

	for pick := 0; pick < count && len(candidates) > 0; pick++ {
		var totalWeight int64
		var pool []gamedata.AffixDef
		for _, c := range candidates {
			if !usedGroups[c.ExclusiveGroup] {
				pool = append(pool, c)
				totalWeight += c.RollWeight
			}
		}
		if len(pool) == 0 || totalWeight <= 0 {
			break
		}

		roll, err := cryptoRandInt64(totalWeight)
		if err != nil {
			return nil, err
		}
		chosen := pool[len(pool)-1]
		var acc int64
		for _, c := range pool {
			acc += c.RollWeight
			if roll < acc {
				chosen = c
				break
			}
		}
		usedGroups[chosen.ExclusiveGroup] = true

		// Pick matching tier (prefer tierPool or item level match, fallback to tier 1)
		selectedTier := chosen.Tiers[0]
		for _, t := range chosen.Tiers {
			if t.Tier == tierPool || (requiredLevel >= t.MinItemLevel && requiredLevel <= t.MaxItemLevel) {
				selectedTier = t
				break
			}
		}

		val := selectedTier.MinValue
		if selectedTier.Step > 0 && selectedTier.MaxValue > selectedTier.MinValue {
			steps := int64(math.Round((selectedTier.MaxValue - selectedTier.MinValue) / selectedTier.Step))
			if steps > 0 {
				sIdx, err := cryptoRandRange(0, steps)
				if err != nil {
					return nil, err
				}
				val = selectedTier.MinValue + float64(sIdx)*selectedTier.Step
				// Clean floating-point rounding to 4 decimal places
				val = math.Round(val*10000.0) / 10000.0
			}
		}

		out = append(out, inventory.RolledAffix{
			AffixID: chosen.AffixID,
			AttrID:  chosen.TargetAttrID,
			Tier:    selectedTier.Tier,
			Value:   val,
		})
	}

	return out, nil
}

// RollMonsterLootFenced executes an authoritative CSPRNG Personal Loot + Vàng drop for a defeated monster under fencing_epoch.
func (s *Service) RollMonsterLootFenced(
	ctx context.Context,
	characterID uuid.UUID,
	fencingEpoch int64,
	serverID string,
	sourceEntityID string,
	zoneID string,
	idempotencyKey string,
) (MobLootResult, error) {
	if fencingEpoch < 1 {
		return MobLootResult{}, ErrStaleFencingEpoch
	}
	idemKey := strings.TrimSpace(idempotencyKey)
	if idemKey == "" {
		return MobLootResult{}, errors.New("idempotency_key is required for monster loot roll")
	}
	if strings.TrimSpace(zoneID) == "" {
		zoneID = "zone.p01_west_hub"
	}
	if strings.TrimSpace(serverID) == "" {
		serverID = "ue5-ded-7777"
	}

	for {
		s.idemMu.Lock()
		if cached, ok := s.seenLoot[idemKey]; ok {
			s.idemMu.Unlock()
			cached.Idempotent = true
			return cached, nil
		}
		if waitCh, busy := s.inFlight[idemKey]; busy {
			s.idemMu.Unlock()
			select {
			case <-waitCh:
				continue
			case <-ctx.Done():
				return MobLootResult{}, ctx.Err()
			}
		}
		waitCh := make(chan struct{})
		s.inFlight[idemKey] = waitCh
		s.idemMu.Unlock()
		defer func() {
			s.idemMu.Lock()
			delete(s.inFlight, idemKey)
			close(waitCh)
			s.idemMu.Unlock()
		}()
		break
	}

	mobID := strings.TrimSpace(sourceEntityID)
	mobDef, ok := s.catalog.Monsters[mobID]
	if !ok {
		mobID = "mob.prototype_raider"
		mobDef = s.catalog.Monsters[mobID]
	}

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return MobLootResult{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var lockedID uuid.UUID
	if err := tx.QueryRow(ctx, `
		SELECT id FROM characters WHERE id = $1 AND fencing_epoch = $2 AND deleted_at IS NULL FOR UPDATE
	`, characterID, fencingEpoch).Scan(&lockedID); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return MobLootResult{}, ErrStaleFencingEpoch
		}
		return MobLootResult{}, err
	}

	res := MobLootResult{
		CharacterID:    characterID,
		SourceEntityID: mobID,
		ItemsDropped:   []DroppedItemSummary{},
	}

	// 1. Roll CSPRNG Vàng (Standard Coin) & record on immutable currency_ledger
	coins, err := cryptoRandRange(mobDef.Rewards.CoinMin, mobDef.Rewards.CoinMax)
	if err != nil {
		return MobLootResult{}, err
	}
	if coins > 0 {
		newBal, err := economy.ApplyCoinDeltaTx(
			ctx, tx, characterID, fencingEpoch, serverID, coins,
			"MOB_DROP", "MOB_KILL", mobID, "coin:"+idemKey,
		)
		if err != nil {
			return MobLootResult{}, err
		}
		res.CoinsGranted = coins
		res.NewCoinBalance = newBal
	}

	// 2. Roll CSPRNG Loot Table Groups
	if lt, ok := s.catalog.LootTables[mobDef.Rewards.LootTableID]; ok {
		dropCounter := 0
		for _, grp := range lt.RollGroups {
			triggerBP := int64(math.Round(grp.GroupTriggerChance * 10000.0))
			rollTrig, err := cryptoRandInt64(10000)
			if err != nil {
				return MobLootResult{}, err
			}
			if rollTrig >= triggerBP || len(grp.Entries) == 0 {
				continue
			}

			picks := max(1, grp.PickCount)
			for p := 0; p < picks; p++ {
				var totalW int64
				for _, e := range grp.Entries {
					if e.Weight > 0 {
						totalW += e.Weight
					}
				}
				if totalW <= 0 {
					break
				}
				wRoll, err := cryptoRandInt64(totalW)
				if err != nil {
					return MobLootResult{}, err
				}
				chosen := grp.Entries[len(grp.Entries)-1]
				var acc int64
				for _, e := range grp.Entries {
					acc += e.Weight
					if wRoll < acc {
						chosen = e
						break
					}
				}
				if chosen.EntryType != "ITEM" || chosen.RefID == "" {
					continue
				}
				itemDef, exists := s.catalog.Items[chosen.RefID]
				if !exists {
					continue
				}

				qty64, err := cryptoRandRange(int64(max(1, chosen.MinQty)), int64(max(chosen.MinQty, chosen.MaxQty)))
				if err != nil {
					return MobLootResult{}, err
				}
				qty := int(qty64)

				var affixes []inventory.RolledAffix
				if itemDef.EquipSpec != nil && itemDef.EquipSpec.RandomAffixCount > 0 {
					affixes, err = s.RollAffixesCSPRNG(
						itemDef.EquipSpec.Slot,
						itemDef.RequiredLevel,
						itemDef.EquipSpec.RandomAffixCount,
						itemDef.EquipSpec.AffixTierPool,
					)
					if err != nil {
						return MobLootResult{}, err
					}
				}

				instID, _, addErr := inventory.AddOrStackItemTx(ctx, tx, s.catalog, characterID, itemDef.ItemID, qty, affixes, false, "LOOT_DROP")
				if errors.Is(addErr, inventory.ErrBagFull) {
					res.BagFullSkipped = true
					continue
				}
				if addErr != nil {
					return MobLootResult{}, addErr
				}

				dropCounter++
				lootIdem := fmt.Sprintf("%s:item:%d", idemKey, dropCounter)
				affixSnapBytes := []byte("[]")
				if len(affixes) > 0 {
					affixSnapBytes, _ = json.Marshal(affixes)
				}
				var instPtr *uuid.UUID
				if instID != uuid.Nil {
					idCopy := instID
					instPtr = &idCopy
				}

				// Check idempotency in DB in case process restarted
				var existingLootID uuid.UUID
				if errCheck := tx.QueryRow(ctx, `SELECT loot_event_id FROM loot_history WHERE idempotency_key = $1`, lootIdem).Scan(&existingLootID); errors.Is(errCheck, pgx.ErrNoRows) {
					if _, err := tx.Exec(ctx, `
						INSERT INTO loot_history (
							idempotency_key, character_id, item_instance_id, item_def_id,
							rarity, quantity, source_entity_id, zone_id, channel_id,
							was_pity_guaranteed, rolled_affixes_snapshot
						) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, 1, FALSE, $9::jsonb)
					`, lootIdem, characterID, instPtr, itemDef.ItemID, itemDef.Rarity, qty, mobID, zoneID, string(affixSnapBytes)); err != nil {
						return MobLootResult{}, fmt.Errorf("insert loot_history: %w", err)
					}
				}

				res.ItemsDropped = append(res.ItemsDropped, DroppedItemSummary{
					InstanceID:  instID,
					ItemDefID:   itemDef.ItemID,
					DisplayName: itemDef.DisplayName,
					Rarity:      itemDef.Rarity,
					Quantity:    qty,
					Affixes:     affixes,
				})
			}
		}
	}

	if err := tx.Commit(ctx); err != nil {
		return MobLootResult{}, err
	}

	s.idemMu.Lock()
	if len(s.seenLoot) > 4096 {
		clear(s.seenLoot)
	}
	s.seenLoot[idemKey] = res
	s.idemMu.Unlock()

	return res, nil
}
