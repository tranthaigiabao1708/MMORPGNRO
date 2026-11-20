package main

import (
	"bytes"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"math"
	"net/http"
	"os"
	"sort"
	"sync"
	"sync/atomic"
	"time"

	"github.com/google/uuid"
)

type botHero struct {
	Index      int
	Username   string
	CharName   string
	Token      string
	CharID     string
	Epoch      int64
	ZoneID     string
	ChannelID  int
	ServerID   string
}

func signHMAC(secret string, body []byte) string {
	mac := hmac.New(sha256.New, []byte(secret))
	mac.Write(body)
	return hex.EncodeToString(mac.Sum(nil))
}

func percentile(sorted []float64, p float64) float64 {
	if len(sorted) == 0 {
		return 0
	}
	idx := int(math.Floor(float64(len(sorted)) * p))
	if idx >= len(sorted) {
		idx = len(sorted) - 1
	}
	return math.Round(sorted[idx]*100.0) / 100.0
}

func main() {
	gatewayURL := flag.String("gateway", "http://127.0.0.1:8080", "Go Gateway HTTP base URL")
	hmacSecret := flag.String("hmac", "iu_step3_internal_server_hmac_2026", "Internal Server HMAC secret")
	ccu := flag.Int("ccu", 20, "Concurrent simulated player sessions (default 20 = 4x5-player Parties)")
	rounds := flag.Int("rounds", 6, "Combat/Quest/Zone-Transfer rounds per bot")
	flag.Parse()

	client := &http.Client{
		Timeout: 10 * time.Second,
		Transport: &http.Transport{
			MaxIdleConns:        128,
			MaxIdleConnsPerHost: 128,
			IdleConnTimeout:     30 * time.Second,
		},
	}

	var latMu sync.Mutex
	latencies := make([]float64, 0, (*ccu)*(*rounds)*8)
	var totalOK atomic.Int64
	var total5xx atomic.Int64
	var chaosBlocked atomic.Int64

	recordLat := func(ms float64) {
		latMu.Lock()
		latencies = append(latencies, ms)
		latMu.Unlock()
	}

	postJSON := func(path string, bearer string, useHMAC bool, payload any, out any) (int, []byte, error) {
		raw, err := json.Marshal(payload)
		if err != nil {
			return 0, nil, err
		}
		req, err := http.NewRequest(http.MethodPost, *gatewayURL+path, bytes.NewReader(raw))
		if err != nil {
			return 0, nil, err
		}
		req.Header.Set("Content-Type", "application/json")
		if bearer != "" {
			req.Header.Set("Authorization", "Bearer "+bearer)
		}
		if useHMAC {
			req.Header.Set("X-IU-Server-HMAC", signHMAC(*hmacSecret, raw))
		}
		t0 := time.Now()
		resp, err := client.Do(req)
		elapsedMs := float64(time.Since(t0).Microseconds()) / 1000.0
		recordLat(elapsedMs)
		if err != nil {
			return 0, nil, err
		}
		defer resp.Body.Close()
		respBody, _ := io.ReadAll(resp.Body)
		if resp.StatusCode >= 500 {
			total5xx.Add(1)
		} else if resp.StatusCode >= 200 && resp.StatusCode < 300 {
			totalOK.Add(1)
			if out != nil && len(respBody) > 0 {
				_ = json.Unmarshal(respBody, out)
			}
		}
		return resp.StatusCode, respBody, nil
	}

	runTag := fmt.Sprintf("%d", time.Now().UnixNano()%1000000)

	// 1. Register 2 Multi-Zone Dedicated Server Nodes (:7777 West Hub Ch.1 & :7778 Highland Boss Ch.2)
	_, _, _ = postJSON("/internal/v1/world/heartbeat", "", true, map[string]any{
		"server_id":    "load-srv-1",
		"zone_id":      "zone.p01_west_hub",
		"channel_id":   1,
		"server_addr":  "127.0.0.1:7777",
		"online_ccu":   0,
		"soft_cap_ccu": 20,
		"hard_cap_ccu": 25,
		"boss_alive":   false,
	}, nil)
	_, _, _ = postJSON("/internal/v1/world/heartbeat", "", true, map[string]any{
		"server_id":     "load-srv-2",
		"zone_id":       "zone.p01_highland_01",
		"channel_id":    2,
		"server_addr":   "127.0.0.1:7778",
		"online_ccu":    0,
		"soft_cap_ccu":  20,
		"hard_cap_ccu":  25,
		"boss_alive":    true,
		"boss_name":     "Highland Titan",
		"boss_hp_ratio": 1.0,
	}, nil)

	// 2. Spawn 20 Concurrent Authenticated Heroes
	bots := make([]*botHero, *ccu)
	var bootWG sync.WaitGroup
	var bootErr atomic.Value

	tStart := time.Now()
	for i := 0; i < *ccu; i++ {
		bootWG.Add(1)
		go func(idx int) {
			defer bootWG.Done()
			uName := fmt.Sprintf("LB_%s_%02d", runTag, idx)
			cName := fmt.Sprintf("Hero%s_%02d", runTag[:4], idx)
			var regResp struct {
				AccessToken string `json:"access_token"`
			}
			code, b, err := postJSON("/v1/auth/register", "", false, map[string]any{
				"username": uName,
				"email":    uName + "@iu.load",
				"password": "LoadBotStep6!2026",
			}, &regResp)
			if err != nil || code != http.StatusCreated {
				bootErr.Store(fmt.Errorf("register bot %d failed (%d): %v %s", idx, code, err, string(b)))
				return
			}

			var charResp struct {
				CharacterID string `json:"character_id"`
			}
			code, b, err = postJSON("/v1/characters", regResp.AccessToken, false, map[string]any{
				"character_name": cName,
				"race_id":        "race.zenith_warrior",
			}, &charResp)
			if err != nil || code != http.StatusCreated {
				bootErr.Store(fmt.Errorf("create char %d failed (%d): %v %s", idx, code, err, string(b)))
				return
			}

			var enterResp struct {
				TicketID     string `json:"ticket_id"`
				TargetZoneID string `json:"target_zone_id"`
			}
			code, b, err = postJSON("/v1/world/enter", regResp.AccessToken, false, map[string]any{
				"character_id": charResp.CharacterID,
			}, &enterResp)
			if err != nil || code != http.StatusOK {
				bootErr.Store(fmt.Errorf("enter world %d failed (%d): %v %s", idx, code, err, string(b)))
				return
			}

			var claimResp struct {
				FencingEpoch int64  `json:"fencing_epoch"`
				ZoneID       string `json:"zone_id"`
			}
			code, b, err = postJSON("/internal/v1/lease/claim", "", true, map[string]any{
				"ticket_id": enterResp.TicketID,
				"server_id": "load-srv-1",
				"zone_id":   enterResp.TargetZoneID,
			}, &claimResp)
			if err != nil || code != http.StatusOK {
				bootErr.Store(fmt.Errorf("claim lease %d failed (%d): %v %s", idx, code, err, string(b)))
				return
			}

			// CHAOS CHECK #1: Replay consumed ticket -> MUST be rejected (401)!
			replayCode, _, _ := postJSON("/internal/v1/lease/claim", "", true, map[string]any{
				"ticket_id": enterResp.TicketID,
				"server_id": "load-srv-1",
				"zone_id":   enterResp.TargetZoneID,
			}, nil)
			if replayCode == http.StatusUnauthorized {
				chaosBlocked.Add(1)
			} else {
				bootErr.Store(fmt.Errorf("ticket replay on bot %d was NOT rejected: got %d", idx, replayCode))
				return
			}

			bots[idx] = &botHero{
				Index:     idx,
				Username:  uName,
				CharName:  cName,
				Token:     regResp.AccessToken,
				CharID:    charResp.CharacterID,
				Epoch:     claimResp.FencingEpoch,
				ZoneID:    claimResp.ZoneID,
				ChannelID: 1,
				ServerID:  "load-srv-1",
			}
		}(i)
	}
	bootWG.Wait()
	if v := bootErr.Load(); v != nil {
		fmt.Fprintf(os.Stderr, "[FATAL] 20-CCU Boot Failed: %v\n", v)
		os.Exit(1)
	}

	// 3. Form 4x 5-Player Parties across the 20 Bots
	for p := 0; p+4 < *ccu; p += 5 {
		leader := bots[p]
		for m := 1; m < 5; m++ {
			member := bots[p+m]
			_, _, _ = postJSON("/internal/v1/party/invite", "", true, map[string]any{
				"character_id":   leader.CharID,
				"character_name": leader.CharName,
				"level":          1,
				"fencing_epoch":  leader.Epoch,
				"server_id":      leader.ServerID,
				"target_char_id": member.CharID,
				"target_name":    member.CharName,
			}, nil)
			_, _, _ = postJSON("/internal/v1/party/respond", "", true, map[string]any{
				"character_id":   member.CharID,
				"character_name": member.CharName,
				"level":          1,
				"fencing_epoch":  member.Epoch,
				"server_id":      member.ServerID,
				"accept":         true,
			}, nil)
		}
	}

	// 4. Concurrent 20-CCU Combat, Quest Progress, Zone Transfer (:7777 -> :7778) & Chaos Stale-Epoch Replay!
	var simWG sync.WaitGroup
	var simErr atomic.Value
	for i := 0; i < *ccu; i++ {
		simWG.Add(1)
		go func(b *botHero) {
			defer simWG.Done()
			for r := 1; r <= *rounds; r++ {
				code, raw, err := postJSON("/internal/v1/character/exp", "", true, map[string]any{
					"character_id":         b.CharID,
					"fencing_epoch":        b.Epoch,
					"server_id":            b.ServerID,
					"outgoing_damage":      85.0,
					"nearby_party_members": 5,
					"killed_target":        true,
					"idempotency_key":      fmt.Sprintf("lb_exp_%s_%d_%d", runTag, b.Index, r),
				}, nil)
				if err != nil || code != http.StatusOK {
					simErr.Store(fmt.Errorf("exp bot %d round %d failed (%d): %v %s", b.Index, r, code, err, string(raw)))
					return
				}

				code, raw, err = postJSON("/internal/v1/loot/roll-mob", "", true, map[string]any{
					"character_id":     b.CharID,
					"fencing_epoch":    b.Epoch,
					"server_id":        b.ServerID,
					"source_entity_id": "mob.prototype_raider",
					"zone_id":          b.ZoneID,
					"idempotency_key":  fmt.Sprintf("lb_loot_%s_%d_%d", runTag, b.Index, r),
				}, nil)
				if err != nil || code != http.StatusOK {
					simErr.Store(fmt.Errorf("loot bot %d round %d failed (%d): %v %s", b.Index, r, code, err, string(raw)))
					return
				}

				// Mid-run Seamless Zone Transfer from West Hub (:7777, Ep#1) -> Highland Boss Zone (:7778, Ep#2)!
				if r == 3 {
					oldEpoch := b.Epoch
					oldServer := b.ServerID
					var xferResp struct {
						TicketID     string `json:"ticket_id"`
						TargetZoneID string `json:"target_zone_id"`
					}
					code, raw, err = postJSON("/internal/v1/world/transfer", "", true, map[string]any{
						"checkpoint": map[string]any{
							"character_id":  b.CharID,
							"fencing_epoch": b.Epoch,
							"server_id":     b.ServerID,
							"zone_id":       b.ZoneID,
							"channel_id":    b.ChannelID,
							"pos_x":         120.0,
							"pos_y":         -80.0,
							"pos_z":         118.0,
							"is_alive":      true,
							"hp":            200.0,
							"energy":        200.0,
						},
						"target_zone_id":    "zone.p01_highland_01",
						"target_channel_id": 2,
					}, &xferResp)
					if err != nil || code != http.StatusOK {
						simErr.Store(fmt.Errorf("transfer bot %d failed (%d): %v %s", b.Index, code, err, string(raw)))
						return
					}

					// CHAOS CHECK #2: Old Server tries to save/mutate using revoked Epoch #1 -> MUST fail (409)!
					staleCode, _, _ := postJSON("/internal/v1/loot/roll-mob", "", true, map[string]any{
						"character_id":     b.CharID,
						"fencing_epoch":    oldEpoch,
						"server_id":        oldServer,
						"source_entity_id": "mob.prototype_raider",
						"zone_id":          "zone.p01_west_hub",
						"idempotency_key":  fmt.Sprintf("lb_stale_%s_%d", runTag, b.Index),
					}, nil)
					if staleCode == http.StatusConflict {
						chaosBlocked.Add(1)
					} else {
						simErr.Store(fmt.Errorf("stale epoch write on bot %d was NOT fenced: got %d", b.Index, staleCode))
						return
					}

					var claim2 struct {
						FencingEpoch int64  `json:"fencing_epoch"`
						ZoneID       string `json:"zone_id"`
						ChannelID    int    `json:"channel_id"`
					}
					code, raw, err = postJSON("/internal/v1/lease/claim", "", true, map[string]any{
						"ticket_id": xferResp.TicketID,
						"server_id": "load-srv-2",
						"zone_id":   xferResp.TargetZoneID,
					}, &claim2)
					if err != nil || code != http.StatusOK {
						simErr.Store(fmt.Errorf("claim destination lease bot %d failed (%d): %v %s", b.Index, code, err, string(raw)))
						return
					}
					b.Epoch = claim2.FencingEpoch
					b.ZoneID = claim2.ZoneID
					b.ChannelID = claim2.ChannelID
					b.ServerID = "load-srv-2"
				}
			}

			// Claim Raider Patrol Quest after >= 3 kills!
			code, raw, err := postJSON("/internal/v1/quest/claim", "", true, map[string]any{
				"character_id":    b.CharID,
				"fencing_epoch":   b.Epoch,
				"server_id":       b.ServerID,
				"quest_id":        "quest.nro_raider_patrol_01",
				"idempotency_key": fmt.Sprintf("lb_q1_%s_%d", runTag, b.Index),
			}, nil)
			if err != nil || code != http.StatusOK {
				simErr.Store(fmt.Errorf("quest claim bot %d failed (%d): %v %s", b.Index, code, err, string(raw)))
				return
			}
		}(bots[i])
	}
	simWG.Wait()
	if v := simErr.Load(); v != nil {
		fmt.Fprintf(os.Stderr, "[FATAL] 20-CCU Simulation Failed: %v\n", v)
		os.Exit(1)
	}

	// 5. Settle 20-Player World Boss Kill (Highland Titan) + Redeem Pity Shop across all 20 Bots!
	participants := make([]map[string]any, 0, *ccu)
	for _, b := range bots {
		cid, _ := uuid.Parse(b.CharID)
		participants = append(participants, map[string]any{
			"character_id":   cid.String(),
			"character_name": b.CharName,
			"fencing_epoch":  b.Epoch,
			"damage_dealt":   25000.0 + float64(b.Index*500), // each ~4-6% > 2% min contribution!
		})
	}
	code, rawBoss, err := postJSON("/internal/v1/boss/settle-kill", "", true, map[string]any{
		"boss_id":           "boss.highland_titan_01",
		"zone_id":           "zone.p01_highland_01",
		"channel_id":        2,
		"server_id":         "load-srv-2",
		"fight_duration_ms": 48500,
		"idempotency_key":   "lb_boss_kill_" + runTag,
		"participants":      participants,
	}, nil)
	if err != nil || code != http.StatusOK {
		fmt.Fprintf(os.Stderr, "[FATAL] 20-CCU Boss Settle Failed (%d): %v %s\n", code, err, string(rawBoss))
		os.Exit(1)
	}

	// Claim Titan Slayer Quest (+3 Titan Tokens -> every bot now has >= 4 Titan Tokens!) & Redeem Crimson Ore Pity Pack!
	var endWG sync.WaitGroup
	for _, b := range bots {
		endWG.Add(1)
		go func(bot *botHero) {
			defer endWG.Done()
			_, _, _ = postJSON("/internal/v1/quest/claim", "", true, map[string]any{
				"character_id":    bot.CharID,
				"fencing_epoch":   bot.Epoch,
				"server_id":       bot.ServerID,
				"quest_id":        "quest.nro_titan_slayer_01",
				"idempotency_key": fmt.Sprintf("lb_q2_%s_%d", runTag, bot.Index),
			}, nil)
			codeRedeem, rawR, errR := postJSON("/internal/v1/boss/pity/redeem", "", true, map[string]any{
				"character_id":    bot.CharID,
				"fencing_epoch":   bot.Epoch,
				"server_id":       bot.ServerID,
				"entry_id":        "pity.mat_crimson_ore_t1",
				"idempotency_key": fmt.Sprintf("lb_pity_%s_%d", runTag, bot.Index),
			}, nil)
			if errR != nil || codeRedeem != http.StatusOK {
				simErr.Store(fmt.Errorf("pity redeem bot %d failed (%d): %v %s", bot.Index, codeRedeem, errR, string(rawR)))
				return
			}
			_, _, _ = postJSON("/internal/v1/lease/release", "", true, map[string]any{
				"character_id":  bot.CharID,
				"fencing_epoch": bot.Epoch,
				"server_id":     bot.ServerID,
			}, nil)
		}(b)
	}
	endWG.Wait()
	if v := simErr.Load(); v != nil {
		fmt.Fprintf(os.Stderr, "[FATAL] 20-CCU Pity/Quest Finale Failed: %v\n", v)
		os.Exit(1)
	}

	totalWall := time.Since(tStart)
	sort.Float64s(latencies)
	var sum float64
	for _, v := range latencies {
		sum += v
	}
	avg := 0.0
	if len(latencies) > 0 {
		avg = math.Round((sum/float64(len(latencies)))*100.0) / 100.0
	}

	fmt.Printf("=== STEP 6 20-CCU LOAD & CHAOS FAULT-INJECTION REPORT ===\n")
	fmt.Printf("ConcurrentHeroes=%d | WallTime=%s | TotalRequests=%d | OK_2xx=%d | HTTP_5xx=%d | ChaosAttacksBlocked=%d\n",
		*ccu, totalWall.Round(time.Millisecond), len(latencies), totalOK.Load(), total5xx.Load(), chaosBlocked.Load())
	fmt.Printf("Latency_ms: Avg=%.2f ms | p50=%.2f ms | p95=%.2f ms | p99=%.2f ms\n",
		avg, percentile(latencies, 0.50), percentile(latencies, 0.95), percentile(latencies, 0.99))

	if total5xx.Load() > 0 {
		fmt.Fprintf(os.Stderr, "[FATAL] Detected %d HTTP 5xx errors during 20-CCU load test!\n", total5xx.Load())
		os.Exit(1)
	}
}
