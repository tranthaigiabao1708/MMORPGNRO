package httpapi_test

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log/slog"
	"math"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/infinite-universe/backend/internal/httpapi"
	"github.com/infinite-universe/backend/internal/modules/account"
	"github.com/infinite-universe/backend/internal/modules/auth"
	"github.com/infinite-universe/backend/internal/modules/character"
	"github.com/infinite-universe/backend/internal/modules/world"
	"github.com/infinite-universe/backend/internal/platform/postgres"
	"github.com/jackc/pgx/v5/pgxpool"
)

const testHMACSecret = "iu_step3_test_hmac_secret_2026"

var testDBSeq atomic.Uint64

type testEnv struct {
	srv     *httptest.Server
	handler *httpapi.Handler
	authMgr *auth.Manager
	pool    *pgxpool.Pool
}

func setupStep3Env(t *testing.T) *testEnv {
	t.Helper()
	baseDSN := strings.TrimSpace(os.Getenv("IU_TEST_PG_DSN"))
	if baseDSN == "" {
		baseDSN = "postgres://iu_admin:iu_dev_secret_2026@127.0.0.1:5433/infinite_universe?sslmode=disable"
	}

	ctx, cancel := context.WithTimeout(context.Background(), 25*time.Second)
	defer cancel()

	adminPool, err := postgres.NewPool(ctx, baseDSN)
	if err != nil {
		t.Fatalf("connect admin postgres (%s): %v", baseDSN, err)
	}

	dbName := fmt.Sprintf("iu_s3_%d_%d", time.Now().UnixNano(), testDBSeq.Add(1))
	if _, err := adminPool.Exec(ctx, fmt.Sprintf(`CREATE DATABASE %s`, dbName)); err != nil {
		adminPool.Close()
		t.Fatalf("create isolated test db %s: %v", dbName, err)
	}

	u, err := url.Parse(baseDSN)
	if err != nil {
		adminPool.Close()
		t.Fatalf("parse baseDSN: %v", err)
	}
	u.Path = "/" + dbName

	pool, err := postgres.NewPool(ctx, u.String())
	if err != nil {
		_, _ = adminPool.Exec(context.Background(), fmt.Sprintf(`DROP DATABASE IF EXISTS %s WITH (FORCE)`, dbName))
		adminPool.Close()
		t.Fatalf("connect isolated db %s: %v", dbName, err)
	}

	_, callerFile, _, _ := runtime.Caller(0)
	migPath := filepath.Clean(filepath.Join(filepath.Dir(callerFile), "..", "..", "migrations", "000001_init_core_schema.up.sql"))
	migSQL, err := os.ReadFile(migPath)
	if err != nil {
		t.Fatalf("read migration %s: %v", migPath, err)
	}
	if _, err := pool.Exec(ctx, string(migSQL)); err != nil {
		t.Fatalf("apply 000001_init_core_schema.up.sql: %v", err)
	}

	logger := slog.New(slog.NewJSONHandler(io.Discard, nil))
	authMgr, err := auth.NewManager("", "", testHMACSecret, nil)
	if err != nil {
		t.Fatalf("init auth manager: %v", err)
	}
	accSvc := account.NewService(pool)
	charSvc := character.NewService(pool)
	worldCoord := world.NewCoordinator(nil, "zone.p01_west_hub", "127.0.0.1:7777")

	h := httpapi.NewHandler(accSvc, authMgr, charSvc, worldCoord, logger)
	mux := http.NewServeMux()
	h.RegisterRoutes(mux)
	srv := httptest.NewServer(mux)

	t.Cleanup(func() {
		srv.Close()
		pool.Close()
		dropCtx, dropCancel := context.WithTimeout(context.Background(), 15*time.Second)
		defer dropCancel()
		_, _ = adminPool.Exec(dropCtx, fmt.Sprintf(`DROP DATABASE IF EXISTS %s WITH (FORCE)`, dbName))
		adminPool.Close()
	})

	return &testEnv{
		srv:     srv,
		handler: h,
		authMgr: authMgr,
		pool:    pool,
	}
}

func doJSON(t *testing.T, method, targetURL string, bearer string, hmacMgr *auth.Manager, reqBody any) (int, map[string]any) {
	t.Helper()
	var bodyBytes []byte
	if reqBody != nil {
		var err error
		bodyBytes, err = json.Marshal(reqBody)
		if err != nil {
			t.Fatalf("marshal json: %v", err)
		}
	}
	req, err := http.NewRequest(method, targetURL, bytes.NewReader(bodyBytes))
	if err != nil {
		t.Fatalf("new request: %v", err)
	}
	req.Header.Set("Content-Type", "application/json")
	if bearer != "" {
		req.Header.Set("Authorization", "Bearer "+bearer)
	}
	if hmacMgr != nil {
		req.Header.Set("X-IU-Server-HMAC", hmacMgr.ComputeInternalHMAC(bodyBytes))
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("http %s %s: %v", method, targetURL, err)
	}
	defer resp.Body.Close()

	raw, _ := io.ReadAll(resp.Body)
	out := make(map[string]any)
	_ = json.Unmarshal(raw, &out)
	return resp.StatusCode, out
}

// Group 3.2: Auth, Argon2id, Ed25519 JWT tamper, Single-Active-Session superseding, SQLi safety & Rate Limit
func TestStep3AuthSecurityAndRateLimit(t *testing.T) {
	env := setupStep3Env(t)

	// 1. Register account
	code, regResp := doJSON(t, "POST", env.srv.URL+"/v1/auth/register", "", nil, map[string]string{
		"username": "GokuHero",
		"email":    "goku@capsule.corp",
		"password": "SuperSaiyanPassword!2026",
	})
	if code != http.StatusCreated {
		t.Fatalf("expected 201 on register, got %d: %+v", code, regResp)
	}
	firstAccessToken := regResp["access_token"].(string)
	firstRefreshToken := regResp["refresh_token"].(string)

	// Verify Argon2id PHC hash stored in PostgreSQL (never plaintext!)
	var storedHash string
	if err := env.pool.QueryRow(context.Background(), `SELECT password_hash FROM accounts WHERE username = 'GokuHero'`).Scan(&storedHash); err != nil {
		t.Fatalf("query stored password_hash: %v", err)
	}
	if !strings.HasPrefix(storedHash, "$argon2id$v=19$") {
		t.Fatalf("expected Argon2id PHC hash, got %q", storedHash)
	}

	// 2. Duplicate case-insensitive CITEXT username rejected with 409
	code, _ = doJSON(t, "POST", env.srv.URL+"/v1/auth/register", "", nil, map[string]string{
		"username": "gokuhero",
		"email":    "other@capsule.corp",
		"password": "SuperSaiyanPassword!2026",
	})
	if code != http.StatusConflict {
		t.Fatalf("expected 409 on CITEXT duplicate username, got %d", code)
	}

	// 3. Wrong password & SQL injection payload safely rejected with 401
	code, _ = doJSON(t, "POST", env.srv.URL+"/v1/auth/login", "", nil, map[string]string{
		"username": "GokuHero",
		"password": "WrongPassword123",
	})
	if code != http.StatusUnauthorized {
		t.Fatalf("expected 401 on wrong password, got %d", code)
	}

	code, _ = doJSON(t, "POST", env.srv.URL+"/v1/auth/login", "", nil, map[string]string{
		"username": "GokuHero' OR '1'='1' --",
		"password": "' OR 1=1 --",
	})
	if code != http.StatusUnauthorized {
		t.Fatalf("expected 401 on SQLi login payload, got %d", code)
	}

	// 4. Tampered Ed25519 JWT signature rejected with 401
	tampered := firstAccessToken[:len(firstAccessToken)-4] + "AAAA"
	code, _ = doJSON(t, "GET", env.srv.URL+"/v1/characters", tampered, nil, nil)
	if code != http.StatusUnauthorized {
		t.Fatalf("expected 401 on tampered Ed25519 JWT, got %d", code)
	}

	// 5. Refresh Token rotation: first refresh succeeds, replaying old refresh token fails 401!
	code, rotResp := doJSON(t, "POST", env.srv.URL+"/v1/auth/refresh", "", nil, map[string]string{
		"refresh_token": firstRefreshToken,
	})
	if code != http.StatusOK {
		t.Fatalf("expected 200 on refresh rotation, got %d: %+v", code, rotResp)
	}
	rotatedAccess := rotResp["access_token"].(string)

	code, _ = doJSON(t, "POST", env.srv.URL+"/v1/auth/refresh", "", nil, map[string]string{
		"refresh_token": firstRefreshToken,
	})
	if code != http.StatusUnauthorized {
		t.Fatalf("expected 401 when replaying consumed refresh token, got %d", code)
	}

	// 6. Single-Active-Session enforcement: logging in from a new client supersedes previous JWT!
	code, secondLogin := doJSON(t, "POST", env.srv.URL+"/v1/auth/login", "", nil, map[string]string{
		"username": "GokuHero",
		"password": "SuperSaiyanPassword!2026",
	})
	if code != http.StatusOK || secondLogin["superseded_prev_sess"] != true {
		t.Fatalf("expected 200 with superseded_prev_sess=true, got %d: %+v", code, secondLogin)
	}

	code, staleResp := doJSON(t, "GET", env.srv.URL+"/v1/characters", rotatedAccess, nil, nil)
	if code != http.StatusUnauthorized || staleResp["error_code"] != "ERR_SESSION_SUPERSEDED" {
		t.Fatalf("expected 401 ERR_SESSION_SUPERSEDED for superseded token, got %d: %+v", code, staleResp)
	}

	// 7. Per-IP Rate Limiter returns 429 Too Many Requests under brute-force burst
	env.handler.SetAuthRateLimit(0.1, 3.0)
	saw429 := false
	for i := 0; i < 6; i++ {
		c, _ := doJSON(t, "POST", env.srv.URL+"/v1/auth/login", "", nil, map[string]string{
			"username": "GokuHero",
			"password": "bad",
		})
		if c == http.StatusTooManyRequests {
			saw429 = true
			break
		}
	}
	if !saw429 {
		t.Fatal("expected 429 Too Many Requests when exceeding per-IP auth rate limit burst")
	}
}

// Group 3.3: Character slot limit, CITEXT uniqueness, Cross-Account Ownership & One-Time World Ticket Replay Rejection
func TestStep3CharactersAndOneTimeWorldTickets(t *testing.T) {
	env := setupStep3Env(t)

	_, regA := doJSON(t, "POST", env.srv.URL+"/v1/auth/register", "", nil, map[string]string{
		"username": "PlayerAlpha",
		"email":    "alpha@iu.dev",
		"password": "AlphaSecurePassword99",
	})
	tokA := regA["access_token"].(string)

	_, regB := doJSON(t, "POST", env.srv.URL+"/v1/auth/register", "", nil, map[string]string{
		"username": "PlayerBeta",
		"email":    "beta@iu.dev",
		"password": "BetaSecurePassword99",
	})
	tokB := regB["access_token"].(string)

	// Create 4 characters (default max_character_slots = 4), then verify 5th is rejected 422!
	var firstCharID string
	for slot := 1; slot <= 4; slot++ {
		code, cResp := doJSON(t, "POST", env.srv.URL+"/v1/characters", tokA, nil, map[string]string{
			"character_name": fmt.Sprintf("ZenithSlot%d", slot),
			"race_id":        "race.zenith_warrior",
		})
		if code != http.StatusCreated {
			t.Fatalf("slot %d create failed (%d): %+v", slot, code, cResp)
		}
		if slot == 1 {
			firstCharID = cResp["character_id"].(string)
		}
	}

	code, _ := doJSON(t, "POST", env.srv.URL+"/v1/characters", tokA, nil, map[string]string{
		"character_name": "ZenithSlot5Overflow",
		"race_id":        "race.zenith_warrior",
	})
	if code != http.StatusUnprocessableEntity {
		t.Fatalf("expected 422 ERR_SLOT_LIMIT on 5th character, got %d", code)
	}

	// PlayerBeta tries to reuse PlayerAlpha's character name (case-insensitive) -> 409 Conflict
	code, _ = doJSON(t, "POST", env.srv.URL+"/v1/characters", tokB, nil, map[string]string{
		"character_name": "zenithslot1",
		"race_id":        "race.terran_adept",
	})
	if code != http.StatusConflict {
		t.Fatalf("expected 409 ERR_NAME_TAKEN on CITEXT duplicate character name, got %d", code)
	}

	// PlayerBeta tries to EnterWorld with PlayerAlpha's character_id -> 403 Forbidden!
	code, _ = doJSON(t, "POST", env.srv.URL+"/v1/world/enter", tokB, nil, map[string]string{
		"character_id": firstCharID,
	})
	if code != http.StatusForbidden {
		t.Fatalf("expected 403 ERR_FORBIDDEN when entering world with another account's character, got %d", code)
	}

	// PlayerAlpha requests a valid one-time world ticket
	code, enterResp := doJSON(t, "POST", env.srv.URL+"/v1/world/enter", tokA, nil, map[string]string{
		"character_id": firstCharID,
	})
	if code != http.StatusOK {
		t.Fatalf("expected 200 on EnterWorld, got %d: %+v", code, enterResp)
	}
	ticketID := enterResp["ticket_id"].(string)

	// Unauthenticated internal server call without valid HMAC must fail 403!
	code, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/lease/claim", "", nil, map[string]string{
		"ticket_id": ticketID,
		"server_id": "rogue-server",
		"zone_id":   "zone.p01_west_hub",
	})
	if code != http.StatusForbidden {
		t.Fatalf("expected 403 ERR_BAD_SERVER_HMAC without valid HMAC, got %d", code)
	}

	// Valid HMAC ClaimLease #1 succeeds and advances fencing_epoch 0 -> 1
	code, claim1 := doJSON(t, "POST", env.srv.URL+"/internal/v1/lease/claim", "", env.authMgr, map[string]string{
		"ticket_id": ticketID,
		"server_id": "ue5-ded-7777",
		"zone_id":   "zone.p01_west_hub",
	})
	if code != http.StatusOK || int64(claim1["fencing_epoch"].(float64)) != 1 {
		t.Fatalf("expected 200 with fencing_epoch=1, got %d: %+v", code, claim1)
	}

	// Replay Attack: ClaimLease #2 with the exact same one-time ticket MUST be rejected 401!
	code, replayResp := doJSON(t, "POST", env.srv.URL+"/internal/v1/lease/claim", "", env.authMgr, map[string]string{
		"ticket_id": ticketID,
		"server_id": "ue5-ded-7777",
		"zone_id":   "zone.p01_west_hub",
	})
	if code != http.StatusUnauthorized {
		t.Fatalf("expected 401 ERR_INVALID_TICKET on one-time ticket replay, got %d: %+v", code, replayResp)
	}
}

// Group 3.4: Split-Brain Two-Server Lease Fencing & Duplicate-Login Eviction Proof
func TestStep3SplitBrainLeaseFencing(t *testing.T) {
	env := setupStep3Env(t)

	_, reg := doJSON(t, "POST", env.srv.URL+"/v1/auth/register", "", nil, map[string]string{
		"username": "VegetaPrince",
		"email":    "vegeta@saiyan.org",
		"password": "FinalFlashPassword!99",
	})
	tok1 := reg["access_token"].(string)

	_, ch := doJSON(t, "POST", env.srv.URL+"/v1/characters", tok1, nil, map[string]string{
		"character_name": "PrinceVegeta",
		"race_id":        "race.zenith_warrior",
	})
	charID := ch["character_id"].(string)

	// Server A claims initial lease (fencing_epoch = 1)
	_, tktA := doJSON(t, "POST", env.srv.URL+"/v1/world/enter", tok1, nil, map[string]string{"character_id": charID})
	code, claimA := doJSON(t, "POST", env.srv.URL+"/internal/v1/lease/claim", "", env.authMgr, map[string]string{
		"ticket_id": tktA["ticket_id"].(string),
		"server_id": "ue5-server-alpha",
		"zone_id":   "zone.p01_west_hub",
	})
	if code != http.StatusOK || int64(claimA["fencing_epoch"].(float64)) != 1 {
		t.Fatalf("Server A claim failed: %d %+v", code, claimA)
	}

	// Server A saves initial valid checkpoint at (-3400, 100, 112)
	code, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/character/save", "", env.authMgr, map[string]any{
		"character_id":    charID,
		"fencing_epoch":   1,
		"server_id":       "ue5-server-alpha",
		"snapshot_reason": "PERIODIC_60S",
		"zone_id":         "zone.p01_west_hub",
		"pos_x":           -3400.0,
		"pos_y":           100.0,
		"pos_z":           112.0,
		"rot_yaw":         90.0,
		"is_alive":        true,
		"hp":              92.0,
		"energy":          85.0,
		"enemy_kills":     1,
	})
	if code != http.StatusOK {
		t.Fatalf("expected Server A epoch=1 save to succeed, got %d", code)
	}

	// Duplicate Login arrives from Client #2 -> revokes Server A's lease and claims on Server B (fencing_epoch = 2)!
	_, login2 := doJSON(t, "POST", env.srv.URL+"/v1/auth/login", "", nil, map[string]string{
		"username": "VegetaPrince",
		"password": "FinalFlashPassword!99",
	})
	tok2 := login2["access_token"].(string)

	_, tktB := doJSON(t, "POST", env.srv.URL+"/v1/world/enter", tok2, nil, map[string]string{"character_id": charID})
	if tktB["kicked_duplicate"] != true {
		t.Fatalf("expected kicked_duplicate=true on duplicate world entry, got %+v", tktB)
	}

	code, claimB := doJSON(t, "POST", env.srv.URL+"/internal/v1/lease/claim", "", env.authMgr, map[string]string{
		"ticket_id": tktB["ticket_id"].(string),
		"server_id": "ue5-server-beta",
		"zone_id":   "zone.p01_west_hub",
	})
	if code != http.StatusOK || int64(claimB["fencing_epoch"].(float64)) != 2 {
		t.Fatalf("expected Server B fencing_epoch=2, got %d %+v", code, claimB)
	}

	// Server B moves character to (+1250, -480, 310) and saves under epoch=2
	code, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/character/save", "", env.authMgr, map[string]any{
		"character_id":    charID,
		"fencing_epoch":   2,
		"server_id":       "ue5-server-beta",
		"snapshot_reason": "PERIODIC_60S",
		"zone_id":         "zone.p01_west_hub",
		"pos_x":           1250.0,
		"pos_y":           -480.0,
		"pos_z":           310.0,
		"rot_yaw":         -45.0,
		"is_alive":        true,
		"hp":              100.0,
		"energy":          100.0,
		"enemy_kills":     3,
	})
	if code != http.StatusOK {
		t.Fatalf("expected Server B epoch=2 save to succeed, got %d", code)
	}

	// Split-Brain Zombie Server A wakes up from network stall:
	// 1) RenewLeases MUST report charID as REVOKED so Server A kicks its stale Pawn!
	code, renewA := doJSON(t, "POST", env.srv.URL+"/internal/v1/lease/renew", "", env.authMgr, map[string]any{
		"server_id": "ue5-server-alpha",
		"leases": []map[string]any{
			{"character_id": charID, "fencing_epoch": 1},
		},
	})
	if code != http.StatusOK {
		t.Fatalf("RenewLeases HTTP error: %d", code)
	}
	revokedSlice := renewA["revoked_character_ids"].([]any)
	if len(revokedSlice) != 1 || revokedSlice[0].(string) != charID {
		t.Fatalf("expected Server A lease to be reported in revoked_character_ids, got %+v", renewA)
	}

	// 2) Stale SaveCharacter & GrantEXP from Server A (epoch=1) MUST be rejected with 409 Conflict!
	code, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/character/save", "", env.authMgr, map[string]any{
		"character_id":    charID,
		"fencing_epoch":   1,
		"server_id":       "ue5-server-alpha",
		"snapshot_reason": "LOGOUT",
		"zone_id":         "zone.p01_west_hub",
		"pos_x":           -99999.0,
		"pos_y":           -99999.0,
		"pos_z":           -99999.0,
		"is_alive":        true,
		"hp":              1.0,
		"energy":          1.0,
	})
	if code != http.StatusConflict {
		t.Fatalf("expected 409 Conflict for stale Server A save (epoch=1 < 2), got %d", code)
	}

	code, _ = doJSON(t, "POST", env.srv.URL+"/internal/v1/character/exp", "", env.authMgr, map[string]any{
		"character_id":    charID,
		"fencing_epoch":   1,
		"server_id":       "ue5-server-alpha",
		"exp_gain":        500,
		"idempotency_key": "stale-kill-1",
	})
	if code != http.StatusConflict {
		t.Fatalf("expected 409 Conflict for stale Server A GrantEXP, got %d", code)
	}

	// Verify PostgreSQL still holds Server B's exact authoritative state (+1250, -480, 310, epoch=2)!
	_, charList := doJSON(t, "GET", env.srv.URL+"/v1/characters", tok2, nil, nil)
	chars := charList["characters"].([]any)
	persisted := chars[0].(map[string]any)
	if math.Abs(persisted["pos_x"].(float64)-1250.0) > 0.01 || int64(persisted["fencing_epoch"].(float64)) != 2 {
		t.Fatalf("split-brain corrupted persisted character state: %+v", persisted)
	}
}

// Groups 3.5, 3.6 & 3.7: 50-Goroutine Concurrent EXP + Checkpoints under -race & Full Persistence Reload
func TestStep3PersistenceAcrossRestartAndConcurrent50Goroutines(t *testing.T) {
	env := setupStep3Env(t)

	_, reg := doJSON(t, "POST", env.srv.URL+"/v1/auth/register", "", nil, map[string]string{
		"username": "GohanScholar",
		"email":    "gohan@iu.dev",
		"password": "MasenkoPassword!2026",
	})
	tok := reg["access_token"].(string)

	_, ch := doJSON(t, "POST", env.srv.URL+"/v1/characters", tok, nil, map[string]string{
		"character_name": "MysticGohan",
		"race_id":        "race.zenith_warrior",
	})
	charID := ch["character_id"].(string)

	_, tkt := doJSON(t, "POST", env.srv.URL+"/v1/world/enter", tok, nil, map[string]string{"character_id": charID})
	_, claim := doJSON(t, "POST", env.srv.URL+"/internal/v1/lease/claim", "", env.authMgr, map[string]string{
		"ticket_id": tkt["ticket_id"].(string),
		"server_id": "ue5-ded-7777",
		"zone_id":   "zone.p01_west_hub",
	})
	epoch := int64(claim["fencing_epoch"].(float64))

	// Launch 50 concurrent goroutines:
	// - 30 goroutines grant 10 unique mob kills (3 goroutines per idempotency_key to verify deduplication!) -> 10 * 55 = 550 total EXP!
	//   Level 1->2 needs 100 EXP (rem 450); Level 2->3 needs 225 EXP (rem 225); Level 3->4 needs 400 EXP -> exact result: Level 3, CurrentEXP = 225, EnemyKills = 10!
	// - 20 goroutines fire concurrent periodic saves & lease renewals!
	var wg sync.WaitGroup
	errCh := make(chan error, 60)

	for g := 0; g < 30; g++ {
		wg.Add(1)
		go func(idx int) {
			defer wg.Done()
			killSlot := idx % 10 // 3x duplicate submissions per killSlot!
			c, resp := doJSON(t, "POST", env.srv.URL+"/internal/v1/character/exp", "", env.authMgr, map[string]any{
				"character_id":    charID,
				"fencing_epoch":   epoch,
				"server_id":       "ue5-ded-7777",
				"exp_gain":        55,
				"idempotency_key": fmt.Sprintf("mob-kill-slot-%d", killSlot),
			})
			if c != http.StatusOK {
				errCh <- fmt.Errorf("goroutine %d GrantEXP HTTP %d: %+v", idx, c, resp)
			}
		}(g)
	}

	for g := 0; g < 20; g++ {
		wg.Add(1)
		go func(idx int) {
			defer wg.Done()
			if idx%2 == 0 {
				c, resp := doJSON(t, "POST", env.srv.URL+"/internal/v1/lease/renew", "", env.authMgr, map[string]any{
					"server_id": "ue5-ded-7777",
					"leases":    []map[string]any{{"character_id": charID, "fencing_epoch": epoch}},
				})
				if c != http.StatusOK {
					errCh <- fmt.Errorf("goroutine %d RenewLease HTTP %d: %+v", idx, c, resp)
				}
			} else {
				c, resp := doJSON(t, "POST", env.srv.URL+"/internal/v1/character/save", "", env.authMgr, map[string]any{
					"character_id":    charID,
					"fencing_epoch":   epoch,
					"server_id":       "ue5-ded-7777",
					"snapshot_reason": "PERIODIC_60S",
					"zone_id":         "zone.p01_west_hub",
					"pos_x":           -3380.0,
					"pos_y":           240.0,
					"pos_z":           114.0,
					"rot_yaw":         135.0,
					"is_alive":        true,
					"hp":              120.0,
					"energy":          110.0,
					"enemy_kills":     10,
				})
				if c != http.StatusOK {
					errCh <- fmt.Errorf("goroutine %d Save HTTP %d: %+v", idx, c, resp)
				}
			}
		}(g)
	}

	wg.Wait()
	close(errCh)
	for e := range errCh {
		t.Fatal(e)
	}

	// Clean Logout & Final Save before simulated Dedicated Server restart
	code, _ := doJSON(t, "POST", env.srv.URL+"/internal/v1/lease/release", "", env.authMgr, map[string]any{
		"final_save": map[string]any{
			"character_id":  charID,
			"fencing_epoch": epoch,
			"server_id":     "ue5-ded-7777",
			"zone_id":       "zone.p01_west_hub",
			"pos_x":         -3412.5,
			"pos_y":         188.25,
			"pos_z":         112.0,
			"rot_yaw":       -90.0,
			"is_alive":      true,
			"hp":            142.0,
			"energy":        125.0,
			"enemy_kills":   10,
			"pvp_kills":     2,
			"deaths":        1,
		},
	})
	if code != http.StatusOK {
		t.Fatalf("ReleaseLease on logout failed: %d", code)
	}

	// Simulated Dedicated Server Restart ("ue5-ded-7777-restarted") claims fresh lease (epoch = 2)
	_, tkt2 := doJSON(t, "POST", env.srv.URL+"/v1/world/enter", tok, nil, map[string]string{"character_id": charID})
	code, reloaded := doJSON(t, "POST", env.srv.URL+"/internal/v1/lease/claim", "", env.authMgr, map[string]string{
		"ticket_id": tkt2["ticket_id"].(string),
		"server_id": "ue5-ded-7777-restarted",
		"zone_id":   "zone.p01_west_hub",
	})
	if code != http.StatusOK {
		t.Fatalf("post-restart ClaimLease failed: %d %+v", code, reloaded)
	}

	if int(reloaded["level"].(float64)) != 3 || int64(reloaded["current_exp"].(float64)) != 225 {
		t.Fatalf("expected deterministic Level=3, CurrentEXP=225 after 10 unique 55-EXP kills, got level=%v exp=%v",
			reloaded["level"], reloaded["current_exp"])
	}
	if math.Abs(reloaded["max_hp"].(float64)-150.0) > 0.01 || math.Abs(reloaded["hp"].(float64)-142.0) > 0.01 {
		t.Fatalf("expected Level 3 MaxHP=150, restored HP=142, got max_hp=%v hp=%v", reloaded["max_hp"], reloaded["hp"])
	}
	if math.Abs(reloaded["pos_x"].(float64)-(-3412.5)) > 0.01 || reloaded["has_saved_pos"] != true {
		t.Fatalf("expected exact restored PosX=-3412.5 & has_saved_pos=true, got %+v", reloaded)
	}

	var snapshotCount int
	if err := env.pool.QueryRow(context.Background(), `SELECT COUNT(*) FROM character_save_snapshots WHERE character_id = $1`, charID).Scan(&snapshotCount); err != nil {
		t.Fatalf("count snapshots: %v", err)
	}
	if snapshotCount < 3 {
		t.Fatalf("expected >= 3 audit snapshots (LEVEL_UP + PERIODIC_60S + LOGOUT), got %d", snapshotCount)
	}
}
