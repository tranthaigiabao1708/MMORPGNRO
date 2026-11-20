package httpapi_test

import (
	"context"
	"fmt"
	"io"
	"math"
	"net/http"
	"net/http/httptest"
	"sync"
	"testing"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/httpapi"
	"github.com/infinite-universe/backend/internal/modules/account"
	"github.com/infinite-universe/backend/internal/modules/auth"
	"github.com/infinite-universe/backend/internal/modules/character"
	"github.com/infinite-universe/backend/internal/modules/economy"
	"github.com/infinite-universe/backend/internal/modules/gamedata"
	"github.com/infinite-universe/backend/internal/modules/inventory"
	"github.com/infinite-universe/backend/internal/modules/loot"
	"github.com/infinite-universe/backend/internal/modules/world"
	"github.com/infinite-universe/backend/internal/platform/telemetry"
	"github.com/jackc/pgx/v5/pgxpool"
)

type step4TestEnv struct {
	pool    *pgxpool.Pool
	catalog *gamedata.Catalog
	authMgr *auth.Manager
	ecoSvc  *economy.Service
	invSvc  *inventory.Service
	lootSvc *loot.Service
	srv     *httptest.Server
}

func setupStep4Env(t *testing.T) *step4TestEnv {
	t.Helper()
	base := setupStep3Env(t)
	base.srv.Close()

	cat, err := gamedata.LoadDefaultCatalog()
	if err != nil {
		t.Fatalf("LoadDefaultCatalog: %v", err)
	}
	if err := cat.SyncItemDefinitions(context.Background(), base.pool); err != nil {
		t.Fatalf("SyncItemDefinitions: %v", err)
	}

	logger := telemetry.NewJSONLogger(io.Discard, "step4-test", telemetry.ParseLogLevel("error"))
	accSvc := account.NewService(base.pool)
	charSvc := character.NewService(base.pool)
	ecoSvc := economy.NewService(base.pool)
	invSvc := inventory.NewService(base.pool, cat)
	lootSvc := loot.NewService(base.pool, cat)
	coord := world.NewCoordinator(nil, "zone.p01_west_hub", "127.0.0.1:7777")

	h := httpapi.NewHandler(accSvc, base.authMgr, charSvc, coord, logger)
	h.AttachStep4Modules(cat, ecoSvc, invSvc, lootSvc)

	mux := http.NewServeMux()
	h.RegisterRoutes(mux)
	srv := httptest.NewServer(mux)
	t.Cleanup(srv.Close)

	return &step4TestEnv{
		pool:    base.pool,
		catalog: cat,
		authMgr: base.authMgr,
		ecoSvc:  ecoSvc,
		invSvc:  invSvc,
		lootSvc: lootSvc,
		srv:     srv,
	}
}

func createAndClaimHero(t *testing.T, env *step4TestEnv, username, charName string) (tok string, charID string, epoch int64) {
	t.Helper()
	_, reg := doJSON(t, "POST", env.srv.URL+"/v1/auth/register", "", nil, map[string]string{
		"username": username,
		"email":    username + "@iu.step4",
		"password": "Step4SecretPassword!99",
	})
	tok = reg["access_token"].(string)

	_, ch := doJSON(t, "POST", env.srv.URL+"/v1/characters", tok, nil, map[string]string{
		"character_name": charName,
		"race_id":        "race.zenith_warrior",
	})
	charID = ch["character_id"].(string)

	_, tkt := doJSON(t, "POST", env.srv.URL+"/v1/world/enter", tok, nil, map[string]string{"character_id": charID})
	code, claim := doJSON(t, "POST", env.srv.URL+"/internal/v1/lease/claim", "", env.authMgr, map[string]string{
		"ticket_id": tkt["ticket_id"].(string),
		"server_id": "ue5-ded-7777",
		"zone_id":   "zone.p01_west_hub",
	})
	if code != http.StatusOK {
		t.Fatalf("claim failed (%d): %+v", code, claim)
	}
	epoch = int64(claim["fencing_epoch"].(float64))
	return tok, charID, epoch
}

// Groups 4.1, 4.2, 4.3, 4.4 & 4.6:
// Starter Kit, CSPRNG Loot + Unique Affixes, Level-Gated 8-Slot Equip/Unequip, Consumable Healing, Safe Enhancement & Restart Persistence
func TestStep4InventoryLootEquipEnhanceAndPersistence(t *testing.T) {
	env := setupStep4Env(t)
	tok, charID, epoch := createAndClaimHero(t, env, "TrunksStep4", "FutureTrunks")
	charUUID := uuid.MustParse(charID)

	// 1. Verify item_definitions synced & Starter Kit granted (1x Tunic Lv.1, 3x Elixir, 4x Crimson Ore)
	code, inv0 := doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/get", "", env.authMgr, map[string]any{
		"character_id":  charID,
		"fencing_epoch": epoch,
		"server_id":     "ue5-ded-7777",
	})
	if code != http.StatusOK {
		t.Fatalf("get inventory failed (%d): %+v", code, inv0)
	}
	if int(inv0["used_bag_slots"].(float64)) != 3 || int(inv0["elixir_count"].(float64)) != 3 || int(inv0["crimson_ore_count"].(float64)) != 4 {
		t.Fatalf("unexpected starter kit: %+v", inv0)
	}

	// Find starter tunic instance_id
	var tunicID string
	for _, rawIt := range inv0["bag_items"].([]any) {
		it := rawIt.(map[string]any)
		if it["item_def_id"].(string) == "item.body_recruit_tunic_01" {
			tunicID = it["instance_id"].(string)
		}
	}
	if tunicID == "" {
		t.Fatal("starter tunic not found in bag_items")
	}

	// 2. Equip starter tunic (+80 MaxHP, +8 Defense) -> verify MaxHP 200->280 & Defense 20->28 immediately!
	code, equipResp := doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/equip", "", env.authMgr, map[string]any{
		"character_id":     charID,
		"fencing_epoch":    epoch,
		"server_id":        "ue5-ded-7777",
		"item_instance_id": tunicID,
	})
	if code != http.StatusOK {
		t.Fatalf("equip starter tunic failed (%d): %+v", code, equipResp)
	}
	if math.Abs(equipResp["max_hp"].(float64)-280.0) > 0.01 || math.Abs(equipResp["defense"].(float64)-28.0) > 0.01 {
		t.Fatalf("expected MaxHP=280 & Defense=28 after equipping Recruit Tunic, got %+v", equipResp)
	}

	// 3. Inject a Lv.5 Wolfhide Wraps into bag & verify Lv.1 character is rejected with 422 ERR_LEVEL_TOO_LOW!
	tx, _ := env.pool.Begin(context.Background())
	affixes, err := env.lootSvc.RollAffixesCSPRNG("slot.hands", 5, 2, 1)
	if err != nil || len(affixes) != 2 || affixes[0].AffixID == affixes[1].AffixID {
		t.Fatalf("expected 2 distinct CSPRNG affixes for slot.hands, got %+v (err=%v)", affixes, err)
	}
	lv5HandsID, _, err := inventory.AddOrStackItemTx(context.Background(), tx, env.catalog, charUUID, "item.hands_wolfhide_wraps_05", 1, affixes, false, "TEST")
	if err != nil {
		t.Fatalf("AddOrStackItemTx lv5 hands: %v", err)
	}
	_ = tx.Commit(context.Background())

	code, lvErrResp := doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/equip", "", env.authMgr, map[string]any{
		"character_id":     charID,
		"fencing_epoch":    epoch,
		"server_id":        "ue5-ded-7777",
		"item_instance_id": lv5HandsID.String(),
	})
	if code != http.StatusUnprocessableEntity || lvErrResp["error_code"] != "ERR_LEVEL_TOO_LOW" {
		t.Fatalf("expected 422 ERR_LEVEL_TOO_LOW when Lv.1 equips Lv.5 item, got %d %+v", code, lvErrResp)
	}

	// 4. Roll CSPRNG Monster Loot 4 times (earning Vàng + materials/gear) & verify idempotency on duplicate key!
	for k := 1; k <= 4; k++ {
		c1, r1 := doJSON(t, "POST", env.srv.URL+"/internal/v1/loot/roll-mob", "", env.authMgr, map[string]any{
			"character_id":     charID,
			"fencing_epoch":    epoch,
			"server_id":        "ue5-ded-7777",
			"source_entity_id": "mob.prototype_raider",
			"zone_id":          "zone.p01_west_hub",
			"idempotency_key":  fmt.Sprintf("raider-kill-%d", k),
		})
		if c1 != http.StatusOK {
			t.Fatalf("roll-mob #%d failed (%d): %+v", k, c1, r1)
		}
		if k == 1 {
			// Replay exact same idempotency_key -> must NOT grant duplicate Vàng!
			bal1 := int64(r1["loot"].(map[string]any)["standard_coin"].(float64))
			cDup, rDup := doJSON(t, "POST", env.srv.URL+"/internal/v1/loot/roll-mob", "", env.authMgr, map[string]any{
				"character_id":     charID,
				"fencing_epoch":    epoch,
				"server_id":        "ue5-ded-7777",
				"source_entity_id": "mob.prototype_raider",
				"zone_id":          "zone.p01_west_hub",
				"idempotency_key":  "raider-kill-1",
			})
			balDup := int64(rDup["loot"].(map[string]any)["standard_coin"].(float64))
			if cDup != http.StatusOK || balDup != bal1 {
				t.Fatalf("duplicate roll-mob idempotency failed: bal1=%d vs balDup=%d", bal1, balDup)
			}
		}
	}

	// 5. Enhance equipped Tunic twice (+0 -> +1 -> +2, 100% rate, costs 100 + 150 = 250 Vàng & 2 Crimson Ore)
	for step := 1; step <= 2; step++ {
		cEnh, enhResp := doJSON(t, "POST", env.srv.URL+"/internal/v1/economy/enhance", "", env.authMgr, map[string]any{
			"character_id":     charID,
			"fencing_epoch":    epoch,
			"server_id":        "ue5-ded-7777",
			"item_instance_id": tunicID,
			"idempotency_key":  fmt.Sprintf("enh-tunic-%d", step),
		})
		if cEnh != http.StatusOK || enhResp["enhance_succeeded"] != true {
			t.Fatalf("enhance step %d failed (%d): %+v", step, cEnh, enhResp)
		}
	}
	// At +2 (+10% stat bonus): Tunic +80 HP * 1.10 = +88 HP -> Total MaxHP = 288!
	_, afterEnh := doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/get", "", env.authMgr, map[string]any{
		"character_id":  charID,
		"fencing_epoch": epoch,
		"server_id":     "ue5-ded-7777",
	})
	if math.Abs(afterEnh["max_hp"].(float64)-288.0) > 0.01 {
		t.Fatalf("expected MaxHP=288 after +2 Tunic enhancement, got %v", afterEnh["max_hp"])
	}

	// 6. Damage character to 90 HP via checkpoint save, then use 1x Verdant Elixir (+150 HP/Ki) -> HP becomes 240/288!
	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/character/save", "", env.authMgr, map[string]any{
		"character_id":    charID,
		"fencing_epoch":   epoch,
		"server_id":       "ue5-ded-7777",
		"snapshot_reason": "PERIODIC_60S",
		"pos_x":           -520.0,
		"pos_y":           140.0,
		"pos_z":           112.0,
		"is_alive":        true,
		"hp":              90.0,
		"energy":          50.0,
	})
	cUse, useResp := doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/use", "", env.authMgr, map[string]any{
		"character_id":  charID,
		"fencing_epoch": epoch,
		"server_id":     "ue5-ded-7777",
	})
	if cUse != http.StatusOK || math.Abs(useResp["hp"].(float64)-240.0) > 0.01 || math.Abs(useResp["max_hp"].(float64)-288.0) > 0.01 {
		t.Fatalf("expected Verdant Elixir to heal HP 90 -> 240 / 288, got %d %+v", cUse, useResp)
	}

	// 7. Test Item Lock protection & Equipped Sell protection, then unlock & sell Lv.5 Wolfhide Wraps (+45 Vàng)
	cEqSell, _ := doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/sell", "", env.authMgr, map[string]any{
		"character_id":     charID,
		"fencing_epoch":    epoch,
		"server_id":        "ue5-ded-7777",
		"item_instance_id": tunicID,
	})
	if cEqSell != http.StatusUnprocessableEntity {
		t.Fatalf("expected 422 ERR_CANNOT_SELL_EQUIPPED when selling equipped tunic, got %d", cEqSell)
	}

	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/lock", "", env.authMgr, map[string]any{
		"character_id":     charID,
		"fencing_epoch":    epoch,
		"server_id":        "ue5-ded-7777",
		"item_instance_id": lv5HandsID.String(),
		"locked":           true,
	})
	cLockSell, _ := doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/sell", "", env.authMgr, map[string]any{
		"character_id":     charID,
		"fencing_epoch":    epoch,
		"server_id":        "ue5-ded-7777",
		"item_instance_id": lv5HandsID.String(),
	})
	if cLockSell != http.StatusUnprocessableEntity {
		t.Fatalf("expected 422 ERR_ITEM_LOCKED when selling locked item, got %d", cLockSell)
	}

	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/lock", "", env.authMgr, map[string]any{
		"character_id":     charID,
		"fencing_epoch":    epoch,
		"server_id":        "ue5-ded-7777",
		"item_instance_id": lv5HandsID.String(),
		"locked":           false,
	})
	cSell, sellResp := doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/sell", "", env.authMgr, map[string]any{
		"character_id":     charID,
		"fencing_epoch":    epoch,
		"server_id":        "ue5-ded-7777",
		"item_instance_id": lv5HandsID.String(),
		"idempotency_key":  "sell-wolfhide-1",
	})
	if cSell != http.StatusOK || int64(sellResp["coins_delta"].(float64)) != 45 {
		t.Fatalf("expected +45 Vàng on selling unlocked Wolfhide Wraps, got %d %+v", cSell, sellResp)
	}

	// 8. Verify Double-Entry Immutable Ledger Parity: wallet.standard_coin == SUM(currency_ledger.delta_amount)!
	walletBal, ledgerSum, err := env.ecoSvc.VerifyLedgerParity(context.Background(), charUUID)
	if err != nil || walletBal != ledgerSum || walletBal <= 0 {
		t.Fatalf("LEDGER PARITY VIOLATION: wallet=%d vs ledgerSum=%d (err=%v)", walletBal, ledgerSum, err)
	}

	// 9. Simulated Dedicated Server Restart -> ClaimLease (epoch=2) restores MaxHP=288, Defense=29, & exact Vàng!
	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/lease/release", "", env.authMgr, map[string]any{
		"character_id":  charID,
		"fencing_epoch": epoch,
		"server_id":     "ue5-ded-7777",
	})
	_, tkt2 := doJSON(t, "POST", env.srv.URL+"/v1/world/enter", tok, nil, map[string]string{"character_id": charID})
	c2, claim2 := doJSON(t, "POST", env.srv.URL+"/internal/v1/lease/claim", "", env.authMgr, map[string]string{
		"ticket_id": tkt2["ticket_id"].(string),
		"server_id": "ue5-ded-restarted",
		"zone_id":   "zone.p01_west_hub",
	})
	if c2 != http.StatusOK || int64(claim2["standard_coin"].(float64)) != walletBal || math.Abs(claim2["max_hp"].(float64)-288.0) > 0.01 {
		t.Fatalf("post-restart ClaimLease did not restore Vàng=%d & MaxHP=288: %+v", walletBal, claim2)
	}
}

// Groups 4.5 & 4.7:
// Anti-Dupe 50-Goroutine Race (Concurrent Sell vs Equip vs Enhance on the SAME Item UUID + Stale Epoch Rejection)
func TestStep4AntiDupeConcurrent50GoroutinesAndLedgerParity(t *testing.T) {
	env := setupStep4Env(t)
	tok, charID, epoch := createAndClaimHero(t, env, "AntiDupeHero", "VegitoGuard")
	charUUID := uuid.MustParse(charID)

	// Grant 1000 Vàng + 1xLv.1 Recruit Gloves (sell_price = 30 Vàng) so 50 concurrent goroutines race to Sell/Equip/Enhance it!
	tx, _ := env.pool.Begin(context.Background())
	_, err := economy.ApplyCoinDeltaTx(context.Background(), tx, charUUID, epoch, "ue5-ded-7777", 1000, "ADMIN_SEED", "TEST", "seed1", "seed-1000-coin")
	if err != nil {
		t.Fatalf("seed coin: %v", err)
	}
	contestedItemID, _, err := inventory.AddOrStackItemTx(context.Background(), tx, env.catalog, charUUID, "item.hands_recruit_gloves_01", 1, nil, false, "LOOT_DROP")
	if err != nil {
		t.Fatalf("seed contested gloves: %v", err)
	}
	_ = tx.Commit(context.Background())

	// Fire 50 concurrent goroutines racing to Sell (35 goroutines with DISTINCT idempotency keys!), Equip (10), and Enhance (5) the SAME item!
	// Anti-Dupe Invariant: At most ONE Sell can ever succeed if not equipped, OR if Equipped first then ZERO Sells can succeed!
	var wg sync.WaitGroup
	var sellSuccesses int32
	var mu sync.Mutex

	for g := 0; g < 50; g++ {
		wg.Add(1)
		go func(idx int) {
			defer wg.Done()
			switch {
			case idx < 35:
				c, _ := doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/sell", "", env.authMgr, map[string]any{
					"character_id":     charID,
					"fencing_epoch":    epoch,
					"server_id":        "ue5-ded-7777",
					"item_instance_id": contestedItemID.String(),
					"idempotency_key":  fmt.Sprintf("race-sell-distinct-%d", idx),
				})
				if c == http.StatusOK {
					mu.Lock()
					sellSuccesses++
					mu.Unlock()
				}
			case idx < 45:
				_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/equip", "", env.authMgr, map[string]any{
					"character_id":     charID,
					"fencing_epoch":    epoch,
					"server_id":        "ue5-ded-7777",
					"item_instance_id": contestedItemID.String(),
				})
			default:
				_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/economy/enhance", "", env.authMgr, map[string]any{
					"character_id":     charID,
					"fencing_epoch":    epoch,
					"server_id":        "ue5-ded-7777",
					"item_instance_id": contestedItemID.String(),
					"idempotency_key":  fmt.Sprintf("race-enh-%d", idx),
				})
			}
		}(g)
	}
	wg.Wait()

	if sellSuccesses > 1 {
		t.Fatalf("CRITICAL ITEM DUPLICATION DETECTED: contested item sold %d times concurrently!", sellSuccesses)
	}

	// Verify item cannot exist in BOTH equipped and sold states!
	_, snap := doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/get", "", env.authMgr, map[string]any{
		"character_id":  charID,
		"fencing_epoch": epoch,
		"server_id":     "ue5-ded-7777",
	})
	foundCount := 0
	for _, rawIt := range snap["bag_items"].([]any) {
		if rawIt.(map[string]any)["instance_id"].(string) == contestedItemID.String() {
			foundCount++
		}
	}
	for _, rawIt := range snap["equipped_items"].([]any) {
		if rawIt.(map[string]any)["instance_id"].(string) == contestedItemID.String() {
			foundCount++
		}
	}
	if sellSuccesses == 1 && foundCount != 0 {
		t.Fatalf("CRITICAL GHOST ITEM DUPE: item was sold for Vàng yet still exists (%d copies)!", foundCount)
	}
	if sellSuccesses == 0 && foundCount != 1 {
		t.Fatalf("expected exactly 1 instance of item when not sold, got %d", foundCount)
	}

	// Verify 100% Double-Entry Ledger Parity after 50-goroutine contention!
	walletBal, ledgerSum, err := env.ecoSvc.VerifyLedgerParity(context.Background(), charUUID)
	if err != nil || walletBal != ledgerSum {
		t.Fatalf("ledger mismatch after 50-goroutine race: wallet=%d vs ledger=%d (err=%v)", walletBal, ledgerSum, err)
	}

	// Supersede lease to epoch=2 and prove stale epoch=1 inventory/loot/enhance calls are rejected 409 Conflict!
	_, tkt2 := doJSON(t, "POST", env.srv.URL+"/v1/world/enter", tok, nil, map[string]string{"character_id": charID})
	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/lease/claim", "", env.authMgr, map[string]string{
		"ticket_id": tkt2["ticket_id"].(string),
		"server_id": "ue5-ded-srv2",
		"zone_id":   "zone.p01_west_hub",
	})

	for _, ep := range []string{"/internal/v1/inventory/use", "/internal/v1/loot/roll-mob"} {
		cStale, _ := doJSON(t, "POST", env.srv.URL+ep, "", env.authMgr, map[string]any{
			"character_id":    charID,
			"fencing_epoch":   1,
			"server_id":       "ue5-ded-7777",
			"idempotency_key": "stale-attempt-1",
		})
		if cStale != http.StatusConflict {
			t.Fatalf("expected 409 Conflict for stale epoch=1 on %s, got %d", ep, cStale)
		}
	}
}
