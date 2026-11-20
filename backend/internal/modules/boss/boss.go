package boss

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
	"sync"
	"time"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/modules/economy"
	"github.com/infinite-universe/backend/internal/modules/gamedata"
	"github.com/infinite-universe/backend/internal/modules/inventory"
	"github.com/infinite-universe/backend/internal/modules/loot"
	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

const (
	DefaultBossID                = "boss.highland_titan_01"
	DefaultTitanTokenID          = "token.boss_highland_titan"
	DefaultMinContributionRatio  = 0.02
	HardPityDryKillThreshold     = 4 // 4 consecutive dry kills -> 5th kill guarantees 100% Epic/Legendary Boss drop!
)

var (
	ErrStaleFencingEpoch     = errors.New("stale fencing_epoch rejected during boss/pity settlement")
	ErrInvalidBossSettlement = errors.New("boss settlement requires idempotency_key and positive participant damage")
	ErrPityEntryNotFound     = errors.New("unknown pity_shop entry_id")
	ErrInsufficientPityToken = errors.New("insufficient boss pity tokens for selected reward")
	ErrPityDailyLimitReached = errors.New("daily purchase limit reached for selected pity shop entry")
)

type ParticipantDamageInput struct {
	CharacterID   uuid.UUID `json:"character_id"`
	CharacterName string    `json:"character_name"`
	FencingEpoch  int64     `json:"fencing_epoch"`
	DamageDealt   float64   `json:"damage_dealt"`
}

type SettleBossKillInput struct {
	BossID          string                   `json:"boss_id"`
	ZoneID          string                   `json:"zone_id"`
	ChannelID       int                      `json:"channel_id"`
	InstanceID      string                   `json:"instance_id,omitempty"`
	ServerID        string                   `json:"server_id"`
	FightDurationMs int                      `json:"fight_duration_ms"`
	IdempotencyKey  string                   `json:"idempotency_key"`
	Participants    []ParticipantDamageInput `json:"participants"`
}

type ParticipantAuditJSON struct {
	CharacterID   string  `json:"character_id"`
	CharacterName string  `json:"character_name"`
	Damage        int64   `json:"damage"`
	SharePct      float64 `json:"share_pct"`
	Rank          int     `json:"rank"`
	Eligible      bool    `json:"eligible"`
}

type ParticipantRewardOutcome struct {
	CharacterID         string                     `json:"character_id"`
	CharacterName       string                     `json:"character_name"`
	Rank                int                        `json:"rank"`
	DamageDealt         int64                      `json:"damage_dealt"`
	SharePct            float64                    `json:"share_pct"`
	Eligible            bool                       `json:"eligible"`
	CoinsGranted        int64                      `json:"coins_granted"`
	NewCoinBalance      int64                      `json:"standard_coin"`
	TokensGranted       int                        `json:"tokens_granted"`
	NewTokenBalance     int                        `json:"token_balance"`
	ConsecutiveDryKills int                        `json:"consecutive_dry_kills"`
	HardPityTriggered   bool                       `json:"hard_pity_triggered"`
	ItemsDropped        []loot.DroppedItemSummary  `json:"items_dropped"`
}

type SettleBossKillResult struct {
	KillID           string                     `json:"kill_id"`
	BossID           string                     `json:"boss_id"`
	ZoneID           string                     `json:"zone_id"`
	ChannelID        int                        `json:"channel_id"`
	TotalDamageDealt int64                      `json:"total_damage_dealt"`
	TopKillerCharID  string                     `json:"top_killer_char_id"`
	TopKillerName    string                     `json:"top_killer_name"`
	Announcement     string                     `json:"announcement"`
	Rewards          []ParticipantRewardOutcome `json:"rewards"`
	Idempotent       bool                       `json:"idempotent"`
}

type PityShopItemView struct {
	EntryID          string `json:"entry_id"`
	TokenItemID      string `json:"token_item_id"`
	TokenCost        int    `json:"token_cost"`
	StandardCoinCost int64  `json:"standard_coin_cost"`
	RewardItemID     string `json:"reward_item_id"`
	RewardName       string `json:"reward_name"`
	RewardRarity     string `json:"reward_rarity"`
	RewardQuantity   int    `json:"reward_quantity"`
	DailyLimit       int    `json:"daily_limit"` // 0 = unlimited
	DailyPurchased   int    `json:"daily_purchased"`
}

type PityShopSnapshot struct {
	TokenID             string             `json:"token_id"`
	TokenBalance        int                `json:"token_balance"`
	LifetimeEarned      int                `json:"lifetime_earned"`
	LifetimeRedeemed    int                `json:"lifetime_redeemed"`
	ConsecutiveDryKills int                `json:"consecutive_dry_kills"`
	HardPityThreshold   int                `json:"hard_pity_threshold"`
	StandardCoin        int64              `json:"standard_coin"`
	Entries             []PityShopItemView `json:"entries"`
	ActionMessage       string             `json:"action_message,omitempty"`
}

type Service struct {
	pool    *pgxpool.Pool
	catalog *gamedata.Catalog
	lootSvc *loot.Service

	idemMu    sync.Mutex
	seenKills map[string]SettleBossKillResult
	inFlight  map[string]chan struct{}
}

func NewService(pool *pgxpool.Pool, catalog *gamedata.Catalog, lootSvc *loot.Service) *Service {
	return &Service{
		pool:      pool,
		catalog:   catalog,
		lootSvc:   lootSvc,
		seenKills: make(map[string]SettleBossKillResult),
		inFlight:  make(map[string]chan struct{}),
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
	off, err := cryptoRandInt64(maxIncl - minIncl + 1)
	if err != nil {
		return minIncl, err
	}
	return minIncl + off, nil
}

func isEpicOrHigher(rarity string) bool {
	switch strings.ToLower(strings.TrimSpace(rarity)) {
	case "rarity.epic", "rarity.legendary", "rarity.mythic":
		return true
	default:
		return false
	}
}

// SettleBossKillFenced records boss_kill_history and distributes >= 2% contribution rewards,
// guaranteed boss_pity_tokens, and 5-kill hard-pity Epic/Legendary protection in a single ACID transaction.
func (s *Service) SettleBossKillFenced(ctx context.Context, in SettleBossKillInput) (SettleBossKillResult, error) {
	idemKey := strings.TrimSpace(in.IdempotencyKey)
	if idemKey == "" || len(in.Participants) == 0 {
		return SettleBossKillResult{}, ErrInvalidBossSettlement
	}

	for {
		s.idemMu.Lock()
		if cached, ok := s.seenKills[idemKey]; ok {
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
				return SettleBossKillResult{}, ctx.Err()
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

	bossID := strings.TrimSpace(in.BossID)
	if bossID == "" {
		bossID = DefaultBossID
	}
	bossDef, ok := s.catalog.Monsters[bossID]
	if !ok {
		bossID = DefaultBossID
		bossDef = s.catalog.Monsters[bossID]
	}

	zoneID := strings.TrimSpace(in.ZoneID)
	if zoneID == "" {
		zoneID = "zone.p01_highland_01"
	}
	channelID := max(1, in.ChannelID)
	serverID := strings.TrimSpace(in.ServerID)
	if serverID == "" {
		serverID = "ue5-ded-7778"
	}
	fightMs := max(0, in.FightDurationMs)

	// Merge any duplicate participant UUIDs & sort by damage descending
	type mergedPart struct {
		id     uuid.UUID
		name   string
		epoch  int64
		damage int64
	}
	byID := make(map[uuid.UUID]*mergedPart)
	for _, p := range in.Participants {
		if p.CharacterID == uuid.Nil || p.DamageDealt <= 0 {
			continue
		}
		dmg := max(int64(1), int64(math.Round(p.DamageDealt)))
		if ex, exists := byID[p.CharacterID]; exists {
			ex.damage += dmg
			if p.FencingEpoch > ex.epoch {
				ex.epoch = p.FencingEpoch
			}
			if p.CharacterName != "" {
				ex.name = p.CharacterName
			}
		} else {
			byID[p.CharacterID] = &mergedPart{
				id:     p.CharacterID,
				name:   strings.TrimSpace(p.CharacterName),
				epoch:  p.FencingEpoch,
				damage: dmg,
			}
		}
	}
	if len(byID) == 0 {
		return SettleBossKillResult{}, ErrInvalidBossSettlement
	}

	ranked := make([]*mergedPart, 0, len(byID))
	var totalDamage int64
	for _, mp := range byID {
		totalDamage += mp.damage
		ranked = append(ranked, mp)
	}
	sort.Slice(ranked, func(i, j int) bool {
		if ranked[i].damage != ranked[j].damage {
			return ranked[i].damage > ranked[j].damage
		}
		return strings.Compare(ranked[i].id.String(), ranked[j].id.String()) < 0
	})

	minRatio := bossDef.Rewards.MinContributionRatio
	if minRatio <= 0 {
		minRatio = DefaultMinContributionRatio
	}
	tokenID := DefaultTitanTokenID
	if bossDef.Rewards.GuaranteedPityTokenID != nil && *bossDef.Rewards.GuaranteedPityTokenID != "" {
		tokenID = *bossDef.Rewards.GuaranteedPityTokenID
	}
	tokMin := max(1, bossDef.Rewards.GuaranteedPityTokenMin)
	tokMax := max(tokMin, bossDef.Rewards.GuaranteedPityTokenMax)

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return SettleBossKillResult{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	// DB-level idempotency check
	var existingKillID uuid.UUID
	if err := tx.QueryRow(ctx, `SELECT kill_id FROM boss_kill_history WHERE idempotency_key = $1`, idemKey).Scan(&existingKillID); err == nil {
		return SettleBossKillResult{
			KillID:           existingKillID.String(),
			BossID:           bossID,
			ZoneID:           zoneID,
			ChannelID:        channelID,
			TotalDamageDealt: totalDamage,
			Rewards:          []ParticipantRewardOutcome{},
			Idempotent:       true,
		}, nil
	}

	// Lock all participant character rows in deterministic lexicographical UUID order to prevent deadlocks!
	sortedUUIDs := make([]uuid.UUID, 0, len(ranked))
	for _, r := range ranked {
		sortedUUIDs = append(sortedUUIDs, r.id)
	}
	sort.Slice(sortedUUIDs, func(i, j int) bool {
		return strings.Compare(sortedUUIDs[i].String(), sortedUUIDs[j].String()) < 0
	})

	validEpochs := make(map[uuid.UUID]int64, len(sortedUUIDs))
	charNames := make(map[uuid.UUID]string, len(sortedUUIDs))
	for _, cid := range sortedUUIDs {
		var dbEpoch int64
		var dbName string
		if err := tx.QueryRow(ctx, `
			SELECT fencing_epoch, character_name
			  FROM characters
			 WHERE id = $1 AND deleted_at IS NULL
			   FOR UPDATE
		`, cid).Scan(&dbEpoch, &dbName); err == nil {
			validEpochs[cid] = dbEpoch
			charNames[cid] = dbName
		}
	}

	auditList := make([]ParticipantAuditJSON, 0, len(ranked))
	outcomes := make([]ParticipantRewardOutcome, 0, len(ranked))

	lt, hasLT := s.catalog.LootTables[bossDef.Rewards.LootTableID]
	topKillerUUID := ranked[0].id
	topKillerName := ranked[0].name
	if n := charNames[topKillerUUID]; n != "" {
		topKillerName = n
	}

	for idx, mp := range ranked {
		rank := idx + 1
		if n := charNames[mp.id]; n != "" {
			mp.name = n
		}
		share := float64(mp.damage) / float64(totalDamage)
		shareRounded := math.Round(share*10000.0) / 10000.0
		dbEpoch, exists := validEpochs[mp.id]
		fencedOK := exists && mp.epoch >= 1 && dbEpoch == mp.epoch
		eligible := fencedOK && (share+1e-9 >= minRatio)

		auditList = append(auditList, ParticipantAuditJSON{
			CharacterID:   mp.id.String(),
			CharacterName: mp.name,
			Damage:        mp.damage,
			SharePct:      shareRounded,
			Rank:          rank,
			Eligible:      eligible,
		})

		outcome := ParticipantRewardOutcome{
			CharacterID:   mp.id.String(),
			CharacterName: mp.name,
			Rank:          rank,
			DamageDealt:   mp.damage,
			SharePct:      shareRounded,
			Eligible:      eligible,
			ItemsDropped:  []loot.DroppedItemSummary{},
		}
		if !eligible {
			outcomes = append(outcomes, outcome)
			continue
		}

		// 1. Grant Boss Vàng via immutable currency_ledger
		coins, err := cryptoRandRange(bossDef.Rewards.CoinMin, bossDef.Rewards.CoinMax)
		if err != nil {
			return SettleBossKillResult{}, err
		}
		if coins > 0 {
			newBal, err := economy.ApplyCoinDeltaTx(
				ctx, tx, mp.id, mp.epoch, serverID, coins,
				"BOSS_DROP", "BOSS_KILL", bossID, fmt.Sprintf("boss_coin:%s:%s", idemKey, mp.id.String()),
			)
			if err != nil {
				return SettleBossKillResult{}, err
			}
			outcome.CoinsGranted = coins
			outcome.NewCoinBalance = newBal
		}

		// 2. Ensure & lock boss_pity_tokens row
		if _, err := tx.Exec(ctx, `
			INSERT INTO boss_pity_tokens (character_id, token_id, amount, consecutive_dry_kills, lifetime_earned, lifetime_redeemed)
			VALUES ($1, $2, 0, 0, 0, 0)
			ON CONFLICT (character_id, token_id) DO NOTHING
		`, mp.id, tokenID); err != nil {
			return SettleBossKillResult{}, err
		}

		var curTokens, curDryKills int
		if err := tx.QueryRow(ctx, `
			SELECT amount, consecutive_dry_kills
			  FROM boss_pity_tokens
			 WHERE character_id = $1 AND token_id = $2
			   FOR UPDATE
		`, mp.id, tokenID).Scan(&curTokens, &curDryKills); err != nil {
			return SettleBossKillResult{}, err
		}

		// 3. Roll Personal Instanced Boss Loot Table
		gotRareEquip := false
		dropSeq := 0
		grantBossItem := func(itemDef gamedata.ItemDef, qty int, pityGuaranteed bool) error {
			var affixes []inventory.RolledAffix
			if itemDef.EquipSpec != nil && itemDef.EquipSpec.RandomAffixCount > 0 && s.lootSvc != nil {
				var afErr error
				affixes, afErr = s.lootSvc.RollAffixesCSPRNG(
					itemDef.EquipSpec.Slot,
					itemDef.RequiredLevel,
					itemDef.EquipSpec.RandomAffixCount,
					itemDef.EquipSpec.AffixTierPool,
				)
				if afErr != nil {
					return afErr
				}
			}

			instID, _, addErr := inventory.AddOrStackItemTx(ctx, tx, s.catalog, mp.id, itemDef.ItemID, qty, affixes, false, "BOSS_DROP")
			if errors.Is(addErr, inventory.ErrBagFull) {
				return nil
			}
			if addErr != nil {
				return addErr
			}

			dropSeq++
			lootIdem := fmt.Sprintf("boss_loot:%s:%s:%d", idemKey, mp.id.String(), dropSeq)
			affixBytes := []byte("[]")
			if len(affixes) > 0 {
				affixBytes, _ = json.Marshal(affixes)
			}
			var instPtr *uuid.UUID
			if instID != uuid.Nil {
				cp := instID
				instPtr = &cp
			}

			if _, err := tx.Exec(ctx, `
				INSERT INTO loot_history (
					idempotency_key, character_id, item_instance_id, item_def_id,
					rarity, quantity, source_entity_id, zone_id, channel_id,
					was_pity_guaranteed, rolled_affixes_snapshot
				) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11::jsonb)
				ON CONFLICT (idempotency_key) DO NOTHING
			`, lootIdem, mp.id, instPtr, itemDef.ItemID, itemDef.Rarity, qty, bossID, zoneID, channelID, pityGuaranteed, string(affixBytes)); err != nil {
				return err
			}

			if isEpicOrHigher(itemDef.Rarity) {
				gotRareEquip = true
			}
			outcome.ItemsDropped = append(outcome.ItemsDropped, loot.DroppedItemSummary{
				InstanceID:  instID,
				ItemDefID:   itemDef.ItemID,
				DisplayName: itemDef.DisplayName,
				Rarity:      itemDef.Rarity,
				Quantity:    qty,
				Affixes:     affixes,
			})
			return nil
		}

		if hasLT {
			for _, grp := range lt.RollGroups {
				trigBP := int64(math.Round(grp.GroupTriggerChance * 10000.0))
				rTrig, err := cryptoRandInt64(10000)
				if err != nil {
					return SettleBossKillResult{}, err
				}
				if rTrig >= trigBP || len(grp.Entries) == 0 {
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
						return SettleBossKillResult{}, err
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
					itemDef, ok := s.catalog.Items[chosen.RefID]
					if !ok {
						continue
					}
					q64, err := cryptoRandRange(int64(max(1, chosen.MinQty)), int64(max(chosen.MinQty, chosen.MaxQty)))
					if err != nil {
						return SettleBossKillResult{}, err
					}
					if err := grantBossItem(itemDef, int(q64), false); err != nil {
						return SettleBossKillResult{}, err
					}
				}
			}
		}

		// 4. 5th-Kill Hard Pity Protection: if 4 prior consecutive kills had 0 Epic/Legendary drops, force 100% Boss Equipment drop!
		if !gotRareEquip && curDryKills >= HardPityDryKillThreshold && hasLT {
			var rarePool []gamedata.LootEntry
			var rareWeight int64
			for _, grp := range lt.RollGroups {
				for _, e := range grp.Entries {
					if e.EntryType == "ITEM" && e.Weight > 0 {
						if def, ok := s.catalog.Items[e.RefID]; ok && isEpicOrHigher(def.Rarity) {
							rarePool = append(rarePool, e)
							rareWeight += e.Weight
						}
					}
				}
			}
			if len(rarePool) > 0 && rareWeight > 0 {
				wRoll, err := cryptoRandInt64(rareWeight)
				if err != nil {
					return SettleBossKillResult{}, err
				}
				picked := rarePool[len(rarePool)-1]
				var acc int64
				for _, e := range rarePool {
					acc += e.Weight
					if wRoll < acc {
						picked = e
						break
					}
				}
				if def, ok := s.catalog.Items[picked.RefID]; ok {
					if err := grantBossItem(def, 1, true); err != nil {
						return SettleBossKillResult{}, err
					}
					outcome.HardPityTriggered = true
				}
			}
		}

		// 5. Grant Guaranteed Boss Pity Tokens (1..2) & update consecutive_dry_kills
		tokGain64, err := cryptoRandRange(int64(tokMin), int64(tokMax))
		if err != nil {
			return SettleBossKillResult{}, err
		}
		tokGain := int(tokGain64)
		newDryKills := curDryKills + 1
		if gotRareEquip {
			newDryKills = 0
		}
		newTokens := curTokens + tokGain

		if _, err := tx.Exec(ctx, `
			UPDATE boss_pity_tokens
			   SET amount = $3,
			       consecutive_dry_kills = $4,
			       lifetime_earned = lifetime_earned + $5,
			       updated_at = NOW()
			 WHERE character_id = $1 AND token_id = $2
		`, mp.id, tokenID, newTokens, newDryKills, tokGain); err != nil {
			return SettleBossKillResult{}, err
		}

		outcome.TokensGranted = tokGain
		outcome.NewTokenBalance = newTokens
		outcome.ConsecutiveDryKills = newDryKills
		outcomes = append(outcomes, outcome)
	}

	// 6. Insert immutable boss_kill_history audit row
	partBytes, _ := json.Marshal(auditList)
	spawnedAt := time.Now().UTC().Add(-time.Duration(fightMs) * time.Millisecond)
	var instPtr *string
	if strings.TrimSpace(in.InstanceID) != "" {
		ic := strings.TrimSpace(in.InstanceID)
		instPtr = &ic
	}

	var killID uuid.UUID
	if err := tx.QueryRow(ctx, `
		INSERT INTO boss_kill_history (
			idempotency_key, boss_id, zone_id, channel_id, instance_id,
			fight_duration_ms, total_damage_dealt, top_killer_char_id,
			participants, spawned_at, killed_at
		) VALUES (
			$1, $2, $3, $4, $5,
			$6, $7, $8,
			$9::jsonb, $10, NOW()
		)
		RETURNING kill_id
	`, idemKey, bossID, zoneID, channelID, instPtr, fightMs, totalDamage, topKillerUUID, string(partBytes), spawnedAt).Scan(&killID); err != nil {
		return SettleBossKillResult{}, fmt.Errorf("insert boss_kill_history: %w", err)
	}

	if err := tx.Commit(ctx); err != nil {
		return SettleBossKillResult{}, err
	}

	announce := fmt.Sprintf("Chien binh %s cung to doi da ha guc World Boss Highland Titan tai Khu %d!", topKillerName, channelID)
	res := SettleBossKillResult{
		KillID:           killID.String(),
		BossID:           bossID,
		ZoneID:           zoneID,
		ChannelID:        channelID,
		TotalDamageDealt: totalDamage,
		TopKillerCharID:  topKillerUUID.String(),
		TopKillerName:    topKillerName,
		Announcement:     announce,
		Rewards:          outcomes,
	}

	s.idemMu.Lock()
	if len(s.seenKills) > 1024 {
		clear(s.seenKills)
	}
	s.seenKills[idemKey] = res
	s.idemMu.Unlock()

	return res, nil
}

// GetPityShopSnapshot returns a character's boss_pity_tokens state & all 7 pity_shop.json entries with daily purchase counts.
func (s *Service) GetPityShopSnapshot(ctx context.Context, characterID uuid.UUID, actionMsg string) (PityShopSnapshot, error) {
	snap := PityShopSnapshot{
		TokenID:           DefaultTitanTokenID,
		HardPityThreshold: HardPityDryKillThreshold + 1,
		Entries:           make([]PityShopItemView, 0, len(s.catalog.PityShop)),
		ActionMessage:     actionMsg,
	}

	_ = s.pool.QueryRow(ctx, `
		SELECT amount, consecutive_dry_kills, lifetime_earned, lifetime_redeemed
		  FROM boss_pity_tokens
		 WHERE character_id = $1 AND token_id = $2
	`, characterID, DefaultTitanTokenID).Scan(
		&snap.TokenBalance, &snap.ConsecutiveDryKills, &snap.LifetimeEarned, &snap.LifetimeRedeemed,
	)
	_ = s.pool.QueryRow(ctx, `SELECT COALESCE(standard_coin, 0) FROM character_wallets WHERE character_id = $1`, characterID).Scan(&snap.StandardCoin)

	dailyCounts := make(map[string]int)
	rows, err := s.pool.Query(ctx, `
		SELECT idempotency_key
		  FROM loot_history
		 WHERE character_id = $1
		   AND was_pity_guaranteed = TRUE
		   AND source_entity_id LIKE 'pity_shop:%'
		   AND created_at >= date_trunc('day', NOW() AT TIME ZONE 'UTC')
	`, characterID)
	if err == nil {
		for rows.Next() {
			var k string
			if scanErr := rows.Scan(&k); scanErr == nil {
				// source_entity_id stored as pity_shop:<entry_id>; parse entry_id from second query or key prefix
				parts := strings.SplitN(k, ":", 3)
				if len(parts) >= 2 {
					dailyCounts[parts[1]]++
				}
			}
		}
		rows.Close()
	}

	for _, e := range s.catalog.PityShop {
		dLim := 0
		if e.DailyLimit != nil && *e.DailyLimit > 0 {
			dLim = *e.DailyLimit
		}
		snap.Entries = append(snap.Entries, PityShopItemView{
			EntryID:          e.EntryID,
			TokenItemID:      e.TokenItemID,
			TokenCost:        e.TokenCost,
			StandardCoinCost: e.StandardCoinCost,
			RewardItemID:     e.RewardItemID,
			RewardName:       e.RewardName,
			RewardRarity:     e.RewardRarity,
			RewardQuantity:   e.RewardQuantity,
			DailyLimit:       dLim,
			DailyPurchased:   dailyCounts[e.EntryID],
		})
	}
	return snap, nil
}

// RedeemPityShopEntryFenced exchanges boss_pity_tokens + standard_coin for a guaranteed item from pity_shop.json.
func (s *Service) RedeemPityShopEntryFenced(
	ctx context.Context,
	characterID uuid.UUID,
	fencingEpoch int64,
	serverID string,
	entryID string,
	idempotencyKey string,
) (PityShopSnapshot, error) {
	if fencingEpoch < 1 {
		return PityShopSnapshot{}, ErrStaleFencingEpoch
	}
	entry, ok := s.catalog.PityByID[strings.TrimSpace(entryID)]
	if !ok {
		return PityShopSnapshot{}, ErrPityEntryNotFound
	}
	itemDef, ok := s.catalog.Items[entry.RewardItemID]
	if !ok {
		return PityShopSnapshot{}, ErrPityEntryNotFound
	}
	cleanIdem := strings.TrimSpace(idempotencyKey)
	if cleanIdem == "" {
		cleanIdem = uuid.NewString()
	}
	lootIdemKey := fmt.Sprintf("pity:%s:%s", entry.EntryID, cleanIdem)
	if strings.TrimSpace(serverID) == "" {
		serverID = "ue5-ded-7777"
	}

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return PityShopSnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var zoneID string
	var chanID int
	if err := tx.QueryRow(ctx, `
		SELECT zone_id, last_channel_id
		  FROM characters
		 WHERE id = $1 AND fencing_epoch = $2 AND deleted_at IS NULL
		   FOR UPDATE
	`, characterID, fencingEpoch).Scan(&zoneID, &chanID); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return PityShopSnapshot{}, ErrStaleFencingEpoch
		}
		return PityShopSnapshot{}, err
	}

	// Idempotency check if already redeemed with this key
	var existingLoot uuid.UUID
	if err := tx.QueryRow(ctx, `SELECT loot_event_id FROM loot_history WHERE idempotency_key = $1`, lootIdemKey).Scan(&existingLoot); err == nil {
		_ = tx.Rollback(ctx)
		return s.GetPityShopSnapshot(ctx, characterID, fmt.Sprintf("Da doi %s x%d!", entry.RewardName, entry.RewardQuantity))
	}

	// Enforce daily_limit if configured in pity_shop.json
	if entry.DailyLimit != nil && *entry.DailyLimit > 0 {
		var usedToday int
		if err := tx.QueryRow(ctx, `
			SELECT COUNT(*)
			  FROM loot_history
			 WHERE character_id = $1
			   AND source_entity_id = $2
			   AND created_at >= date_trunc('day', NOW() AT TIME ZONE 'UTC')
		`, characterID, "pity_shop:"+entry.EntryID).Scan(&usedToday); err != nil {
			return PityShopSnapshot{}, err
		}
		if usedToday >= *entry.DailyLimit {
			return PityShopSnapshot{}, ErrPityDailyLimitReached
		}
	}

	var tokenBal int
	err = tx.QueryRow(ctx, `
		SELECT amount
		  FROM boss_pity_tokens
		 WHERE character_id = $1 AND token_id = $2
		   FOR UPDATE
	`, characterID, entry.TokenItemID).Scan(&tokenBal)
	if errors.Is(err, pgx.ErrNoRows) || tokenBal < entry.TokenCost {
		return PityShopSnapshot{}, ErrInsufficientPityToken
	}
	if err != nil {
		return PityShopSnapshot{}, err
	}

	if entry.StandardCoinCost > 0 {
		if _, err := economy.ApplyCoinDeltaTx(
			ctx, tx, characterID, fencingEpoch, serverID,
			-entry.StandardCoinCost, "PITY_SHOP_REDEEM", "PITY_SHOP", entry.EntryID, "coin:"+lootIdemKey,
		); err != nil {
			return PityShopSnapshot{}, err
		}
	}

	if _, err := tx.Exec(ctx, `
		UPDATE boss_pity_tokens
		   SET amount = amount - $3,
		       lifetime_redeemed = lifetime_redeemed + $3,
		       updated_at = NOW()
		 WHERE character_id = $1 AND token_id = $2
	`, characterID, entry.TokenItemID, entry.TokenCost); err != nil {
		return PityShopSnapshot{}, err
	}

	var affixes []inventory.RolledAffix
	if itemDef.EquipSpec != nil && itemDef.EquipSpec.RandomAffixCount > 0 && s.lootSvc != nil {
		affixes, err = s.lootSvc.RollAffixesCSPRNG(
			itemDef.EquipSpec.Slot,
			itemDef.RequiredLevel,
			itemDef.EquipSpec.RandomAffixCount,
			itemDef.EquipSpec.AffixTierPool,
		)
		if err != nil {
			return PityShopSnapshot{}, err
		}
	}

	instID, _, err := inventory.AddOrStackItemTx(ctx, tx, s.catalog, characterID, itemDef.ItemID, max(1, entry.RewardQuantity), affixes, false, "PITY_SHOP")
	if err != nil {
		return PityShopSnapshot{}, err
	}

	affixBytes := []byte("[]")
	if len(affixes) > 0 {
		affixBytes, _ = json.Marshal(affixes)
	}
	var instPtr *uuid.UUID
	if instID != uuid.Nil {
		cp := instID
		instPtr = &cp
	}

	if _, err := tx.Exec(ctx, `
		INSERT INTO loot_history (
			idempotency_key, character_id, item_instance_id, item_def_id,
			rarity, quantity, source_entity_id, zone_id, channel_id,
			was_pity_guaranteed, rolled_affixes_snapshot
		) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, TRUE, $10::jsonb)
	`, lootIdemKey, characterID, instPtr, itemDef.ItemID, itemDef.Rarity, max(1, entry.RewardQuantity), "pity_shop:"+entry.EntryID, zoneID, max(1, chanID), string(affixBytes)); err != nil {
		return PityShopSnapshot{}, err
	}

	if err := tx.Commit(ctx); err != nil {
		return PityShopSnapshot{}, err
	}

	return s.GetPityShopSnapshot(ctx, characterID, fmt.Sprintf("Doi thanh cong [%s] x%d (-%d Huy Chuong, -%d Vang)!", entry.RewardName, max(1, entry.RewardQuantity), entry.TokenCost, entry.StandardCoinCost))
}
