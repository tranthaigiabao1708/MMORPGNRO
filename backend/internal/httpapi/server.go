package httpapi

import (
	"encoding/json"
	"errors"
	"io"
	"log/slog"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/modules/account"
	"github.com/infinite-universe/backend/internal/modules/auth"
	"github.com/infinite-universe/backend/internal/modules/character"
	"github.com/infinite-universe/backend/internal/modules/economy"
	"github.com/infinite-universe/backend/internal/modules/gamedata"
	"github.com/infinite-universe/backend/internal/modules/inventory"
	"github.com/infinite-universe/backend/internal/modules/loot"
	"github.com/infinite-universe/backend/internal/modules/world"
)

const MaxBodyBytes = 64 * 1024

type ipBucket struct {
	tokens     float64
	lastRefill time.Time
}

// Handler exposes the Step 3–4 Client REST API (/v1/*) and HMAC-authenticated UE5 Dedicated Server Bridge (/internal/v1/*).
type Handler struct {
	accounts   *account.Service
	authMgr    *auth.Manager
	characters *character.Service
	worldCoord *world.Coordinator
	economy    *economy.Service
	inventory  *inventory.Service
	loot       *loot.Service
	logger     *slog.Logger

	rateMu    sync.Mutex
	rateRPS   float64
	rateBurst float64
	ipBuckets map[string]ipBucket
}

func NewHandler(
	accounts *account.Service,
	authMgr *auth.Manager,
	characters *character.Service,
	worldCoord *world.Coordinator,
	logger *slog.Logger,
) *Handler {
	return &Handler{
		accounts:   accounts,
		authMgr:    authMgr,
		characters: characters,
		worldCoord: worldCoord,
		logger:     logger,
		rateRPS:    25.0,
		rateBurst:  40.0,
		ipBuckets:  make(map[string]ipBucket),
	}
}

// AttachStep4Modules wires Step 4 Inventory, CSPRNG Loot, and Economy services onto Handler & CharacterService.
func (h *Handler) AttachStep4Modules(
	cat *gamedata.Catalog,
	eco *economy.Service,
	inv *inventory.Service,
	lootSvc *loot.Service,
) {
	if h.characters != nil && cat != nil {
		h.characters.SetCatalog(cat)
	}
	h.economy = eco
	h.inventory = inv
	h.loot = lootSvc
}

// SetAuthRateLimit configures the per-IP token bucket for /v1/auth/* endpoints.
func (h *Handler) SetAuthRateLimit(rps, burst float64) {
	h.rateMu.Lock()
	defer h.rateMu.Unlock()
	h.rateRPS = rps
	h.rateBurst = burst
	h.ipBuckets = make(map[string]ipBucket)
}

func (h *Handler) rateLimitAuth(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		ip, _, err := net.SplitHostPort(r.RemoteAddr)
		if err != nil || ip == "" {
			ip = r.RemoteAddr
		}
		now := time.Now()
		h.rateMu.Lock()
		b, ok := h.ipBuckets[ip]
		if !ok {
			b = ipBucket{tokens: h.rateBurst, lastRefill: now}
		} else {
			elapsed := now.Sub(b.lastRefill).Seconds()
			if elapsed > 0 {
				b.tokens += elapsed * h.rateRPS
				if b.tokens > h.rateBurst {
					b.tokens = h.rateBurst
				}
				b.lastRefill = now
			}
		}
		if b.tokens < 1.0 {
			h.ipBuckets[ip] = b
			h.rateMu.Unlock()
			writeErr(w, http.StatusTooManyRequests, "ERR_RATE_LIMITED", "too many authentication requests; slow down")
			return
		}
		b.tokens -= 1.0
		h.ipBuckets[ip] = b
		h.rateMu.Unlock()
		next(w, r)
	}
}

// RegisterRoutes mounts all Step 3 public & internal endpoints onto mux.
func (h *Handler) RegisterRoutes(mux *http.ServeMux) {
	// Public Client Edge Endpoints (Ed25519 JWT Authenticated + Per-IP Auth Rate Limit)
	mux.HandleFunc("POST /v1/auth/register", h.rateLimitAuth(h.handleRegister))
	mux.HandleFunc("POST /v1/auth/login", h.rateLimitAuth(h.handleLogin))
	mux.HandleFunc("POST /v1/auth/refresh", h.rateLimitAuth(h.handleRefresh))
	mux.HandleFunc("POST /v1/auth/logout", h.handleLogout)
	mux.HandleFunc("GET /v1/characters", h.handleListCharacters)
	mux.HandleFunc("POST /v1/characters", h.handleCreateCharacter)
	mux.HandleFunc("POST /v1/world/enter", h.handleEnterWorld)

	// Internal Authoritative UE5 Dedicated Server Bridge (X-IU-Server-HMAC Authenticated)
	mux.HandleFunc("POST /internal/v1/lease/claim", h.requireServerHMAC(h.handleInternalClaimLease))
	mux.HandleFunc("POST /internal/v1/lease/renew", h.requireServerHMAC(h.handleInternalRenewLeases))
	mux.HandleFunc("POST /internal/v1/character/save", h.requireServerHMAC(h.handleInternalSaveCharacter))
	mux.HandleFunc("POST /internal/v1/character/exp", h.requireServerHMAC(h.handleInternalGrantEXP))
	mux.HandleFunc("POST /internal/v1/character/potential", h.requireServerHMAC(h.handleInternalAllocatePotential))
	mux.HandleFunc("POST /internal/v1/lease/release", h.requireServerHMAC(h.handleInternalReleaseLease))

	// Step 4 Authoritative Inventory, 8-Slot Equipment, CSPRNG Loot & ACID Economy Bridge
	mux.HandleFunc("POST /internal/v1/inventory/get", h.requireServerHMAC(h.handleInternalGetInventory))
	mux.HandleFunc("POST /internal/v1/inventory/equip", h.requireServerHMAC(h.handleInternalEquipItem))
	mux.HandleFunc("POST /internal/v1/inventory/unequip", h.requireServerHMAC(h.handleInternalUnequipSlot))
	mux.HandleFunc("POST /internal/v1/inventory/use", h.requireServerHMAC(h.handleInternalUseConsumable))
	mux.HandleFunc("POST /internal/v1/inventory/lock", h.requireServerHMAC(h.handleInternalToggleItemLock))
	mux.HandleFunc("POST /internal/v1/inventory/sell", h.requireServerHMAC(h.handleInternalSellItem))
	mux.HandleFunc("POST /internal/v1/economy/enhance", h.requireServerHMAC(h.handleInternalEnhanceEquipment))
	mux.HandleFunc("POST /internal/v1/loot/roll-mob", h.requireServerHMAC(h.handleInternalRollMobLoot))
}

func (h *Handler) handleRegister(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Username string `json:"username"`
		Email    string `json:"email"`
		Password string `json:"password"`
	}
	if err := decodeJSON(r, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}

	acc, err := h.accounts.Register(r.Context(), req.Username, req.Email, req.Password)
	if err != nil {
		switch {
		case errors.Is(err, account.ErrAccountAlreadyExists):
			writeErr(w, http.StatusConflict, "ERR_ACCOUNT_EXISTS", err.Error())
		case errors.Is(err, account.ErrInvalidAccountInput):
			writeErr(w, http.StatusBadRequest, "ERR_INVALID_INPUT", err.Error())
		default:
			writeErr(w, http.StatusInternalServerError, "ERR_INTERNAL", err.Error())
		}
		return
	}

	tokens, _, err := h.authMgr.IssueSession(r.Context(), acc.ID, acc.Username)
	if err != nil {
		writeErr(w, http.StatusInternalServerError, "ERR_TOKEN_ISSUE", err.Error())
		return
	}

	writeJSON(w, http.StatusCreated, map[string]any{
		"account_id":    acc.ID.String(),
		"username":      acc.Username,
		"access_token":  tokens.AccessToken,
		"refresh_token": tokens.RefreshToken,
		"expires_in":    tokens.ExpiresInSec,
	})
}

func (h *Handler) handleLogin(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Username string `json:"username"`
		Password string `json:"password"`
	}
	if err := decodeJSON(r, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}

	clientIP, _, _ := net.SplitHostPort(r.RemoteAddr)
	acc, err := h.accounts.Authenticate(r.Context(), req.Username, req.Password, clientIP)
	if err != nil {
		switch {
		case errors.Is(err, account.ErrInvalidCredentials):
			writeErr(w, http.StatusUnauthorized, "ERR_INVALID_CREDENTIALS", err.Error())
		case errors.Is(err, account.ErrAccountNotActive):
			writeErr(w, http.StatusForbidden, "ERR_ACCOUNT_NOT_ACTIVE", err.Error())
		default:
			writeErr(w, http.StatusInternalServerError, "ERR_INTERNAL", err.Error())
		}
		return
	}

	tokens, prevSid, err := h.authMgr.IssueSession(r.Context(), acc.ID, acc.Username)
	if err != nil {
		writeErr(w, http.StatusInternalServerError, "ERR_TOKEN_ISSUE", err.Error())
		return
	}

	writeJSON(w, http.StatusOK, map[string]any{
		"account_id":           acc.ID.String(),
		"username":             acc.Username,
		"access_token":         tokens.AccessToken,
		"refresh_token":        tokens.RefreshToken,
		"expires_in":           tokens.ExpiresInSec,
		"superseded_prev_sess": prevSid != "",
	})
}

func (h *Handler) handleRefresh(w http.ResponseWriter, r *http.Request) {
	var req struct {
		RefreshToken string `json:"refresh_token"`
	}
	if err := decodeJSON(r, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	tokens, err := h.authMgr.Refresh(r.Context(), req.RefreshToken)
	if err != nil {
		writeErr(w, http.StatusUnauthorized, "ERR_INVALID_REFRESH", err.Error())
		return
	}
	writeJSON(w, http.StatusOK, tokens)
}

func (h *Handler) handleLogout(w http.ResponseWriter, r *http.Request) {
	accID, ok := h.authenticateBearer(w, r)
	if !ok {
		return
	}
	h.authMgr.RevokeSession(r.Context(), accID)
	writeJSON(w, http.StatusOK, map[string]string{"status": "logged_out"})
}

func (h *Handler) handleListCharacters(w http.ResponseWriter, r *http.Request) {
	accID, ok := h.authenticateBearer(w, r)
	if !ok {
		return
	}
	chars, err := h.characters.ListByAccount(r.Context(), accID)
	if err != nil {
		writeErr(w, http.StatusInternalServerError, "ERR_LIST_CHARACTERS", err.Error())
		return
	}
	if chars == nil {
		chars = []character.CharacterState{}
	}
	writeJSON(w, http.StatusOK, map[string]any{"characters": chars})
}

func (h *Handler) handleCreateCharacter(w http.ResponseWriter, r *http.Request) {
	accID, ok := h.authenticateBearer(w, r)
	if !ok {
		return
	}
	var req struct {
		CharacterName string `json:"character_name"`
		RaceID        string `json:"race_id"`
	}
	if err := decodeJSON(r, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	st, err := h.characters.CreateCharacter(r.Context(), accID, req.CharacterName, req.RaceID)
	if err != nil {
		switch {
		case errors.Is(err, character.ErrCharacterNameTaken):
			writeErr(w, http.StatusConflict, "ERR_NAME_TAKEN", err.Error())
		case errors.Is(err, character.ErrCharacterSlotLimit):
			writeErr(w, http.StatusUnprocessableEntity, "ERR_SLOT_LIMIT", err.Error())
		case errors.Is(err, character.ErrInvalidCharacterSpec):
			writeErr(w, http.StatusBadRequest, "ERR_INVALID_CHARACTER", err.Error())
		default:
			writeErr(w, http.StatusInternalServerError, "ERR_CREATE_CHARACTER", err.Error())
		}
		return
	}
	writeJSON(w, http.StatusCreated, st)
}

func (h *Handler) handleEnterWorld(w http.ResponseWriter, r *http.Request) {
	accID, ok := h.authenticateBearer(w, r)
	if !ok {
		return
	}
	var req struct {
		CharacterID string `json:"character_id"`
	}
	if err := decodeJSON(r, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, err := uuid.Parse(strings.TrimSpace(req.CharacterID))
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_CHARACTER_ID", "invalid character_id UUID")
		return
	}

	zoneID, err := h.characters.VerifyOwnership(r.Context(), accID, charUUID)
	if err != nil {
		switch {
		case errors.Is(err, character.ErrCharacterNotFound):
			writeErr(w, http.StatusNotFound, "ERR_NOT_FOUND", err.Error())
		case errors.Is(err, character.ErrNotCharacterOwner):
			writeErr(w, http.StatusForbidden, "ERR_FORBIDDEN", err.Error())
		default:
			writeErr(w, http.StatusInternalServerError, "ERR_VERIFY_OWNER", err.Error())
		}
		return
	}

	ticket, kickedDuplicate, err := h.worldCoord.IssueConnectTicket(r.Context(), accID, charUUID, zoneID)
	if err != nil {
		writeErr(w, http.StatusInternalServerError, "ERR_TICKET_ISSUE", err.Error())
		return
	}

	writeJSON(w, http.StatusOK, map[string]any{
		"ticket_id":        ticket.TicketID,
		"character_id":     ticket.CharacterID,
		"target_zone_id":   ticket.TargetZoneID,
		"server_addr":      ticket.ServerAddr,
		"expires_unix":     ticket.ExpiresUnix,
		"kicked_duplicate": kickedDuplicate,
	})
}

// ============================================================================
// Internal Dedicated Server Bridge Handlers (HMAC-SHA256 Protected)
// ============================================================================

func (h *Handler) handleInternalClaimLease(w http.ResponseWriter, r *http.Request, body []byte) {
	var req struct {
		TicketID string `json:"ticket_id"`
		ServerID string `json:"server_id"`
		ZoneID   string `json:"zone_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	if strings.TrimSpace(req.ServerID) == "" {
		writeErr(w, http.StatusBadRequest, "ERR_MISSING_SERVER_ID", "server_id is required")
		return
	}

	ticket, err := h.worldCoord.ConsumeTicket(r.Context(), req.TicketID, req.ZoneID)
	if err != nil {
		writeErr(w, http.StatusUnauthorized, "ERR_INVALID_TICKET", err.Error())
		return
	}

	charUUID, err := uuid.Parse(ticket.CharacterID)
	if err != nil {
		writeErr(w, http.StatusInternalServerError, "ERR_TICKET_CORRUPT", err.Error())
		return
	}

	// Atomically increment PostgreSQL fencing_epoch (+1) and load persistent CharacterState
	charState, err := h.characters.AdvanceEpochAndLoad(r.Context(), charUUID)
	if err != nil {
		writeErr(w, http.StatusInternalServerError, "ERR_EPOCH_CLAIM", err.Error())
		return
	}

	h.worldCoord.ActivateLease(r.Context(), ticket.AccountID, ticket.CharacterID, req.ServerID, charState.ZoneID, charState.FencingEpoch)
	h.logger.Info("character lease claimed by dedicated server",
		slog.String("character_id", charState.ID.String()),
		slog.String("character_name", charState.CharacterName),
		slog.String("server_id", req.ServerID),
		slog.Int64("fencing_epoch", charState.FencingEpoch),
		slog.Int("level", charState.Level),
	)

	writeJSON(w, http.StatusOK, charState)
}

func (h *Handler) handleInternalRenewLeases(w http.ResponseWriter, r *http.Request, body []byte) {
	var req struct {
		ServerID string                   `json:"server_id"`
		Leases   []world.LeaseRenewalItem `json:"leases"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}

	renewed, revoked := h.worldCoord.RenewLeases(r.Context(), req.ServerID, req.Leases)
	if renewed == nil {
		renewed = []string{}
	}
	if revoked == nil {
		revoked = []string{}
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"renewed_character_ids": renewed,
		"revoked_character_ids": revoked,
	})
}

func (h *Handler) handleInternalSaveCharacter(w http.ResponseWriter, r *http.Request, body []byte) {
	var in character.SaveCheckpointInput
	if err := json.Unmarshal(body, &in); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}

	// Double-fence check: verify active coordinator lease AND PostgreSQL WHERE fencing_epoch = $2!
	if err := h.worldCoord.ValidateActiveLease(r.Context(), in.CharacterID.String(), in.ServerID, in.FencingEpoch); err != nil {
		writeErr(w, http.StatusConflict, "ERR_LEASE_REVOKED", err.Error())
		return
	}

	saved, err := h.characters.SaveCheckpointFenced(r.Context(), in)
	if err != nil {
		if errors.Is(err, character.ErrStaleFencingEpoch) {
			writeErr(w, http.StatusConflict, "ERR_STALE_FENCING_EPOCH", err.Error())
			return
		}
		writeErr(w, http.StatusInternalServerError, "ERR_SAVE_FAILED", err.Error())
		return
	}
	writeJSON(w, http.StatusOK, saved)
}

func (h *Handler) handleInternalGrantEXP(w http.ResponseWriter, r *http.Request, body []byte) {
	var req struct {
		CharacterID    string  `json:"character_id"`
		FencingEpoch   int64   `json:"fencing_epoch"`
		ServerID       string  `json:"server_id"`
		OutgoingDamage float64 `json:"outgoing_damage"`
		ExpGain        int64   `json:"exp_gain"`
		PotentialGain  int64   `json:"potential_gain"`
		KilledTarget   bool    `json:"killed_target"`
		IdempotencyKey string  `json:"idempotency_key"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, err := uuid.Parse(strings.TrimSpace(req.CharacterID))
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_CHARACTER_ID", "invalid UUID")
		return
	}

	if err := h.worldCoord.ValidateActiveLease(r.Context(), charUUID.String(), req.ServerID, req.FencingEpoch); err != nil {
		writeErr(w, http.StatusConflict, "ERR_LEASE_REVOKED", err.Error())
		return
	}

	res, err := h.characters.GrantEXPFenced(
		r.Context(),
		charUUID,
		req.FencingEpoch,
		req.ServerID,
		req.OutgoingDamage,
		req.ExpGain,
		req.PotentialGain,
		req.KilledTarget,
		req.IdempotencyKey,
	)
	if err != nil {
		if errors.Is(err, character.ErrStaleFencingEpoch) {
			writeErr(w, http.StatusConflict, "ERR_STALE_FENCING_EPOCH", err.Error())
			return
		}
		writeErr(w, http.StatusInternalServerError, "ERR_EXP_GRANT", err.Error())
		return
	}
	writeJSON(w, http.StatusOK, res)
}

func (h *Handler) handleInternalAllocatePotential(w http.ResponseWriter, r *http.Request, body []byte) {
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
		StatType     string `json:"stat_type"` // "hp" | "ki" | "atk"
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, err := uuid.Parse(strings.TrimSpace(req.CharacterID))
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_CHARACTER_ID", "invalid UUID")
		return
	}

	if err := h.worldCoord.ValidateActiveLease(r.Context(), charUUID.String(), req.ServerID, req.FencingEpoch); err != nil {
		writeErr(w, http.StatusConflict, "ERR_LEASE_REVOKED", err.Error())
		return
	}

	res, err := h.characters.AllocatePotentialFenced(r.Context(), charUUID, req.FencingEpoch, req.ServerID, req.StatType)
	if err != nil {
		switch {
		case errors.Is(err, character.ErrStaleFencingEpoch):
			writeErr(w, http.StatusConflict, "ERR_STALE_FENCING_EPOCH", err.Error())
		case errors.Is(err, character.ErrInsufficientPotential):
			writeErr(w, http.StatusUnprocessableEntity, "ERR_INSUFFICIENT_POTENTIAL", err.Error())
		case errors.Is(err, character.ErrInvalidPotentialStat):
			writeErr(w, http.StatusBadRequest, "ERR_INVALID_STAT_TYPE", err.Error())
		default:
			writeErr(w, http.StatusInternalServerError, "ERR_POTENTIAL_ALLOCATE", err.Error())
		}
		return
	}
	writeJSON(w, http.StatusOK, res)
}

func (h *Handler) handleInternalReleaseLease(w http.ResponseWriter, r *http.Request, body []byte) {
	var req struct {
		SaveInput    *character.SaveCheckpointInput `json:"final_save"`
		CharacterID  string                         `json:"character_id"`
		FencingEpoch int64                          `json:"fencing_epoch"`
		ServerID     string                         `json:"server_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}

	if req.SaveInput != nil && req.SaveInput.CharacterID != uuid.Nil {
		req.SaveInput.SnapshotReason = "LOGOUT"
		if err := h.worldCoord.ValidateActiveLease(r.Context(), req.SaveInput.CharacterID.String(), req.SaveInput.ServerID, req.SaveInput.FencingEpoch); err == nil {
			_, _ = h.characters.SaveCheckpointFenced(r.Context(), *req.SaveInput)
		}
		h.worldCoord.ReleaseLease(r.Context(), req.SaveInput.CharacterID.String(), req.SaveInput.ServerID, req.SaveInput.FencingEpoch)
		writeJSON(w, http.StatusOK, map[string]string{"status": "saved_and_released"})
		return
	}

	h.worldCoord.ReleaseLease(r.Context(), req.CharacterID, req.ServerID, req.FencingEpoch)
	writeJSON(w, http.StatusOK, map[string]string{"status": "released"})
}

func (h *Handler) writeInventoryErr(w http.ResponseWriter, err error) {
	switch {
	case errors.Is(err, inventory.ErrStaleFencingEpoch), errors.Is(err, loot.ErrStaleFencingEpoch):
		writeErr(w, http.StatusConflict, "ERR_STALE_FENCING_EPOCH", err.Error())
	case errors.Is(err, inventory.ErrItemNotFound):
		writeErr(w, http.StatusNotFound, "ERR_ITEM_NOT_FOUND", err.Error())
	case errors.Is(err, inventory.ErrLevelTooLow):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_LEVEL_TOO_LOW", err.Error())
	case errors.Is(err, inventory.ErrRaceNotAllowed):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_RACE_NOT_ALLOWED", err.Error())
	case errors.Is(err, inventory.ErrBagFull):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_BAG_FULL", err.Error())
	case errors.Is(err, inventory.ErrCannotSellEquipped):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_CANNOT_SELL_EQUIPPED", err.Error())
	case errors.Is(err, inventory.ErrItemLocked):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_ITEM_LOCKED", err.Error())
	case errors.Is(err, inventory.ErrNotEquippable), errors.Is(err, inventory.ErrNotSellable), errors.Is(err, inventory.ErrInvalidSlot):
		writeErr(w, http.StatusBadRequest, "ERR_INVALID_ITEM_OP", err.Error())
	case errors.Is(err, inventory.ErrConsumableEmpty):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_CONSUMABLE_EMPTY", err.Error())
	case errors.Is(err, inventory.ErrMaxEnhanceReached):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_MAX_ENHANCE", err.Error())
	case errors.Is(err, inventory.ErrInsufficientMat):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_INSUFFICIENT_MAT", err.Error())
	case errors.Is(err, economy.ErrInsufficientCoins):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_INSUFFICIENT_COINS", err.Error())
	default:
		writeErr(w, http.StatusInternalServerError, "ERR_INVENTORY_ECONOMY", err.Error())
	}
}

func (h *Handler) validateFencedCharReq(w http.ResponseWriter, r *http.Request, charIDStr, serverID string, epoch int64) (uuid.UUID, bool) {
	if h.inventory == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP4_DISABLED", "inventory module not attached")
		return uuid.Nil, false
	}
	charUUID, err := uuid.Parse(strings.TrimSpace(charIDStr))
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_CHARACTER_ID", "invalid character_id UUID")
		return uuid.Nil, false
	}
	if err := h.worldCoord.ValidateActiveLease(r.Context(), charUUID.String(), serverID, epoch); err != nil {
		writeErr(w, http.StatusConflict, "ERR_LEASE_REVOKED", err.Error())
		return uuid.Nil, false
	}
	return charUUID, true
}

func (h *Handler) handleInternalGetInventory(w http.ResponseWriter, r *http.Request, body []byte) {
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateFencedCharReq(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	snap, err := h.inventory.GetInventorySnapshot(r.Context(), charUUID)
	if err != nil {
		h.writeInventoryErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalEquipItem(w http.ResponseWriter, r *http.Request, body []byte) {
	var req struct {
		CharacterID    string `json:"character_id"`
		FencingEpoch   int64  `json:"fencing_epoch"`
		ServerID       string `json:"server_id"`
		ItemInstanceID string `json:"item_instance_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateFencedCharReq(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	itemUUID, err := uuid.Parse(strings.TrimSpace(req.ItemInstanceID))
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_ITEM_ID", "invalid item_instance_id UUID")
		return
	}
	snap, err := h.inventory.EquipItemFenced(r.Context(), charUUID, req.FencingEpoch, req.ServerID, itemUUID)
	if err != nil {
		h.writeInventoryErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalUnequipSlot(w http.ResponseWriter, r *http.Request, body []byte) {
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
		SlotID       string `json:"slot_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateFencedCharReq(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	snap, err := h.inventory.UnequipSlotFenced(r.Context(), charUUID, req.FencingEpoch, req.ServerID, req.SlotID)
	if err != nil {
		h.writeInventoryErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalUseConsumable(w http.ResponseWriter, r *http.Request, body []byte) {
	var req struct {
		CharacterID    string `json:"character_id"`
		FencingEpoch   int64  `json:"fencing_epoch"`
		ServerID       string `json:"server_id"`
		ItemInstanceID string `json:"item_instance_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateFencedCharReq(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	var instPtr *uuid.UUID
	if strings.TrimSpace(req.ItemInstanceID) != "" {
		parsed, err := uuid.Parse(strings.TrimSpace(req.ItemInstanceID))
		if err != nil {
			writeErr(w, http.StatusBadRequest, "ERR_BAD_ITEM_ID", "invalid item_instance_id UUID")
			return
		}
		instPtr = &parsed
	}
	snap, err := h.inventory.UseConsumableFenced(r.Context(), charUUID, req.FencingEpoch, req.ServerID, instPtr)
	if err != nil {
		h.writeInventoryErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalToggleItemLock(w http.ResponseWriter, r *http.Request, body []byte) {
	var req struct {
		CharacterID    string `json:"character_id"`
		FencingEpoch   int64  `json:"fencing_epoch"`
		ServerID       string `json:"server_id"`
		ItemInstanceID string `json:"item_instance_id"`
		Locked         bool   `json:"locked"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateFencedCharReq(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	itemUUID, err := uuid.Parse(strings.TrimSpace(req.ItemInstanceID))
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_ITEM_ID", "invalid item_instance_id UUID")
		return
	}
	snap, err := h.inventory.ToggleItemLockFenced(r.Context(), charUUID, req.FencingEpoch, itemUUID, req.Locked)
	if err != nil {
		h.writeInventoryErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalSellItem(w http.ResponseWriter, r *http.Request, body []byte) {
	var req struct {
		CharacterID    string `json:"character_id"`
		FencingEpoch   int64  `json:"fencing_epoch"`
		ServerID       string `json:"server_id"`
		ItemInstanceID string `json:"item_instance_id"`
		Quantity       int    `json:"quantity"`
		IdempotencyKey string `json:"idempotency_key"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateFencedCharReq(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	itemUUID, err := uuid.Parse(strings.TrimSpace(req.ItemInstanceID))
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_ITEM_ID", "invalid item_instance_id UUID")
		return
	}
	snap, err := h.inventory.SellItemFenced(r.Context(), charUUID, req.FencingEpoch, req.ServerID, itemUUID, req.Quantity, req.IdempotencyKey)
	if err != nil {
		h.writeInventoryErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalEnhanceEquipment(w http.ResponseWriter, r *http.Request, body []byte) {
	var req struct {
		CharacterID    string `json:"character_id"`
		FencingEpoch   int64  `json:"fencing_epoch"`
		ServerID       string `json:"server_id"`
		ItemInstanceID string `json:"item_instance_id"`
		IdempotencyKey string `json:"idempotency_key"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateFencedCharReq(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	itemUUID, err := uuid.Parse(strings.TrimSpace(req.ItemInstanceID))
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_ITEM_ID", "invalid item_instance_id UUID")
		return
	}
	snap, err := h.inventory.EnhanceEquipmentFenced(r.Context(), charUUID, req.FencingEpoch, req.ServerID, itemUUID, req.IdempotencyKey)
	if err != nil {
		h.writeInventoryErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalRollMobLoot(w http.ResponseWriter, r *http.Request, body []byte) {
	var req struct {
		CharacterID    string `json:"character_id"`
		FencingEpoch   int64  `json:"fencing_epoch"`
		ServerID       string `json:"server_id"`
		SourceEntityID string `json:"source_entity_id"`
		ZoneID         string `json:"zone_id"`
		IdempotencyKey string `json:"idempotency_key"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	if h.loot == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP4_DISABLED", "loot module not attached")
		return
	}
	charUUID, ok := h.validateFencedCharReq(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	lootRes, err := h.loot.RollMonsterLootFenced(r.Context(), charUUID, req.FencingEpoch, req.ServerID, req.SourceEntityID, req.ZoneID, req.IdempotencyKey)
	if err != nil {
		h.writeInventoryErr(w, err)
		return
	}
	snap, err := h.inventory.GetInventorySnapshot(r.Context(), charUUID)
	if err != nil {
		h.writeInventoryErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"loot":      lootRes,
		"inventory": snap,
	})
}

func (h *Handler) authenticateBearer(w http.ResponseWriter, r *http.Request) (uuid.UUID, bool) {
	hdr := strings.TrimSpace(r.Header.Get("Authorization"))
	if !strings.HasPrefix(strings.ToLower(hdr), "bearer ") {
		writeErr(w, http.StatusUnauthorized, "ERR_MISSING_BEARER", "Authorization: Bearer <token> required")
		return uuid.Nil, false
	}
	tok := strings.TrimSpace(hdr[7:])
	claims, err := h.authMgr.VerifyAccessToken(r.Context(), tok)
	if err != nil {
		code := "ERR_INVALID_TOKEN"
		if errors.Is(err, auth.ErrSessionStale) {
			code = "ERR_SESSION_SUPERSEDED"
		}
		writeErr(w, http.StatusUnauthorized, code, err.Error())
		return uuid.Nil, false
	}
	accID, err := uuid.Parse(claims.Subject)
	if err != nil {
		writeErr(w, http.StatusUnauthorized, "ERR_INVALID_TOKEN", "malformed sub")
		return uuid.Nil, false
	}
	return accID, true
}

func (h *Handler) requireServerHMAC(next func(http.ResponseWriter, *http.Request, []byte)) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		body, err := io.ReadAll(io.LimitReader(r.Body, MaxBodyBytes))
		if err != nil {
			writeErr(w, http.StatusBadRequest, "ERR_READ_BODY", err.Error())
			return
		}
		sig := r.Header.Get("X-IU-Server-HMAC")
		if err := h.authMgr.VerifyInternalHMAC(body, sig); err != nil {
			writeErr(w, http.StatusForbidden, "ERR_BAD_SERVER_HMAC", err.Error())
			return
		}
		next(w, r, body)
	}
}

func decodeJSON(r *http.Request, dst any) error {
	dec := json.NewDecoder(io.LimitReader(r.Body, MaxBodyBytes))
	return dec.Decode(dst)
}

func writeJSON(w http.ResponseWriter, code int, payload any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(payload)
}

func writeErr(w http.ResponseWriter, code int, errCode, msg string) {
	writeJSON(w, code, map[string]string{
		"error_code": errCode,
		"message":    msg,
	})
}
