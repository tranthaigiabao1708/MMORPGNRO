package httpapi

import (
	"encoding/json"
	"errors"
	"net/http"
	"strings"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/modules/boss"
	"github.com/infinite-universe/backend/internal/modules/character"
	"github.com/infinite-universe/backend/internal/modules/economy"
	"github.com/infinite-universe/backend/internal/modules/inventory"
	"github.com/infinite-universe/backend/internal/modules/quest"
	"github.com/infinite-universe/backend/internal/modules/social"
	"github.com/infinite-universe/backend/internal/modules/world"
)

// ============================================================================
// Step 6A: Multi-Zone & Multi-Channel Coordinator Handlers (/internal/v1/world/*)
// ============================================================================

func (h *Handler) handleInternalWorldHeartbeat(w http.ResponseWriter, r *http.Request, body []byte) {
	var req world.ServerNodeInfo
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	node := h.worldCoord.UpsertServerHeartbeat(req)
	writeJSON(w, http.StatusOK, node)
}

func (h *Handler) handleInternalWorldChannels(w http.ResponseWriter, r *http.Request, body []byte) {
	channels := h.worldCoord.ListActiveChannels()
	writeJSON(w, http.StatusOK, map[string]any{
		"channels": channels,
	})
}

func (h *Handler) handleInternalWorldTransfer(w http.ResponseWriter, r *http.Request, body []byte) {
	var req struct {
		Checkpoint      character.SaveCheckpointInput `json:"checkpoint"`
		TargetZoneID    string                        `json:"target_zone_id"`
		TargetChannelID int                           `json:"target_channel_id"`
		DestX           float64                       `json:"dest_x"`
		DestY           float64                       `json:"dest_y"`
		DestZ           float64                       `json:"dest_z"`
		DestYaw         float32                       `json:"dest_yaw"`
		HasCustomDest   bool                          `json:"has_custom_dest"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	if req.Checkpoint.CharacterID == uuid.Nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_CHARACTER_ID", "checkpoint.character_id is required")
		return
	}

	if err := h.worldCoord.ValidateActiveLease(r.Context(), req.Checkpoint.CharacterID.String(), req.Checkpoint.ServerID, req.Checkpoint.FencingEpoch); err != nil {
		writeErr(w, http.StatusConflict, "ERR_LEASE_REVOKED", err.Error())
		return
	}

	// Refuse transfer if character currently has an active P2P trade open!
	if h.tradeSvc != nil {
		ts := h.tradeSvc.GetActiveSession(req.Checkpoint.CharacterID)
		if ts.HasActiveTrade && ts.State != "SETTLED" && ts.State != "CANCELLED" {
			writeErr(w, http.StatusConflict, "ERR_IN_ACTIVE_TRADE", "cannot switch zone/channel while in an active P2P trade")
			return
		}
	}

	destX, destY, destZ, destYaw := req.DestX, req.DestY, req.DestZ, req.DestYaw
	if !req.HasCustomDest {
		// Default safe landing pad per zone
		if strings.TrimSpace(req.TargetZoneID) == "zone.p01_highland_01" {
			destX, destY, destZ, destYaw = -420.0, 0.0, 118.0, 0.0
		} else {
			destX, destY, destZ, destYaw = 0.0, 0.0, 118.0, 0.0
		}
	}

	saved, err := h.characters.PrepareZoneTransferFenced(
		r.Context(),
		req.Checkpoint,
		req.TargetZoneID,
		req.TargetChannelID,
		destX, destY, destZ, destYaw,
	)
	if err != nil {
		switch {
		case errors.Is(err, character.ErrDeadCannotTransfer):
			writeErr(w, http.StatusUnprocessableEntity, "ERR_DEAD_CANNOT_TRANSFER", err.Error())
		case errors.Is(err, character.ErrStaleFencingEpoch):
			writeErr(w, http.StatusConflict, "ERR_STALE_FENCING_EPOCH", err.Error())
		default:
			writeErr(w, http.StatusInternalServerError, "ERR_TRANSFER_SAVE", err.Error())
		}
		return
	}

	ticket, err := h.worldCoord.IssueZoneTransferTicket(
		r.Context(),
		saved.AccountID,
		saved.ID,
		req.Checkpoint.ServerID,
		req.Checkpoint.FencingEpoch,
		req.TargetZoneID,
		req.TargetChannelID,
	)
	if err != nil {
		switch {
		case errors.Is(err, world.ErrChannelHardCapFull):
			writeErr(w, http.StatusUnprocessableEntity, "ERR_CHANNEL_FULL", err.Error())
		case errors.Is(err, world.ErrTargetChannelNotFound):
			writeErr(w, http.StatusNotFound, "ERR_CHANNEL_OFFLINE", err.Error())
		case errors.Is(err, world.ErrLeaseNotOwned):
			writeErr(w, http.StatusConflict, "ERR_LEASE_REVOKED", err.Error())
		default:
			writeErr(w, http.StatusInternalServerError, "ERR_TRANSFER_TICKET", err.Error())
		}
		return
	}

	writeJSON(w, http.StatusOK, map[string]any{
		"ticket_id":         ticket.TicketID,
		"character_id":      ticket.CharacterID,
		"target_zone_id":    ticket.TargetZoneID,
		"target_channel_id": ticket.TargetChannelID,
		"server_addr":       ticket.ServerAddr,
		"expires_unix":      ticket.ExpiresUnix,
	})
}

// ============================================================================
// Step 6B: 3-Phase World Boss & Pity Shop Handlers (/internal/v1/boss/*)
// ============================================================================

func (h *Handler) handleInternalSettleBossKill(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.bossSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP6_DISABLED", "boss module not attached")
		return
	}
	var req boss.SettleBossKillInput
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}

	res, err := h.bossSvc.SettleBossKillFenced(r.Context(), req)
	if err != nil {
		if errors.Is(err, boss.ErrInvalidBossSettlement) {
			writeErr(w, http.StatusBadRequest, "ERR_INVALID_BOSS_SETTLE", err.Error())
			return
		}
		writeErr(w, http.StatusInternalServerError, "ERR_BOSS_SETTLE", err.Error())
		return
	}

	if !res.Idempotent {
		// Advance Titan Slayer quest progress for all eligible contributors!
		if h.questSvc != nil {
			for _, rw := range res.Rewards {
				if rw.Eligible {
					if cid, pErr := uuid.Parse(rw.CharacterID); pErr == nil {
						_ = h.questSvc.RecordKillProgress(r.Context(), cid, res.BossID, 1)
					}
				}
			}
		}
		// Broadcast server-wide [HE THONG] World Boss kill announcement!
		if h.chatRouter != nil && res.Announcement != "" {
			_, _ = h.chatRouter.SendMessage(social.SendChatInput{
				Channel:    social.ChannelSystem,
				SenderName: "HE THONG",
				ZoneID:     res.ZoneID,
				Content:    res.Announcement,
			})
		}
	}

	writeJSON(w, http.StatusOK, res)
}

func (h *Handler) handleInternalPityShopGet(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.bossSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP6_DISABLED", "boss module not attached")
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
	snap, err := h.bossSvc.GetPityShopSnapshot(r.Context(), charUUID, "")
	if err != nil {
		writeErr(w, http.StatusInternalServerError, "ERR_PITY_GET", err.Error())
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalPityShopRedeem(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.bossSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP6_DISABLED", "boss module not attached")
		return
	}
	var req struct {
		CharacterID    string `json:"character_id"`
		FencingEpoch   int64  `json:"fencing_epoch"`
		ServerID       string `json:"server_id"`
		EntryID        string `json:"entry_id"`
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

	snap, err := h.bossSvc.RedeemPityShopEntryFenced(r.Context(), charUUID, req.FencingEpoch, req.ServerID, req.EntryID, req.IdempotencyKey)
	if err != nil {
		switch {
		case errors.Is(err, boss.ErrStaleFencingEpoch):
			writeErr(w, http.StatusConflict, "ERR_STALE_FENCING_EPOCH", err.Error())
		case errors.Is(err, boss.ErrPityEntryNotFound):
			writeErr(w, http.StatusNotFound, "ERR_PITY_ENTRY_NOT_FOUND", err.Error())
		case errors.Is(err, boss.ErrInsufficientPityToken):
			writeErr(w, http.StatusUnprocessableEntity, "ERR_INSUFFICIENT_PITY_TOKENS", err.Error())
		case errors.Is(err, boss.ErrPityDailyLimitReached):
			writeErr(w, http.StatusUnprocessableEntity, "ERR_PITY_DAILY_LIMIT", err.Error())
		case errors.Is(err, economy.ErrInsufficientCoins):
			writeErr(w, http.StatusUnprocessableEntity, "ERR_INSUFFICIENT_COINS", err.Error())
		case errors.Is(err, inventory.ErrBagFull):
			writeErr(w, http.StatusUnprocessableEntity, "ERR_BAG_FULL", err.Error())
		default:
			writeErr(w, http.StatusInternalServerError, "ERR_PITY_REDEEM", err.Error())
		}
		return
	}

	var invSnap any
	if h.inventory != nil {
		if is, iErr := h.inventory.GetInventorySnapshot(r.Context(), charUUID); iErr == nil {
			invSnap = is
		}
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"pity_shop": snap,
		"inventory": invSnap,
	})
}

// ============================================================================
// Step 6C: NRO Quest Progress & Turn-In Handlers (/internal/v1/quest/*)
// ============================================================================

func (h *Handler) handleInternalQuestGet(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.questSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP6_DISABLED", "quest module not attached")
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
	snap, err := h.questSvc.GetQuestSnapshot(r.Context(), charUUID, "")
	if err != nil {
		writeErr(w, http.StatusInternalServerError, "ERR_QUEST_GET", err.Error())
		return
	}
	writeJSON(w, http.StatusOK, snap)
}

func (h *Handler) handleInternalQuestClaim(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.questSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP6_DISABLED", "quest module not attached")
		return
	}
	var req struct {
		CharacterID    string `json:"character_id"`
		FencingEpoch   int64  `json:"fencing_epoch"`
		ServerID       string `json:"server_id"`
		QuestID        string `json:"quest_id"`
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
	snap, err := h.questSvc.ClaimQuestRewardFenced(r.Context(), charUUID, req.FencingEpoch, req.ServerID, req.QuestID, req.IdempotencyKey)
	if err != nil {
		switch {
		case errors.Is(err, quest.ErrStaleFencingEpoch):
			writeErr(w, http.StatusConflict, "ERR_STALE_FENCING_EPOCH", err.Error())
		case errors.Is(err, quest.ErrQuestNotFound):
			writeErr(w, http.StatusNotFound, "ERR_QUEST_NOT_FOUND", err.Error())
		case errors.Is(err, quest.ErrQuestNotReady):
			writeErr(w, http.StatusUnprocessableEntity, "ERR_QUEST_NOT_READY", err.Error())
		default:
			writeErr(w, http.StatusInternalServerError, "ERR_QUEST_CLAIM", err.Error())
		}
		return
	}

	var invSnap any
	if h.inventory != nil {
		if is, iErr := h.inventory.GetInventorySnapshot(r.Context(), charUUID); iErr == nil {
			invSnap = is
		}
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"quests":    snap,
		"inventory": invSnap,
	})
}

func (h *Handler) handleInternalQuestRepeat(w http.ResponseWriter, r *http.Request, body []byte) {
	if h.questSvc == nil {
		writeErr(w, http.StatusServiceUnavailable, "ERR_STEP6_DISABLED", "quest module not attached")
		return
	}
	var req struct {
		CharacterID  string `json:"character_id"`
		FencingEpoch int64  `json:"fencing_epoch"`
		ServerID     string `json:"server_id"`
		QuestID      string `json:"quest_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_BAD_JSON", err.Error())
		return
	}
	charUUID, ok := h.validateLeaseOnly(w, r, req.CharacterID, req.ServerID, req.FencingEpoch)
	if !ok {
		return
	}
	snap, err := h.questSvc.AcceptRepeatQuestFenced(r.Context(), charUUID, req.FencingEpoch, req.QuestID)
	if err != nil {
		writeErr(w, http.StatusBadRequest, "ERR_QUEST_REPEAT", err.Error())
		return
	}
	writeJSON(w, http.StatusOK, snap)
}
