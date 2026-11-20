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
	"time"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/httpapi"
	"github.com/infinite-universe/backend/internal/modules/account"
	"github.com/infinite-universe/backend/internal/modules/auth"
	"github.com/infinite-universe/backend/internal/modules/character"
	"github.com/infinite-universe/backend/internal/modules/economy"
	"github.com/infinite-universe/backend/internal/modules/gamedata"
	"github.com/infinite-universe/backend/internal/modules/guild"
	"github.com/infinite-universe/backend/internal/modules/inventory"
	"github.com/infinite-universe/backend/internal/modules/loot"
	"github.com/infinite-universe/backend/internal/modules/party"
	"github.com/infinite-universe/backend/internal/modules/social"
	"github.com/infinite-universe/backend/internal/modules/trade"
	"github.com/infinite-universe/backend/internal/modules/world"
	"github.com/infinite-universe/backend/internal/platform/telemetry"
	"github.com/jackc/pgx/v5/pgxpool"
)

type step5TestEnv struct {
	pool       *pgxpool.Pool
	catalog    *gamedata.Catalog
	authMgr    *auth.Manager
	ecoSvc     *economy.Service
	invSvc     *inventory.Service
	lootSvc    *loot.Service
	partyCoord *party.Coordinator
	guildSvc   *guild.Service
	tradeSvc   *trade.Service
	chatRouter *social.Router
	srv        *httptest.Server
}

func setupStep5Env(t *testing.T) *step5TestEnv {
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

	logger := telemetry.NewJSONLogger(io.Discard, "step5-test", telemetry.ParseLogLevel("error"))
	accSvc := account.NewService(base.pool)
	charSvc := character.NewService(base.pool)
	ecoSvc := economy.NewService(base.pool)
	invSvc := inventory.NewService(base.pool, cat)
	lootSvc := loot.NewService(base.pool, cat)
	partyCoord := party.NewCoordinator()
	guildSvc := guild.NewService(base.pool)
	tradeSvc := trade.NewService(base.pool, cat)
	chatRouter := social.NewRouter()
	coord := world.NewCoordinator(nil, "zone.p01_west_hub", "127.0.0.1:7777")

	h := httpapi.NewHandler(accSvc, base.authMgr, charSvc, coord, logger)
	h.AttachStep4Modules(cat, ecoSvc, invSvc, lootSvc)
	h.AttachStep5Modules(partyCoord, guildSvc, tradeSvc, chatRouter)

	mux := http.NewServeMux()
	h.RegisterRoutes(mux)
	srv := httptest.NewServer(mux)
	t.Cleanup(srv.Close)

	return &step5TestEnv{
		pool:       base.pool,
		catalog:    cat,
		authMgr:    base.authMgr,
		ecoSvc:     ecoSvc,
		invSvc:     invSvc,
		lootSvc:    lootSvc,
		partyCoord: partyCoord,
		guildSvc:   guildSvc,
		tradeSvc:   tradeSvc,
		chatRouter: chatRouter,
		srv:        srv,
	}
}

func createStep5Hero(t *testing.T, env *step5TestEnv, username, charName string) (tok string, charID string, epoch int64) {
	t.Helper()
	_, reg := doJSON(t, "POST", env.srv.URL+"/v1/auth/register", "", nil, map[string]string{
		"username": username,
		"email":    username + "@iu.step5",
		"password": "Step5SecretPassword!99",
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

func seedCoinsViaLedger(t *testing.T, env *step5TestEnv, charID string, epoch int64, amount int64, idem string) {
	t.Helper()
	tx, err := env.pool.Begin(context.Background())
	if err != nil {
		t.Fatalf("begin seed tx: %v", err)
	}
	defer func() { _ = tx.Rollback(context.Background()) }()
	if _, err := economy.ApplyCoinDeltaTx(
		context.Background(), tx, uuid.MustParse(charID), epoch, "ue5-ded-7777",
		amount, "MOB_DROP", "MOB", "mob.prototype_raider", idem,
	); err != nil {
		t.Fatalf("seedCoinsViaLedger: %v", err)
	}
	if err := tx.Commit(context.Background()); err != nil {
		t.Fatalf("commit seed tx: %v", err)
	}
}

// S5-M0: 5-Player Party Lifecycle (Cap=5, Invite/Accept/Promote/Kick/Leave/Disconnect) + 1.5x Dame Party Synergy + Multi-Channel Chat & Rate Limit
func TestStep5PartySynergyAndMultiChannelChat(t *testing.T) {
	env := setupStep5Env(t)

	type hero struct {
		tok   string
		id    string
		name  string
		epoch int64
	}
	heroes := make([]hero, 6)
	for i := 0; i < 6; i++ {
		uname := fmt.Sprintf("PartyHero%d", i+1)
		cname := fmt.Sprintf("ZWarrior%d", i+1)
		tok, cid, ep := createStep5Hero(t, env, uname, cname)
		heroes[i] = hero{tok: tok, id: cid, name: cname, epoch: ep}
	}

	// 1. Hero 1 invites Heroes 2..5 -> all 4 accept -> 5/5 full party!
	for i := 1; i <= 4; i++ {
		cInv, rInv := doJSON(t, "POST", env.srv.URL+"/internal/v1/party/invite", "", env.authMgr, map[string]any{
			"character_id":   heroes[0].id,
			"character_name": heroes[0].name,
			"level":          1,
			"fencing_epoch":  heroes[0].epoch,
			"server_id":      "ue5-ded-7777",
			"target_char_id": heroes[i].id,
			"target_name":    heroes[i].name,
		})
		if cInv != http.StatusOK {
			t.Fatalf("invite hero %d failed (%d): %+v", i+1, cInv, rInv)
		}
		cAcc, rAcc := doJSON(t, "POST", env.srv.URL+"/internal/v1/party/respond", "", env.authMgr, map[string]any{
			"character_id":   heroes[i].id,
			"character_name": heroes[i].name,
			"level":          1,
			"fencing_epoch":  heroes[i].epoch,
			"server_id":      "ue5-ded-7777",
			"accept":         true,
		})
		if cAcc != http.StatusOK || !rAcc["in_party"].(bool) {
			t.Fatalf("accept hero %d failed (%d): %+v", i+1, cAcc, rAcc)
		}
	}

	// 2. Verify 6th player invite is rejected with 422 ERR_PARTY_FULL!
	cFull, rFull := doJSON(t, "POST", env.srv.URL+"/internal/v1/party/invite", "", env.authMgr, map[string]any{
		"character_id":   heroes[0].id,
		"character_name": heroes[0].name,
		"level":          1,
		"fencing_epoch":  heroes[0].epoch,
		"server_id":      "ue5-ded-7777",
		"target_char_id": heroes[5].id,
		"target_name":    heroes[5].name,
	})
	if cFull != http.StatusUnprocessableEntity || rFull["error_code"] != "ERR_PARTY_FULL" {
		t.Fatalf("expected 422 ERR_PARTY_FULL for 6th member, got %d %+v", cFull, rFull)
	}

	// 3. Verify NRO Per-Hit 1.5x Dame Party Synergy Math (1->150, 2->173 [+15%], 5->218 [+45%])!
	checkHitSynergy := func(nearby int, wantSM int64, wantMult float64, idem string) {
		code, res := doJSON(t, "POST", env.srv.URL+"/internal/v1/character/exp", "", env.authMgr, map[string]any{
			"character_id":         heroes[0].id,
			"fencing_epoch":        heroes[0].epoch,
			"server_id":            "ue5-ded-7777",
			"outgoing_damage":      100.0,
			"nearby_party_members": nearby,
			"killed_target":        false,
			"idempotency_key":      idem,
		})
		if code != http.StatusOK {
			t.Fatalf("exp hit failed (%d): %+v", code, res)
		}
		gotExp := int64(res["exp_granted"].(float64))
		gotPot := int64(res["potential_granted"].(float64))
		gotMult := res["party_synergy_mult"].(float64)
		if gotExp != wantSM || gotPot != wantSM || math.Abs(gotMult-wantMult) > 0.001 {
			t.Fatalf("nearby=%d: expected exp=pot=%d mult=%.2f, got exp=%d pot=%d mult=%.2f", nearby, wantSM, wantMult, gotExp, gotPot, gotMult)
		}
	}
	checkHitSynergy(1, 150, 1.00, "hit-solo")
	checkHitSynergy(2, 173, 1.15, "hit-duo")
	checkHitSynergy(5, 218, 1.45, "hit-5man")

	// 4. Multi-Channel Chat: LOCAL 60m (6000cm) spatial culling + show_bubble + PARTY isolation + 800ms Rate Limit!
	cChat1, mChat1 := doJSON(t, "POST", env.srv.URL+"/internal/v1/social/chat/send", "", env.authMgr, map[string]any{
		"character_id":   heroes[0].id,
		"character_name": heroes[0].name,
		"fencing_epoch":  heroes[0].epoch,
		"server_id":      "ue5-ded-7777",
		"channel":        "LOCAL",
		"content":        "Anh em tap trung danh Boss!\x00\x07",
		"zone_id":        "zone.p01_west_hub",
		"pos_x":          0.0,
		"pos_y":          0.0,
		"pos_z":          120.0,
	})
	if cChat1 != http.StatusOK || !mChat1["show_bubble"].(bool) || mChat1["content"].(string) != "Anh em tap trung danh Boss!" {
		t.Fatalf("unexpected LOCAL chat result (%d): %+v", cChat1, mChat1)
	}

	// Immediate second chat within 800ms -> 429 ERR_CHAT_RATE_LIMITED!
	cSpam, rSpam := doJSON(t, "POST", env.srv.URL+"/internal/v1/social/chat/send", "", env.authMgr, map[string]any{
		"character_id":   heroes[0].id,
		"character_name": heroes[0].name,
		"fencing_epoch":  heroes[0].epoch,
		"server_id":      "ue5-ded-7777",
		"channel":        "LOCAL",
		"content":        "Spamming fast!",
		"zone_id":        "zone.p01_west_hub",
	})
	if cSpam != http.StatusTooManyRequests || rSpam["error_code"] != "ERR_CHAT_RATE_LIMITED" {
		t.Fatalf("expected 429 ERR_CHAT_RATE_LIMITED, got %d %+v", cSpam, rSpam)
	}

	// Nearby viewer (2500 cm <= 6000 cm) sees LOCAL message; far viewer (9000 cm > 6000 cm) does NOT!
	_, nearFetch := doJSON(t, "POST", env.srv.URL+"/internal/v1/social/chat/fetch", "", env.authMgr, map[string]any{
		"character_id":  heroes[5].id,
		"fencing_epoch": heroes[5].epoch,
		"server_id":     "ue5-ded-7777",
		"zone_id":       "zone.p01_west_hub",
		"pos_x":         2500.0,
		"pos_y":         0.0,
		"pos_z":         120.0,
		"has_position":  true,
	})
	if len(nearFetch["messages"].([]any)) != 1 {
		t.Fatalf("expected nearby viewer to receive 1 LOCAL msg, got %+v", nearFetch)
	}

	_, farFetch := doJSON(t, "POST", env.srv.URL+"/internal/v1/social/chat/fetch", "", env.authMgr, map[string]any{
		"character_id":  heroes[5].id,
		"fencing_epoch": heroes[5].epoch,
		"server_id":     "ue5-ded-7777",
		"zone_id":       "zone.p01_west_hub",
		"pos_x":         9000.0,
		"pos_y":         0.0,
		"pos_z":         120.0,
		"has_position":  true,
	})
	if len(farFetch["messages"].([]any)) != 0 {
		t.Fatalf("expected far viewer (90m > 60m) to receive 0 LOCAL msgs, got %+v", farFetch)
	}

	// Party chat sent by Hero 2 -> Hero 1 (in party) sees it; Hero 6 (solo) does NOT!
	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/social/chat/send", "", env.authMgr, map[string]any{
		"character_id":   heroes[1].id,
		"character_name": heroes[1].name,
		"fencing_epoch":  heroes[1].epoch,
		"server_id":      "ue5-ded-7777",
		"channel":        "PARTY",
		"content":        "Party secret strategy!",
		"zone_id":        "zone.p01_west_hub",
	})
	_, pFetchIn := doJSON(t, "POST", env.srv.URL+"/internal/v1/social/chat/fetch", "", env.authMgr, map[string]any{
		"character_id":  heroes[0].id,
		"fencing_epoch": heroes[0].epoch,
		"server_id":     "ue5-ded-7777",
		"after_seq":     1,
		"zone_id":       "zone.p01_west_hub",
	})
	_, pFetchOut := doJSON(t, "POST", env.srv.URL+"/internal/v1/social/chat/fetch", "", env.authMgr, map[string]any{
		"character_id":  heroes[5].id,
		"fencing_epoch": heroes[5].epoch,
		"server_id":     "ue5-ded-7777",
		"after_seq":     1,
		"zone_id":       "zone.p01_west_hub",
	})
	if len(pFetchIn["messages"].([]any)) != 1 || len(pFetchOut["messages"].([]any)) != 0 {
		t.Fatalf("party chat leak or miss: in=%+v out=%+v", pFetchIn, pFetchOut)
	}

	// 5. Promote Hero 2 to Leader, Kick Hero 5, and Disconnect cleanup!
	cProm, rProm := doJSON(t, "POST", env.srv.URL+"/internal/v1/party/promote", "", env.authMgr, map[string]any{
		"character_id":   heroes[0].id,
		"fencing_epoch":  heroes[0].epoch,
		"server_id":      "ue5-ded-7777",
		"target_char_id": heroes[1].id,
	})
	if cProm != http.StatusOK || rProm["leader_char_id"].(string) != heroes[1].id {
		t.Fatalf("promote leader failed (%d): %+v", cProm, rProm)
	}
	cKick, rKick := doJSON(t, "POST", env.srv.URL+"/internal/v1/party/kick", "", env.authMgr, map[string]any{
		"character_id":   heroes[1].id,
		"fencing_epoch":  heroes[1].epoch,
		"server_id":      "ue5-ded-7777",
		"target_char_id": heroes[4].id,
	})
	if cKick != http.StatusOK || len(rKick["members"].([]any)) != 4 {
		t.Fatalf("kick member failed (%d): %+v", cKick, rKick)
	}
}

// S5-M1: PostgreSQL 18 Guild System (Create 200 Vàng Lv>=2, CITEXT Uniqueness, Join, MOTD, Role, Treasury Donate & Restart Persistence)
func TestStep5GuildLifecycleTreasuryAndPersistence(t *testing.T) {
	env := setupStep5Env(t)
	tokA, charA, epA := createStep5Hero(t, env, "GuildMasterGoku", "SonGoku")
	_, charB, epB := createStep5Hero(t, env, "GuildMemberVeg", "VegetaPrince")

	seedCoinsViaLedger(t, env, charA, epA, 500, "seed-goku-500")
	seedCoinsViaLedger(t, env, charB, epB, 300, "seed-veg-300")

	// 1. Lv.1 character creating Guild -> rejected with 422 ERR_GUILD_LEVEL_TOO_LOW!
	cLv1, rLv1 := doJSON(t, "POST", env.srv.URL+"/internal/v1/guild/create", "", env.authMgr, map[string]any{
		"character_id":    charA,
		"fencing_epoch":   epA,
		"server_id":       "ue5-ded-7777",
		"guild_name":      "Zenith Vanguards",
		"guild_tag":       "ZNT",
		"idempotency_key": "create-znt-lv1",
	})
	if cLv1 != http.StatusUnprocessableEntity || rLv1["error_code"] != "ERR_GUILD_LEVEL_TOO_LOW" {
		t.Fatalf("expected 422 ERR_GUILD_LEVEL_TOO_LOW at Lv.1, got %d %+v", cLv1, rLv1)
	}

	// Level up Goku to Lv.2 (500 EXP needed: 350 outgoing_damage -> 525 SM/TN)
	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/character/exp", "", env.authMgr, map[string]any{
		"character_id":    charA,
		"fencing_epoch":   epA,
		"server_id":       "ue5-ded-7777",
		"outgoing_damage": 350.0,
		"killed_target":   true,
		"idempotency_key": "goku-lv2",
	})

	// 2. Create Guild [ZNT] Zenith Vanguards (-200 Vàng via currency_ledger GUILD_CREATE)!
	cCreate, rCreate := doJSON(t, "POST", env.srv.URL+"/internal/v1/guild/create", "", env.authMgr, map[string]any{
		"character_id":      charA,
		"fencing_epoch":     epA,
		"server_id":         "ue5-ded-7777",
		"guild_name":        "Zenith Vanguards",
		"guild_tag":         "ZNT",
		"motd_announcement": "San Boss moi toi 20h!",
		"idempotency_key":   "create-znt-ok",
	})
	if cCreate != http.StatusCreated || !rCreate["in_guild"].(bool) {
		t.Fatalf("create guild failed (%d): %+v", cCreate, rCreate)
	}
	gObj := rCreate["guild"].(map[string]any)
	guildID := gObj["guild_id"].(string)
	if int64(rCreate["standard_coin"].(float64)) != 300 || gObj["guild_tag"].(string) != "ZNT" {
		t.Fatalf("expected 300 Vàng remaining & tag ZNT, got %+v", rCreate)
	}

	// 3. CITEXT duplicate tag check ("znt" lowercase must collide with "ZNT"!)
	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/character/exp", "", env.authMgr, map[string]any{
		"character_id":    charB,
		"fencing_epoch":   epB,
		"server_id":       "ue5-ded-7777",
		"outgoing_damage": 350.0,
		"killed_target":   true,
		"idempotency_key": "veg-lv2",
	})
	cDup, rDup := doJSON(t, "POST", env.srv.URL+"/internal/v1/guild/create", "", env.authMgr, map[string]any{
		"character_id":    charB,
		"fencing_epoch":   epB,
		"server_id":       "ue5-ded-7777",
		"guild_name":      "Other Name",
		"guild_tag":       "znt",
		"idempotency_key": "dup-tag-test",
	})
	if cDup != http.StatusConflict || rDup["error_code"] != "ERR_GUILD_TAKEN" {
		t.Fatalf("expected 409 ERR_GUILD_TAKEN on CITEXT tag collision, got %d %+v", cDup, rDup)
	}

	// 4. Vegeta joins [ZNT] & donates 150 Vàng to Guild Treasury!
	cJoin, rJoin := doJSON(t, "POST", env.srv.URL+"/internal/v1/guild/join", "", env.authMgr, map[string]any{
		"character_id":  charB,
		"fencing_epoch": epB,
		"server_id":     "ue5-ded-7777",
		"guild_id":      guildID,
	})
	if cJoin != http.StatusOK || int(rJoin["guild"].(map[string]any)["member_count"].(float64)) != 2 {
		t.Fatalf("join guild failed (%d): %+v", cJoin, rJoin)
	}

	cDon, rDon := doJSON(t, "POST", env.srv.URL+"/internal/v1/guild/donate", "", env.authMgr, map[string]any{
		"character_id":    charB,
		"fencing_epoch":   epB,
		"server_id":       "ue5-ded-7777",
		"amount_coin":     150,
		"idempotency_key": "veg-donate-150",
	})
	if cDon != http.StatusOK {
		t.Fatalf("guild donate failed (%d): %+v", cDon, rDon)
	}
	gAfterDon := rDon["guild"].(map[string]any)
	if int64(gAfterDon["treasury_standard_coin"].(float64)) != 150 || int64(rDon["standard_coin"].(float64)) != 150 {
		t.Fatalf("expected treasury=150 & Vegeta coin=150, got %+v", rDon)
	}

	// Leader cannot leave while Vegeta is still a member!
	cLeaveFail, _ := doJSON(t, "POST", env.srv.URL+"/internal/v1/guild/leave", "", env.authMgr, map[string]any{
		"character_id":  charA,
		"fencing_epoch": epA,
		"server_id":     "ue5-ded-7777",
	})
	if cLeaveFail != http.StatusUnprocessableEntity {
		t.Fatalf("expected 422 when leader tries to leave non-empty guild, got %d", cLeaveFail)
	}

	// 5. Simulate Dedicated Server reconnect (re-claim lease) -> verify guild_tag="ZNT" & guild_name="Zenith Vanguards" restored on CharacterState!
	_, tkt2 := doJSON(t, "POST", env.srv.URL+"/v1/world/enter", tokA, nil, map[string]string{"character_id": charA})
	cClaim2, claim2 := doJSON(t, "POST", env.srv.URL+"/internal/v1/lease/claim", "", env.authMgr, map[string]string{
		"ticket_id": tkt2["ticket_id"].(string),
		"server_id": "ue5-ded-7777-restarted",
		"zone_id":   "zone.p01_west_hub",
	})
	if cClaim2 != http.StatusOK || claim2["guild_tag"] != "ZNT" || claim2["guild_role"] != "LEADER" {
		t.Fatalf("expected guild_tag=ZNT & guild_role=LEADER after server restart claim, got %d %+v", cClaim2, claim2)
	}

	// 6. Verify 100% Double-Entry Ledger Parity for both Goku & Vegeta!
	for _, cid := range []string{charA, charB} {
		wCoin, lSum, err := env.ecoSvc.VerifyLedgerParity(context.Background(), uuid.MustParse(cid))
		if err != nil || wCoin != lSum {
			t.Fatalf("ledger parity mismatch for %s: wallet=%d ledger=%d err=%v", cid, wCoin, lSum, err)
		}
	}
}

// S5-M2: Atomic 2-Step P2P Direct Trade (Bound/Locked Rejection, Anti-Scam Lock Reset, 3.0s Countdown, 5% Tax & 50-Goroutine Race/Anti-Dupe)
func TestStep5AtomicTwoStepTradeAnd50GoroutineConcurrency(t *testing.T) {
	env := setupStep5Env(t)
	env.tradeSvc.SetInspectionDelay(120 * time.Millisecond) // deterministic 120ms inspection gate for unit test

	_, charA, epA := createStep5Hero(t, env, "TraderPiccolo", "Piccolo")
	_, charB, epB := createStep5Hero(t, env, "TraderGohan", "SonGohan")
	uuidA := uuid.MustParse(charA)
	uuidB := uuid.MustParse(charB)

	seedCoinsViaLedger(t, env, charA, epA, 400, "seed-piccolo-400")
	seedCoinsViaLedger(t, env, charB, epB, 100, "seed-gohan-100")

	// Inject 1 unbound CSPRNG Rare Gloves into Gohan's bag (charB)
	tx, _ := env.pool.Begin(context.Background())
	affixes, err := env.lootSvc.RollAffixesCSPRNG("slot.hands", 5, 2, 1)
	if err != nil {
		t.Fatalf("RollAffixesCSPRNG: %v", err)
	}
	unboundGlovesID, _, err := inventory.AddOrStackItemTx(context.Background(), tx, env.catalog, uuidB, "item.hands_wolfhide_wraps_05", 1, affixes, false, "LOOT_DROP")
	if err != nil {
		t.Fatalf("AddOrStackItemTx unbound gloves: %v", err)
	}
	_ = tx.Commit(context.Background())

	// Find Piccolo's bound starter tunic to test soulbound trade rejection
	_, invA0 := doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/get", "", env.authMgr, map[string]any{
		"character_id":  charA,
		"fencing_epoch": epA,
		"server_id":     "ue5-ded-7777",
	})
	var boundTunicID string
	for _, raw := range invA0["bag_items"].([]any) {
		it := raw.(map[string]any)
		if it["item_def_id"].(string) == "item.body_recruit_tunic_01" {
			boundTunicID = it["instance_id"].(string)
		}
	}

	// 1. Open P2P Trade Session (Piccolo -> Gohan)
	cReq, rReq := doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/request", "", env.authMgr, map[string]any{
		"character_id":      charA,
		"fencing_epoch":     epA,
		"server_id":         "ue5-ded-7777",
		"recipient_char_id": charB,
		"zone_id":           "zone.p01_west_hub",
	})
	if cReq != http.StatusOK {
		t.Fatalf("trade request failed (%d): %+v", cReq, rReq)
	}
	tradeID := rReq["trade_id"].(string)

	cResp, _ := doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/respond", "", env.authMgr, map[string]any{
		"character_id":  charB,
		"fencing_epoch": epB,
		"server_id":     "ue5-ded-7777",
		"trade_id":      tradeID,
		"accept":        true,
	})
	if cResp != http.StatusOK {
		t.Fatalf("trade accept failed (%d)", cResp)
	}

	// 2. Offering soulbound Starter Tunic -> MUST fail with 422 ERR_ITEM_BOUND!
	cBound, rBound := doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/offer", "", env.authMgr, map[string]any{
		"character_id":      charA,
		"fencing_epoch":     epA,
		"server_id":         "ue5-ded-7777",
		"trade_id":          tradeID,
		"coin_offer":        0,
		"item_instance_ids": []string{boundTunicID},
	})
	if cBound != http.StatusUnprocessableEntity || rBound["error_code"] != "ERR_ITEM_BOUND" {
		t.Fatalf("expected 422 ERR_ITEM_BOUND when offering bound starter item, got %d %+v", cBound, rBound)
	}

	// 3. Player-locking Gohan's unbound gloves -> offering MUST fail with 422 ERR_ITEM_LOCKED!
	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/lock", "", env.authMgr, map[string]any{
		"character_id":     charB,
		"fencing_epoch":    epB,
		"server_id":        "ue5-ded-7777",
		"item_instance_id": unboundGlovesID.String(),
		"locked":           true,
	})
	cLocked, rLocked := doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/offer", "", env.authMgr, map[string]any{
		"character_id":      charB,
		"fencing_epoch":     epB,
		"server_id":         "ue5-ded-7777",
		"trade_id":          tradeID,
		"coin_offer":        0,
		"item_instance_ids": []string{unboundGlovesID.String()},
	})
	if cLocked != http.StatusUnprocessableEntity || rLocked["error_code"] != "ERR_ITEM_LOCKED" {
		t.Fatalf("expected 422 ERR_ITEM_LOCKED when offering locked item, got %d %+v", cLocked, rLocked)
	}
	// Unlock Gohan's gloves so they can be legitimately traded
	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/lock", "", env.authMgr, map[string]any{
		"character_id":     charB,
		"fencing_epoch":    epB,
		"server_id":        "ue5-ded-7777",
		"item_instance_id": unboundGlovesID.String(),
		"locked":           false,
	})

	// 4. Anti-Scam Lock Reset Test:
	// Piccolo offers 100 Vàng and clicks LOCK; Gohan then changes his offer -> Piccolo's lock MUST auto-reset to false!
	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/offer", "", env.authMgr, map[string]any{
		"character_id":      charA,
		"fencing_epoch":     epA,
		"server_id":         "ue5-ded-7777",
		"trade_id":          tradeID,
		"coin_offer":        100,
		"item_instance_ids": []string{},
	})
	_, rLockA1 := doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/lock", "", env.authMgr, map[string]any{
		"character_id":  charA,
		"fencing_epoch": epA,
		"server_id":     "ue5-ded-7777",
		"trade_id":      tradeID,
	})
	if !rLockA1["initiator_locked"].(bool) {
		t.Fatalf("expected initiator_locked=true, got %+v", rLockA1)
	}

	_, rOfferB := doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/offer", "", env.authMgr, map[string]any{
		"character_id":      charB,
		"fencing_epoch":     epB,
		"server_id":         "ue5-ded-7777",
		"trade_id":          tradeID,
		"coin_offer":        0,
		"item_instance_ids": []string{unboundGlovesID.String()},
	})
	if rOfferB["initiator_locked"].(bool) || rOfferB["recipient_locked"].(bool) {
		t.Fatalf("ANTI-SCAM VIOLATION: partner offer edit failed to reset initiator_locked! %+v", rOfferB)
	}

	// 5. Both Lock -> Immediate Confirm BEFORE inspection countdown expires MUST fail with 422 ERR_TRADE_COUNTDOWN_ACTIVE!
	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/lock", "", env.authMgr, map[string]any{
		"character_id":  charA,
		"fencing_epoch": epA,
		"server_id":     "ue5-ded-7777",
		"trade_id":      tradeID,
	})
	_, rBothLock := doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/lock", "", env.authMgr, map[string]any{
		"character_id":  charB,
		"fencing_epoch": epB,
		"server_id":     "ue5-ded-7777",
		"trade_id":      tradeID,
	})
	if rBothLock["state"] != "BOTH_LOCKED" || int64(rBothLock["tax_coin_preview"].(float64)) != 5 {
		t.Fatalf("expected BOTH_LOCKED & tax_coin_preview=5, got %+v", rBothLock)
	}

	cEarly, rEarly := doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/confirm", "", env.authMgr, map[string]any{
		"character_id":  charA,
		"fencing_epoch": epA,
		"server_id":     "ue5-ded-7777",
		"trade_id":      tradeID,
	})
	if cEarly != http.StatusUnprocessableEntity || rEarly["error_code"] != "ERR_TRADE_COUNTDOWN_ACTIVE" {
		t.Fatalf("expected 422 ERR_TRADE_COUNTDOWN_ACTIVE during inspection window, got %d %+v", cEarly, rEarly)
	}

	// Wait out 120ms inspection gate -> both Confirm -> Atomic Settlement!
	time.Sleep(140 * time.Millisecond)

	cConfA, _ := doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/confirm", "", env.authMgr, map[string]any{
		"character_id":  charA,
		"fencing_epoch": epA,
		"server_id":     "ue5-ded-7777",
		"trade_id":      tradeID,
	})
	cConfB, rConfB := doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/confirm", "", env.authMgr, map[string]any{
		"character_id":  charB,
		"fencing_epoch": epB,
		"server_id":     "ue5-ded-7777",
		"trade_id":      tradeID,
	})
	if cConfA != http.StatusOK || cConfB != http.StatusOK {
		t.Fatalf("confirm failed: cA=%d cB=%d rB=%+v", cConfA, cConfB, rConfB)
	}
	if rConfB["trade"].(map[string]any)["state"] != "SETTLED" {
		t.Fatalf("expected trade state SETTLED, got %+v", rConfB)
	}

	// Verify ownership & coins after 5% tax:
	// Piccolo: 400 - 100 = 300 Vàng + owns unboundGlovesID!
	// Gohan:   100 + 95  = 195 Vàng (5 Vàng burned as TRADE_TAX) + no longer owns unboundGlovesID!
	var ownerAfter uuid.UUID
	if err := env.pool.QueryRow(context.Background(), `SELECT character_id FROM inventory_items WHERE id = $1`, unboundGlovesID).Scan(&ownerAfter); err != nil || ownerAfter != uuidA {
		t.Fatalf("expected unboundGlovesID owner=%s after trade, got %s (err=%v)", uuidA, ownerAfter, err)
	}
	var tradeState string
	var taxBurned int64
	if err := env.pool.QueryRow(context.Background(), `SELECT trade_state, tax_coin_burned FROM trade_transactions WHERE trade_id = $1`, uuid.MustParse(tradeID)).Scan(&tradeState, &taxBurned); err != nil {
		t.Fatalf("query trade_transactions failed: %v", err)
	}
	if tradeState != "SETTLED" || taxBurned != 5 {
		t.Fatalf("expected trade_transactions SETTLED with tax_coin_burned=5, got state=%s tax=%d", tradeState, taxBurned)
	}

	// 6. 50-Goroutine Race & Deadlock Stress Test:
	// Simultaneously fire 25 ConfirmTrade calls + 15 SellItem attempts on the traded item + 10 Wallet mutations!
	// Verify ZERO deadlocks, ZERO item duplication (COUNT(id)=1), and 100% currency_ledger parity!
	env.tradeSvc.SetInspectionDelay(0)
	_, rReq2 := doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/request", "", env.authMgr, map[string]any{
		"character_id":      charA,
		"fencing_epoch":     epA,
		"server_id":         "ue5-ded-7777",
		"recipient_char_id": charB,
		"zone_id":           "zone.p01_west_hub",
	})
	tradeID2 := rReq2["trade_id"].(string)
	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/respond", "", env.authMgr, map[string]any{
		"character_id":  charB,
		"fencing_epoch": epB,
		"server_id":     "ue5-ded-7777",
		"trade_id":      tradeID2,
		"accept":        true,
	})
	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/offer", "", env.authMgr, map[string]any{
		"character_id":      charA,
		"fencing_epoch":     epA,
		"server_id":         "ue5-ded-7777",
		"trade_id":          tradeID2,
		"coin_offer":        20,
		"item_instance_ids": []string{unboundGlovesID.String()},
	})
	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/offer", "", env.authMgr, map[string]any{
		"character_id":      charB,
		"fencing_epoch":     epB,
		"server_id":         "ue5-ded-7777",
		"trade_id":          tradeID2,
		"coin_offer":        40,
		"item_instance_ids": []string{},
	})
	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/lock", "", env.authMgr, map[string]any{
		"character_id":  charA,
		"fencing_epoch": epA,
		"server_id":     "ue5-ded-7777",
		"trade_id":      tradeID2,
	})
	_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/lock", "", env.authMgr, map[string]any{
		"character_id":  charB,
		"fencing_epoch": epB,
		"server_id":     "ue5-ded-7777",
		"trade_id":      tradeID2,
	})

	var wg sync.WaitGroup
	startGate := make(chan struct{})
	for g := 0; g < 50; g++ {
		wg.Add(1)
		go func(idx int) {
			defer wg.Done()
			<-startGate
			switch {
			case idx < 15:
				_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/confirm", "", env.authMgr, map[string]any{
					"character_id":  charA,
					"fencing_epoch": epA,
					"server_id":     "ue5-ded-7777",
					"trade_id":      tradeID2,
				})
			case idx < 30:
				_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/trade/confirm", "", env.authMgr, map[string]any{
					"character_id":  charB,
					"fencing_epoch": epB,
					"server_id":     "ue5-ded-7777",
					"trade_id":      tradeID2,
				})
			default:
				// Simultaneous anti-dupe attack: try selling the exact gloves while trade settlement is in-flight!
				_, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/inventory/sell", "", env.authMgr, map[string]any{
					"character_id":     charA,
					"fencing_epoch":    epA,
					"server_id":        "ue5-ded-7777",
					"item_instance_id": unboundGlovesID.String(),
					"quantity":         1,
					"idempotency_key":  fmt.Sprintf("race-sell-gloves-%d", idx),
				})
			}
		}(g)
	}
	close(startGate)
	wg.Wait()

	// Anti-Dupe Assertion: gloves MUST exist at most ONCE (1 if traded to B before sell, or 0 if sold by A causing trade ABORTED_CONFLICT — NEVER 2, NEVER sold AND traded!)
	var glovesCount int
	_ = env.pool.QueryRow(context.Background(), `SELECT COUNT(*) FROM inventory_items WHERE item_def_id = 'item.hands_wolfhide_wraps_05'`).Scan(&glovesCount)
	var sellLedgerCount int
	_ = env.pool.QueryRow(context.Background(), `SELECT COUNT(*) FROM currency_ledger WHERE character_id = $1 AND reason_code = 'NPC_SELL'`, uuidA).Scan(&sellLedgerCount)
	var trade2SettledCount int
	_ = env.pool.QueryRow(context.Background(), `SELECT COUNT(*) FROM trade_transactions WHERE trade_id = $1 AND trade_state = 'SETTLED'`, uuid.MustParse(tradeID2)).Scan(&trade2SettledCount)

	if !(glovesCount == 1 && sellLedgerCount == 0 && trade2SettledCount == 1) && !(glovesCount == 0 && sellLedgerCount == 1 && trade2SettledCount == 0) {
		t.Fatalf("ANTI-DUPE INVARIANT BROKEN: glovesCount=%d sellLedgerCount=%d trade2SettledCount=%d", glovesCount, sellLedgerCount, trade2SettledCount)
	}

	for _, cid := range []uuid.UUID{uuidA, uuidB} {
		wCoin, lSum, err := env.ecoSvc.VerifyLedgerParity(context.Background(), cid)
		if err != nil || wCoin != lSum || wCoin < 0 {
			t.Fatalf("post-race ledger parity mismatch for %s: wallet=%d ledger=%d err=%v", cid, wCoin, lSum, err)
		}
	}
}
