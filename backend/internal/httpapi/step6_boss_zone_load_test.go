package httpapi_test

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"sync"
	"testing"
	"time"

	"github.com/infinite-universe/backend/internal/httpapi"
	"github.com/infinite-universe/backend/internal/modules/account"
	"github.com/infinite-universe/backend/internal/modules/auth"
	"github.com/infinite-universe/backend/internal/modules/boss"
	"github.com/infinite-universe/backend/internal/modules/character"
	"github.com/infinite-universe/backend/internal/modules/economy"
	"github.com/infinite-universe/backend/internal/modules/gamedata"
	"github.com/infinite-universe/backend/internal/modules/guild"
	"github.com/infinite-universe/backend/internal/modules/inventory"
	"github.com/infinite-universe/backend/internal/modules/loot"
	"github.com/infinite-universe/backend/internal/modules/party"
	"github.com/infinite-universe/backend/internal/modules/quest"
	"github.com/infinite-universe/backend/internal/modules/social"
	"github.com/infinite-universe/backend/internal/modules/trade"
	"github.com/infinite-universe/backend/internal/modules/world"
	"github.com/infinite-universe/backend/internal/platform/telemetry"
	"github.com/jackc/pgx/v5/pgxpool"
)

type step6TestEnv struct {
	pool     *pgxpool.Pool
	catalog  *gamedata.Catalog
	authMgr  *auth.Manager
	bossSvc  *boss.Service
	questSvc *quest.Service
	srv      *httptest.Server
}

func setupStep6Env(t *testing.T) *step6TestEnv {
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

	logger := telemetry.NewJSONLogger(io.Discard, "step6-test", telemetry.ParseLogLevel("error"))
	accSvc := account.NewService(base.pool)
	charSvc := character.NewService(base.pool)
	ecoSvc := economy.NewService(base.pool)
	invSvc := inventory.NewService(base.pool, cat)
	lootSvc := loot.NewService(base.pool, cat)
	partyCoord := party.NewCoordinator()
	guildSvc := guild.NewService(base.pool)
	tradeSvc := trade.NewService(base.pool, cat)
	chatRouter := social.NewRouter()
	bossSvc := boss.NewService(base.pool, cat, lootSvc)
	questSvc := quest.NewService(base.pool)
	coord := world.NewCoordinator(nil, "zone.p01_west_hub", "127.0.0.1:7777")

	h := httpapi.NewHandler(accSvc, base.authMgr, charSvc, coord, logger)
	h.AttachStep4Modules(cat, ecoSvc, invSvc, lootSvc)
	h.AttachStep5Modules(partyCoord, guildSvc, tradeSvc, chatRouter)
	h.AttachStep6Modules(bossSvc, questSvc)

	mux := http.NewServeMux()
	h.RegisterRoutes(mux)
	srv := httptest.NewServer(mux)
	t.Cleanup(srv.Close)

	return &step6TestEnv{
		pool:     base.pool,
		catalog:  cat,
		authMgr:  base.authMgr,
		bossSvc:  bossSvc,
		questSvc: questSvc,
		srv:      srv,
	}
}

func s6PostJSON(t *testing.T, url, bearer string, signer *auth.Manager, payload any, out any) (int, []byte) {
	t.Helper()
	raw, err := json.Marshal(payload)
	if err != nil {
		t.Fatalf("marshal json: %v", err)
	}
	req, err := http.NewRequest(http.MethodPost, url, bytes.NewReader(raw))
	if err != nil {
		t.Fatalf("new request: %v", err)
	}
	req.Header.Set("Content-Type", "application/json")
	if bearer != "" {
		req.Header.Set("Authorization", "Bearer "+bearer)
	}
	if signer != nil {
		req.Header.Set("X-IU-Server-HMAC", signer.ComputeInternalHMAC(raw))
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("http post %s: %v", url, err)
	}
	defer resp.Body.Close()
	respBytes, _ := io.ReadAll(resp.Body)
	if out != nil && resp.StatusCode >= 200 && resp.StatusCode < 300 && len(respBytes) > 0 {
		if err := json.Unmarshal(respBytes, out); err != nil {
			t.Fatalf("unmarshal %s: %v (raw=%s)", url, err, string(respBytes))
		}
	}
	return resp.StatusCode, respBytes
}

func claimStep6Hero(t *testing.T, env *step6TestEnv, uName, cName, serverID string) (string, string, int64) {
	t.Helper()
	var reg struct {
		AccessToken string `json:"access_token"`
	}
	if st, raw := s6PostJSON(t, env.srv.URL+"/v1/auth/register", "", nil, map[string]any{
		"username": uName,
		"email":    uName + "@iu.s6",
		"password": "Step6Password!2026",
	}, &reg); st != http.StatusCreated {
		t.Fatalf("register %s failed (%d): %s", uName, st, string(raw))
	}

	var ch struct {
		CharacterID string `json:"character_id"`
	}
	if st, raw := s6PostJSON(t, env.srv.URL+"/v1/characters", reg.AccessToken, nil, map[string]any{
		"character_name": cName,
		"race_id":        "race.zenith_warrior",
	}, &ch); st != http.StatusCreated {
		t.Fatalf("create char %s failed (%d): %s", cName, st, string(raw))
	}

	var ent struct {
		TicketID     string `json:"ticket_id"`
		TargetZoneID string `json:"target_zone_id"`
	}
	if st, raw := s6PostJSON(t, env.srv.URL+"/v1/world/enter", reg.AccessToken, nil, map[string]any{
		"character_id": ch.CharacterID,
	}, &ent); st != http.StatusOK {
		t.Fatalf("enter world %s failed (%d): %s", cName, st, string(raw))
	}

	var cl struct {
		FencingEpoch int64 `json:"fencing_epoch"`
	}
	if st, raw := s6PostJSON(t, env.srv.URL+"/internal/v1/lease/claim", "", env.authMgr, map[string]any{
		"ticket_id": ent.TicketID,
		"server_id": serverID,
		"zone_id":   ent.TargetZoneID,
	}, &cl); st != http.StatusOK {
		t.Fatalf("claim lease %s failed (%d): %s", cName, st, string(raw))
	}
	return reg.AccessToken, ch.CharacterID, cl.FencingEpoch
}

func TestStep6WorldBossContributionHardPityAndPityShop(t *testing.T) {
	env := setupStep6Env(t)
	tag := fmt.Sprintf("%d", time.Now().UnixNano()%100000)

	_, charA, epA := claimStep6Hero(t, env, "S6BossA_"+tag, "GokuB"+tag, "srv-highland")
	_, charB, epB := claimStep6Hero(t, env, "S6BossB_"+tag, "VegeB"+tag, "srv-highland")
	_, charLeech, epLeech := claimStep6Hero(t, env, "S6Leech_"+tag, "Leech"+tag, "srv-highland")

	// 1. Settle Boss Kill: HeroA (68%), HeroB (31%), Leech (1% < 2% min contribution -> ineligible!)
	var settleRes boss.SettleBossKillResult
	st, raw := s6PostJSON(t, env.srv.URL+"/internal/v1/boss/settle-kill", "", env.authMgr, map[string]any{
		"boss_id":           "boss.highland_titan_01",
		"zone_id":           "zone.p01_highland_01",
		"channel_id":        2,
		"server_id":         "srv-highland",
		"fight_duration_ms": 35200,
		"idempotency_key":   "s6_boss_1_" + tag,
		"participants": []map[string]any{
			{"character_id": charA, "character_name": "GokuB" + tag, "fencing_epoch": epA, "damage_dealt": 68000},
			{"character_id": charB, "character_name": "VegeB" + tag, "fencing_epoch": epB, "damage_dealt": 31000},
			{"character_id": charLeech, "character_name": "Leech" + tag, "fencing_epoch": epLeech, "damage_dealt": 1000},
		},
	}, &settleRes)
	if st != http.StatusOK {
		t.Fatalf("boss settle failed (%d): %s", st, string(raw))
	}
	if len(settleRes.Rewards) != 3 {
		t.Fatalf("expected 3 participant outcomes, got %d", len(settleRes.Rewards))
	}
	if !settleRes.Rewards[0].Eligible || !settleRes.Rewards[1].Eligible || settleRes.Rewards[2].Eligible {
		t.Fatalf("unexpected eligibility: %+v", settleRes.Rewards)
	}
	if settleRes.Rewards[2].CoinsGranted != 0 || settleRes.Rewards[2].TokensGranted != 0 {
		t.Fatalf("<2%% leech must receive 0 coins & 0 tokens: %+v", settleRes.Rewards[2])
	}
	if settleRes.Rewards[0].CoinsGranted < 850 || settleRes.Rewards[0].TokensGranted < 1 {
		t.Fatalf("top contributor expected >=850 coins & >=1 token: %+v", settleRes.Rewards[0])
	}

	// Idempotent replay of identical boss kill key MUST NOT duplicate coins/tokens!
	var replayRes boss.SettleBossKillResult
	st, _ = s6PostJSON(t, env.srv.URL+"/internal/v1/boss/settle-kill", "", env.authMgr, map[string]any{
		"boss_id":           "boss.highland_titan_01",
		"zone_id":           "zone.p01_highland_01",
		"channel_id":        2,
		"server_id":         "srv-highland",
		"fight_duration_ms": 35200,
		"idempotency_key":   "s6_boss_1_" + tag,
		"participants": []map[string]any{
			{"character_id": charA, "character_name": "GokuB" + tag, "fencing_epoch": epA, "damage_dealt": 68000},
		},
	}, &replayRes)
	if st != http.StatusOK || !replayRes.Idempotent {
		t.Fatalf("expected idempotent boss kill replay, got status=%d idem=%v", st, replayRes.Idempotent)
	}

	// 2. Verify 5th-Kill Hard Pity Guarantee when consecutive_dry_kills == 4!
	_, err := env.pool.Exec(context.Background(), `
		UPDATE boss_pity_tokens SET consecutive_dry_kills = 4, amount = 30
		 WHERE character_id = $1 AND token_id = 'token.boss_highland_titan'
	`, charA)
	if err != nil {
		t.Fatalf("seed consecutive_dry_kills=4: %v", err)
	}
	var pityKill boss.SettleBossKillResult
	st, raw = s6PostJSON(t, env.srv.URL+"/internal/v1/boss/settle-kill", "", env.authMgr, map[string]any{
		"boss_id":           "boss.highland_titan_01",
		"zone_id":           "zone.p01_highland_01",
		"channel_id":        2,
		"server_id":         "srv-highland",
		"fight_duration_ms": 22000,
		"idempotency_key":   "s6_boss_hardpity_" + tag,
		"participants": []map[string]any{
			{"character_id": charA, "character_name": "GokuB" + tag, "fencing_epoch": epA, "damage_dealt": 100000},
		},
	}, &pityKill)
	if st != http.StatusOK {
		t.Fatalf("hard pity boss kill failed (%d): %s", st, string(raw))
	}
	if pityKill.Rewards[0].ConsecutiveDryKills != 0 {
		t.Fatalf("expected consecutive_dry_kills reset to 0 on 5th kill, got %d", pityKill.Rewards[0].ConsecutiveDryKills)
	}

	// 3. Claim Titan Slayer Quest (+1000 Vang, +3 Titan Tokens) & Redeem Vanguard Visor (6 Tokens + 1200 Vang)
	st, raw = s6PostJSON(t, env.srv.URL+"/internal/v1/quest/claim", "", env.authMgr, map[string]any{
		"character_id":    charA,
		"fencing_epoch":   epA,
		"server_id":       "srv-highland",
		"quest_id":        "quest.nro_titan_slayer_01",
		"idempotency_key": "s6_q_titan_" + tag,
	}, nil)
	if st != http.StatusOK {
		t.Fatalf("claim titan quest failed (%d): %s", st, string(raw))
	}

	var redeemOut struct {
		PityShop boss.PityShopSnapshot `json:"pity_shop"`
	}
	st, raw = s6PostJSON(t, env.srv.URL+"/internal/v1/boss/pity/redeem", "", env.authMgr, map[string]any{
		"character_id":    charA,
		"fencing_epoch":   epA,
		"server_id":       "srv-highland",
		"entry_id":        "pity.head_vanguard_15",
		"idempotency_key": "s6_redeem_visor_" + tag,
	}, &redeemOut)
	if st != http.StatusOK {
		t.Fatalf("redeem pity.head_vanguard_15 failed (%d): %s", st, string(raw))
	}

	// Verify global zero ledger drift!
	var drift int
	if err := env.pool.QueryRow(context.Background(), `
		SELECT COUNT(*) FROM character_wallets w
		 WHERE w.standard_coin <> COALESCE((SELECT SUM(l.delta_amount) FROM currency_ledger l WHERE l.character_id = w.character_id), 0)
	`).Scan(&drift); err != nil || drift != 0 {
		t.Fatalf("expected 0 currency_ledger drift, got drift=%d err=%v", drift, err)
	}
}

func TestStep6MultiZoneTransferAnd20CCUConcurrentChaos(t *testing.T) {
	env := setupStep6Env(t)
	tag := fmt.Sprintf("%d", time.Now().UnixNano()%100000)

	// Register 2 live Dedicated Servers (:7777 Ch.1 & :7778 Ch.2) + 1 Full Channel (:7779 Ch.3 @ 25/25 HardCap)
	s6PostJSON(t, env.srv.URL+"/internal/v1/world/heartbeat", "", env.authMgr, map[string]any{
		"server_id": "ded-1", "zone_id": "zone.p01_west_hub", "channel_id": 1,
		"server_addr": "127.0.0.1:7777", "online_ccu": 4, "soft_cap_ccu": 20, "hard_cap_ccu": 25,
	}, nil)
	s6PostJSON(t, env.srv.URL+"/internal/v1/world/heartbeat", "", env.authMgr, map[string]any{
		"server_id": "ded-2", "zone_id": "zone.p01_highland_01", "channel_id": 2,
		"server_addr": "127.0.0.1:7778", "online_ccu": 16, "soft_cap_ccu": 20, "hard_cap_ccu": 25,
		"boss_alive": true, "boss_name": "Highland Titan", "boss_hp_ratio": 0.85,
	}, nil)
	s6PostJSON(t, env.srv.URL+"/internal/v1/world/heartbeat", "", env.authMgr, map[string]any{
		"server_id": "ded-3-full", "zone_id": "zone.p01_highland_01", "channel_id": 3,
		"server_addr": "127.0.0.1:7779", "online_ccu": 25, "soft_cap_ccu": 20, "hard_cap_ccu": 25,
	}, nil)

	const ccu = 20
	var wg sync.WaitGroup
	errCh := make(chan error, ccu*2)

	for i := 0; i < ccu; i++ {
		wg.Add(1)
		go func(idx int) {
			defer wg.Done()
			uName := fmt.Sprintf("S6C_%s_%02d", tag, idx)
			cName := fmt.Sprintf("ZHero%s_%02d", tag[:3], idx)
			_, charID, ep1 := claimStep6Hero(t, env, uName, cName, "ded-1")

			// 1. Transfer to full HardCap channel 3 MUST be rejected (422)!
			if stFull, _ := s6PostJSON(t, env.srv.URL+"/internal/v1/world/transfer", "", env.authMgr, map[string]any{
				"checkpoint": map[string]any{
					"character_id": charID, "fencing_epoch": ep1, "server_id": "ded-1",
					"zone_id": "zone.p01_west_hub", "channel_id": 1, "is_alive": true, "hp": 200, "energy": 200,
				},
				"target_zone_id": "zone.p01_highland_01", "target_channel_id": 3,
			}, nil); stFull != http.StatusUnprocessableEntity {
				errCh <- fmt.Errorf("bot %d expected 422 on full channel 3, got %d", idx, stFull)
				return
			}

			// 2. Valid Seamless Zone Transfer from ded-1 (:7777, Ep#1) -> ded-2 (:7778, Ep#2)!
			var xfer struct {
				TicketID        string `json:"ticket_id"`
				TargetZoneID    string `json:"target_zone_id"`
				TargetChannelID int    `json:"target_channel_id"`
				ServerAddr      string `json:"server_addr"`
			}
			stX, rawX := s6PostJSON(t, env.srv.URL+"/internal/v1/world/transfer", "", env.authMgr, map[string]any{
				"checkpoint": map[string]any{
					"character_id": charID, "fencing_epoch": ep1, "server_id": "ded-1",
					"zone_id": "zone.p01_west_hub", "channel_id": 1,
					"pos_x": 100.0, "pos_y": 200.0, "pos_z": 118.0, "is_alive": true, "hp": 200, "energy": 200,
				},
				"target_zone_id": "zone.p01_highland_01", "target_channel_id": 2,
			}, &xfer)
			if stX != http.StatusOK || xfer.ServerAddr != "127.0.0.1:7778" || xfer.TargetChannelID != 2 {
				errCh <- fmt.Errorf("bot %d transfer failed (%d): %s", idx, stX, string(rawX))
				return
			}

			// 3. Old server stale-epoch write MUST fail with 409!
			if stStale, _ := s6PostJSON(t, env.srv.URL+"/internal/v1/loot/roll-mob", "", env.authMgr, map[string]any{
				"character_id": charID, "fencing_epoch": ep1, "server_id": "ded-1",
				"source_entity_id": "mob.prototype_raider", "zone_id": "zone.p01_west_hub",
				"idempotency_key": fmt.Sprintf("stale_%s_%d", tag, idx),
			}, nil); stStale != http.StatusConflict {
				errCh <- fmt.Errorf("bot %d stale epoch not rejected: got %d", idx, stStale)
				return
			}

			// 4. Destination Dedicated Server #2 claims transfer ticket -> advances to Epoch #2!
			var claim2 character.CharacterState
			stC, rawC := s6PostJSON(t, env.srv.URL+"/internal/v1/lease/claim", "", env.authMgr, map[string]any{
				"ticket_id": xfer.TicketID, "server_id": "ded-2", "zone_id": "zone.p01_highland_01",
			}, &claim2)
			if stC != http.StatusOK || claim2.FencingEpoch != ep1+1 || claim2.ZoneID != "zone.p01_highland_01" || claim2.ChannelID != 2 {
				errCh <- fmt.Errorf("bot %d claim2 failed (%d): %+v %s", idx, stC, claim2, string(rawC))
				return
			}

			// 5. Replay consumed transfer ticket MUST fail with 401!
			if stReplay, _ := s6PostJSON(t, env.srv.URL+"/internal/v1/lease/claim", "", env.authMgr, map[string]any{
				"ticket_id": xfer.TicketID, "server_id": "ded-2", "zone_id": "zone.p01_highland_01",
			}, nil); stReplay != http.StatusUnauthorized {
				errCh <- fmt.Errorf("bot %d transfer ticket replay not rejected: got %d", idx, stReplay)
				return
			}
		}(i)
	}
	wg.Wait()
	close(errCh)
	for e := range errCh {
		t.Fatal(e)
	}

	// Verify ZONE_TRANSFER snapshots were written for all 20 concurrent transfers!
	var snapCount int
	if err := env.pool.QueryRow(context.Background(), `
		SELECT COUNT(*) FROM character_save_snapshots WHERE snapshot_reason = 'ZONE_TRANSFER'
	`).Scan(&snapCount); err != nil || snapCount < ccu {
		t.Fatalf("expected >= %d ZONE_TRANSFER snapshots, got %d (err=%v)", ccu, snapCount, err)
	}
}
