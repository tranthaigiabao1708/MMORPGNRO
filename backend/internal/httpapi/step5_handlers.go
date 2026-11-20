package httpapi

import (
	"encoding/json"
	"errors"
	"net/http"
	"strings"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/modules/economy"
	"github.com/infinite-universe/backend/internal/modules/guild"
	"github.com/infinite-universe/backend/internal/modules/inventory"
	"github.com/infinite-universe/backend/internal/modules/party"
	"github.com/infinite-universe/backend/internal/modules/social"
	"github.com/infinite-universe/backend/internal/modules/trade"
)

func (h *Handler) validateLeaseOnly(w http.ResponseWriter, r *http.Request, charIDStr, serverID string, epoch int64) (uuid.UUID, bool) {
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

// ============================================================================
// Step 5A: 5-Player Party System Handlers (/internal/v1/party/*)
// ============================================================================

func (h *Handler) writePartyErr(w http.ResponseWriter, err error) {
	switch {
	case errors.Is(err, party.ErrCannotInviteSelf):
		writeErr(w, http.StatusBadRequest, "ERR_PARTY_SELF_INVITE", err.Error())
	case errors.Is(err, party.ErrAlreadyInParty), errors.Is(err, party.ErrTargetAlreadyInParty):
		writeErr(w, http.StatusConflict, "ERR_ALREADY_IN_PARTY", err.Error())
	case errors.Is(err, party.ErrPartyFull):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_PARTY_FULL", err.Error())
	case errors.Is(err, party.ErrNotPartyLeader):
		writeErr(w, http.StatusForbidden, "ERR_NOT_PARTY_LEADER", err.Error())
	case errors.Is(err, party.ErrNotInParty), errors.Is(err, party.ErrInviteNotFound), errors.Is(err, party.ErrMemberNotFound):
		writeErr(w, http.StatusNotFound, "ERR_PARTY_NOT_FOUND", err.Error())
	default:
		writeErr(w, http.StatusInternalServerError, "ERR_PARTY_INTERNAL", err.Error())
	}
}

func (h *Handler) handleInternalPartyGet(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.partyCoord == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "party module not attached")
		return
	}
	var req struct {
		CharacterID   string  `json:"character_id"`
		CharacterName string  `json:"character_name"`
		FencingEpoch  int64   `json:"fencing_epoch"`
		ServerID      string  `json:"server_id"`
		Level         int     `json:"level"`
		HP            float64 `json:"hp"`
		MaxHP         float64 `json:"max_hp"`
		Energy        float64 `json:"energy"`
		MaxEnergy     float64 `json:"max_energy"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	if req.MaxHP > 0 || req.Level > 0 || req.CharacterName != "" {
		h.partyCoord.SyncMemberVitals(charUUID.String(), req.CharacterName, req.Level, req.HP, req.MaxHP, req.Energy, req.MaxEnergy)
	}
	writeJSON(w, http.StatusOK, h.partyCoord.GetSnapshot(charUUID.String()))
}

func (h *Handler) handleInternalPartyInvite(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.partyCoord == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "party module not attached")
		return
	}
	var req struct {
		CharacterID   string `json:"character_id"`
		CharacterName string `json:"character_name"`
		Level         int    `json:"level"`
		FencingEpoch  int64  `json:"fencing_epoch"`
		ServerID      string `json:"server_id"`
		TargetCharID  string `json:"target_char_id"`
		TargetName    string `json:"target_name"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	inviterUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	targetUUID, err := uuid.Parse(strings.TrimSpace(req.TargetCharID))
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_TARGET_ID", "invalid target_char_id UUID")
		return
	}
	snap, err := h.partyCoord.Invite(inviterUUID.String(), req.CharacterName, req.Level, targetUUID.String(), req.TargetName)
	if err != nil {
		h.writePartyErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalPartyRespond(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.partyCoord == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "party module not attached")
		return
	}
	var req struct {
		CharacterID   string `json:"character_id"`
		CharacterName string `json:"character_name"`
		Level         int    `json:"level"`
		FencingEpoch  int64  `json:"fencing_epoch"`
		ServerID      string `json:"server_id"`
		InviteID      string `json:"invite_id"`
		Accept        bool   `json:"accept"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	snap, err := h.partyCoord.RespondInvite(charUUID.String(), req.CharacterName, req.Level, req.InviteID, req.Accept)
	if err != nil {
		h.writePartyErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalPartyLeave(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.partyCoord == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "party module not attached")
		return
	}
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	snap, err := h.partyCoord.Leave(charUUID.String())
	if err != nil {
		h.writePartyErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalPartyKick(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.partyCoord == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "party module not attached")
		return
	}
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
		TargetCharID string `json:"target_char_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	leaderUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	targetUUID, err := uuid.Parse(strings.TrimSpace(req.TargetCharID))
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_TARGET_ID", "invalid target_char_id UUID")
		return
	}
	snap, err := h.partyCoord.Kick(leaderUUID.String(), targetUUID.String())
	if err != nil {
		h.writePartyErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalPartyPromote(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.partyCoord == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "party module not attached")
		return
	}
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
		TargetCharID string `json:"target_char_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	leaderUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	targetUUID, err := uuid.Parse(strings.TrimSpace(req.TargetCharID))
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_TARGET_ID", "invalid target_char_id UUID")
		return
	}
	snap, err := h.partyCoord.PromoteLeader(leaderUUID.String(), targetUUID.String())
	if err != nil {
		h.writePartyErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

// ============================================================================
// Step 5B: Guild System Handlers (/internal/v1/guild/*)
// ============================================================================

func (h *Handler) writeGuildErr(w http.ResponseWriter, err error) {
	switch {
	case errors.Is(err, guild.ErrStaleFencingEpoch):
		writeErr(w, http.StatusConflict, "ERR_STALE_FENCING_EPOCH", err.Error())
	case errors.Is(err, guild.ErrGuildLevelTooLow):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_GUILD_LEVEL_TOO_LOW", err.Error())
	case errors.Is(err, guild.ErrInvalidGuildNameOrTag), errors.Is(err, guild.ErrInvalidDonateAmount), errors.Is(err, guild.ErrInvalidRole):
		writeErr(w, http.StatusBadRequest, "ERR_GUILD_INVALID_INPUT", err.Error())
	case errors.Is(err, guild.ErrGuildNameOrTagTaken):
		writeErr(w, http.StatusConflict, "ERR_GUILD_TAKEN", err.Error())
	case errors.Is(err, guild.ErrAlreadyInGuild):
		writeErr(w, http.StatusConflict, "ERR_ALREADY_IN_GUILD", err.Error())
	case errors.Is(err, guild.ErrNotInGuild), errors.Is(err, guild.ErrGuildNotFound):
		writeErr(w, http.StatusNotFound, "ERR_GUILD_NOT_FOUND", err.Error())
	case errors.Is(err, guild.ErrGuildFull):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_GUILD_FULL", err.Error())
	case errors.Is(err, guild.ErrInsufficientGuildPerm):
		writeErr(w, http.StatusForbidden, "ERR_GUILD_FORBIDDEN", err.Error())
	case errors.Is(err, guild.ErrLeaderCannotLeave):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_LEADER_CANNOT_LEAVE", err.Error())
	case errors.Is(err, economy.ErrInsufficientCoins):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_INSUFFICIENT_COINS", err.Error())
	default:
		writeErr(w, http.StatusInternalServerError, "ERR_GUILD_INTERNAL", err.Error())
	}
}

func (h *Handler) handleInternalGuildGet(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.guildSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "guild module not attached")
		return
	}
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	snap, err := h.guildSvc.GetGuildSnapshot(r.Context(), charUUID)
	if err != nil {
		h.writeGuildErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalGuildCreate(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.guildSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "guild module not attached")
		return
	}
	var req struct {
		CharacterID    string `json:"character_id"`
		FencingEpoch   int64  `json:"fencing_epoch"`
		ServerID       string `json:"server_id"`
		GuildName      string `json:"guild_name"`
		GuildTag       string `json:"guild_tag"`
		MOTD           string `json:"motd_announcement"`
		IdempotencyKey string `json:"idempotency_key"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	snap, err := h.guildSvc.CreateGuildFenced(r.Context(), charUUID, req.FencingEpoch, req.ServerID, req.GuildName, req.GuildTag, req.MOTD, req.IdempotencyKey)
	if err != nil {
		h.writeGuildErr(w, err)
		return
	}
	writeJSON(w, http.StatusCreated, snap)
}

func (h *Handler) handleInternalGuildJoin(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.guildSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "guild module not attached")
		return
	}
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
		GuildID      string `json:"guild_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	guildUUID, err := uuid.Parse(strings.TrimSpace(req.GuildID))
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_GUILD_ID", "invalid guild_id UUID")
		return
	}
	snap, err := h.guildSvc.JoinGuildFenced(r.Context(), charUUID, req.FencingEpoch, guildUUID)
	if err != nil {
		h.writeGuildErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalGuildLeave(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.guildSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "guild module not attached")
		return
	}
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	snap, err := h.guildSvc.LeaveGuildFenced(r.Context(), charUUID, req.FencingEpoch)
	if err != nil {
		h.writeGuildErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalGuildKick(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.guildSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "guild module not attached")
		return
	}
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
		TargetCharID string `json:"target_char_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	actorUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	targetUUID, err := uuid.Parse(strings.TrimSpace(req.TargetCharID))
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_TARGET_ID", "invalid target_char_id UUID")
		return
	}
	snap, err := h.guildSvc.KickMemberFenced(r.Context(), actorUUID, req.FencingEpoch, targetUUID)
	if err != nil {
		h.writeGuildErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalGuildRole(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.guildSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "guild module not attached")
		return
	}
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
		TargetCharID string `json:"target_char_id"`
		MemberRole   string `json:"member_role"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	actorUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	targetUUID, err := uuid.Parse(strings.TrimSpace(req.TargetCharID))
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_TARGET_ID", "invalid target_char_id UUID")
		return
	}
	snap, err := h.guildSvc.SetMemberRoleFenced(r.Context(), actorUUID, req.FencingEpoch, targetUUID, req.MemberRole)
	if err != nil {
		h.writeGuildErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalGuildMOTD(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.guildSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "guild module not attached")
		return
	}
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
		MOTD         string `json:"motd_announcement"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	actorUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	snap, err := h.guildSvc.UpdateMOTDFenced(r.Context(), actorUUID, req.FencingEpoch, req.MOTD)
	if err != nil {
		h.writeGuildErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalGuildDonate(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.guildSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "guild module not attached")
		return
	}
	var req struct {
		CharacterID    string `json:"character_id"`
		FencingEpoch   int64  `json:"fencing_epoch"`
		ServerID       string `json:"server_id"`
		AmountCoin     int64  `json:"amount_coin"`
		IdempotencyKey string `json:"idempotency_key"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	snap, err := h.guildSvc.DonateCoinsFenced(r.Context(), charUUID, req.FencingEpoch, req.ServerID, req.AmountCoin, req.IdempotencyKey)
	if err != nil {
		h.writeGuildErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

// ============================================================================
// Step 5C: Atomic 2-Step P2P Direct Trade Handlers (/internal/v1/trade/*)
// ============================================================================

func (h *Handler) writeTradeErr(w http.ResponseWriter, err error) {
	switch {
	case errors.Is(err, trade.ErrSelfTrade), errors.Is(err, trade.ErrNegativeCoinOffer), errors.Is(err, trade.ErrDuplicateOfferItem), errors.Is(err, trade.ErrTooManyTradeItems):
		writeErr(w, http.StatusBadRequest, "ERR_TRADE_INVALID_INPUT", err.Error())
	case errors.Is(err, trade.ErrPlayerBusyInTrade):
		writeErr(w, http.StatusConflict, "ERR_PLAYER_BUSY_IN_TRADE", err.Error())
	case errors.Is(err, trade.ErrTradeNotFound), errors.Is(err, inventory.ErrItemNotFound):
		writeErr(w, http.StatusNotFound, "ERR_TRADE_NOT_FOUND", err.Error())
	case errors.Is(err, trade.ErrNotTradeParticipant):
		writeErr(w, http.StatusForbidden, "ERR_NOT_TRADE_PARTICIPANT", err.Error())
	case errors.Is(err, trade.ErrTradeAlreadyBothLocked), errors.Is(err, trade.ErrTradeNotBothLocked), errors.Is(err, trade.ErrInvalidTradeState):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_TRADE_STATE", err.Error())
	case errors.Is(err, trade.ErrInspectionCountdownActive):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_TRADE_COUNTDOWN_ACTIVE", err.Error())
	case errors.Is(err, trade.ErrItemBoundUntradeable):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_ITEM_BOUND", err.Error())
	case errors.Is(err, trade.ErrItemLockedUntradeable):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_ITEM_LOCKED", err.Error())
	case errors.Is(err, economy.ErrInsufficientCoins):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_INSUFFICIENT_COINS", err.Error())
	case errors.Is(err, inventory.ErrBagFull):
		writeErr(w, http.StatusUnprocessableEntity, "ERR_BAG_FULL", err.Error())
	case errors.Is(err, trade.ErrStaleFencingEpoch), errors.Is(err, trade.ErrTradeConflictAborted):
		writeErr(w, http.StatusConflict, "ERR_TRADE_CONFLICT", err.Error())
	default:
		writeErr(w, http.StatusInternalServerError, "ERR_TRADE_INTERNAL", err.Error())
	}
}

func (h *Handler) handleInternalTradeGet(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.tradeSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "trade module not attached")
		return
	}
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	writeJSON(w, http.StatusOK, h.tradeSvc.GetActiveSession(charUUID))
}

func (h *Handler) handleInternalTradeRequest(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.tradeSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "trade module not attached")
		return
	}
	var req struct {
		CharacterID     string `json:"character_id"`
		FencingEpoch    int64  `json:"fencing_epoch"`
		ServerID        string `json:"server_id"`
		RecipientCharID string `json:"recipient_char_id"`
		ZoneID          string `json:"zone_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	initUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	recipUUID, err := uuid.Parse(strings.TrimSpace(req.RecipientCharID))
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_TARGET_ID", "invalid recipient_char_id UUID")
		return
	}
	snap, err := h.tradeSvc.RequestTrade(r.Context(), initUUID, req.FencingEpoch, recipUUID, req.ZoneID)
	if err != nil {
		h.writeTradeErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalTradeRespond(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.tradeSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "trade module not attached")
		return
	}
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
		TradeID      string `json:"trade_id"`
		Accept       bool   `json:"accept"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	recipUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	snap, err := h.tradeSvc.RespondTrade(r.Context(), recipUUID, req.FencingEpoch, req.TradeID, req.Accept)
	if err != nil {
		h.writeTradeErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalTradeOffer(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.tradeSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "trade module not attached")
		return
	}
	var req struct {
		CharacterID     string   `json:"character_id"`
		FencingEpoch    int64    `json:"fencing_epoch"`
		ServerID        string   `json:"server_id"`
		TradeID         string   `json:"trade_id"`
		CoinOffer       int64    `json:"coin_offer"`
		ItemInstanceIDs []string `json:"item_instance_ids"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	actorUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	parsedItems := make([]uuid.UUID, 0, len(req.ItemInstanceIDs))
	for _, s := range req.ItemInstanceIDs {
		id, err := uuid.Parse(strings.TrimSpace(s))
		if err != nil {
			writeErr(w, http.StatusBadRequest, "ERR_BAD_ITEM_ID", "invalid item_instance_id UUID in trade offer")
			return
		}
		parsedItems = append(parsedItems, id)
	}
	snap, err := h.tradeSvc.UpdateOffer(r.Context(), actorUUID, req.FencingEpoch, req.TradeID, req.CoinOffer, parsedItems)
	if err != nil {
		h.writeTradeErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalTradeLock(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.tradeSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "trade module not attached")
		return
	}
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
		TradeID      string `json:"trade_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	actorUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	snap, err := h.tradeSvc.LockOffer(r.Context(), actorUUID, req.FencingEpoch, req.TradeID)
	if err != nil {
		h.writeTradeErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalTradeConfirm(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.tradeSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "trade module not attached")
		return
	}
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
		TradeID      string `json:"trade_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	actorUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	snap, err := h.tradeSvc.ConfirmAndSettleTradeFenced(r.Context(), actorUUID, req.FencingEpoch, req.ServerID, req.TradeID)
	if err != nil {
		h.writeTradeErr(w, err)
		return
	}
	var invSnap *inventory.InventorySnapshot
	if h.inventory != nil {
		if loaded, invErr := h.inventory.GetInventorySnapshot(r.Context(), actorUUID); invErr == nil {
			invSnap = &loaded
		}
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"trade":     snap,
		"inventory": invSnap,
	})
}

func (h *Handler) handleInternalTradeCancel(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.tradeSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "trade module not attached")
		return
	}
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
		TradeID      string `json:"trade_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	actorUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	snap, err := h.tradeSvc.CancelTrade(actorUUID, req.TradeID)
	if err != nil {
		h.writeTradeErr(w, err)
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

// ============================================================================
// Step 5D: Multi-Channel Chat & Overhead Speech Bubble Router (/internal/v1/social/chat/*)
// ============================================================================

func (h *Handler) handleInternalChatSend(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.chatRouter == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "social chat module not attached")
		return
	}
	var req struct {
		CharacterID    string  `json:"character_id"`
		CharacterName  string  `json:"character_name"`
		FencingEpoch   int64   `json:"fencing_epoch"`
		ServerID       string  `json:"server_id"`
		Channel        string  `json:"channel"`
		Content        string  `json:"content"`
		TargetCharID   string  `json:"target_char_id"`
		TargetName     string  `json:"target_name"`
		ZoneID         string  `json:"zone_id"`
		PosX           float64 `json:"pos_x"`
		PosY           float64 `json:"pos_y"`
		PosZ           float64 `json:"pos_z"`
		SenderGuildTag string  `json:"sender_guild_tag"`
		GuildID        string  `json:"guild_id"`
		PartyID        string  `json:"party_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	senderUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}

	partyID := strings.TrimSpace(req.PartyID)
	if partyID == "" && h.partyCoord != nil {
		partyID = h.partyCoord.GetPartyID(senderUUID.String())
	}
	guildID := strings.TrimSpace(req.GuildID)
	guildTag := strings.TrimSpace(req.SenderGuildTag)
	if (guildID == "" || guildTag == "") && h.guildSvc != nil {
		if gb, err := h.guildSvc.LookupCharacterGuild(r.Context(), senderUUID); err == nil {
			if guildID == "" {
				guildID = gb.GuildID
			}
			if guildTag == "" {
				guildTag = gb.GuildTag
			}
		}
	}

	msg, err := h.chatRouter.SendMessage(social.SendChatInput{
		Channel:        req.Channel,
		SenderCharID:   senderUUID.String(),
		SenderName:     req.CharacterName,
		SenderGuildTag: guildTag,
		PartyID:        partyID,
		GuildID:        guildID,
		TargetCharID:   req.TargetCharID,
		TargetName:     req.TargetName,
		ZoneID:         req.ZoneID,
		PosX:           req.PosX,
		PosY:           req.PosY,
		PosZ:           req.PosZ,
		Content:        req.Content,
	})
	if err != nil {
		switch {
		case errors.Is(err, social.ErrChatRateLimited):
			writeErr(w, http.StatusTooManyRequests, "ERR_CHAT_RATE_LIMITED", err.Error())
		case errors.Is(err, social.ErrEmptyChatMessage), errors.Is(err, social.ErrInvalidChannel), errors.Is(err, social.ErrMissingWhisperTgt):
			writeErr(w, http.StatusBadRequest, "ERR_CHAT_INVALID", err.Error())
		case errors.Is(err, social.ErrNotInPartyForChat), errors.Is(err, social.ErrNotInGuildForChat):
			writeErr(w, http.StatusUnprocessableEntity, "ERR_CHAT_MEMBERSHIP", err.Error())
		default:
			writeErr(w, http.StatusInternalServerError, "ERR_CHAT_INTERNAL", err.Error())
		}
		return
	}
	writeJSON(w, http.StatusOK, msg)
}

func (h *Handler) handleInternalChatFetch(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.chatRouter == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP5_DISABLED", "social chat module not attached")
		return
	}
	var req struct {
		CharacterID   string  `json:"character_id"`
		CharacterName string  `json:"character_name"`
		FencingEpoch  int64   `json:"fencing_epoch"`
		ServerID      string  `json:"server_id"`
		AfterSeq      int64   `json:"after_seq"`
		ZoneID        string  `json:"zone_id"`
		PartyID       string  `json:"party_id"`
		GuildID       string  `json:"guild_id"`
		PosX          float64 `json:"pos_x"`
		PosY          float64 `json:"pos_y"`
		PosZ          float64 `json:"pos_z"`
		HasPosition   bool    `json:"has_position"`
		Limit         int     `json:"limit"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	viewerUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}

	partyID := strings.TrimSpace(req.PartyID)
	if partyID == "" && h.partyCoord != nil {
		partyID = h.partyCoord.GetPartyID(viewerUUID.String())
	}
	guildID := strings.TrimSpace(req.GuildID)
	if guildID == "" && h.guildSvc != nil {
		if gb, err := h.guildSvc.LookupCharacterGuild(r.Context(), viewerUUID); err == nil {
			guildID = gb.GuildID
		}
	}

	msgs, latestSeq := h.chatRouter.FetchMessages(social.FetchFilter{
		AfterSeq:      req.AfterSeq,
		CharacterID:   viewerUUID.String(),
		CharacterName: req.CharacterName,
		ZoneID:        req.ZoneID,
		PartyID:       partyID,
		GuildID:       guildID,
		PosX:          req.PosX,
		PosY:          req.PosY,
		PosZ:          req.PosZ,
		HasPosition:   req.HasPosition,
		Limit:         req.Limit,
	})
	writeJSON(w, http.StatusOK, map[string]any{
		"messages":   msgs,
		"latest_seq": latestSeq,
	})
}
