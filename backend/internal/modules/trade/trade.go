package trade

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/modules/economy"
	"github.com/infinite-universe/backend/internal/modules/gamedata"
	"github.com/infinite-universe/backend/internal/modules/inventory"
	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

const (
	StatePendingInvite  = "PENDING_INVITE"
	StateOpen           = "OPEN"
	StateBothLocked     = "BOTH_LOCKED"
	StateSettled        = "SETTLED"
	StateCancelled      = "CANCELLED"
	StateAborted        = "ABORTED_CONFLICT"
	MaxItemsPerOffer    = 8
	DefaultInspectDelay = 3 * time.Second
	DefaultInviteTTL    = 60 * time.Second
	TradeTaxRateBP      = int64(500) // 5.00% = 500 / 10000
)

var (
	ErrSelfTrade                 = errors.New("cannot trade with yourself")
	ErrPlayerBusyInTrade         = errors.New("one of the characters is already in an active trade session")
	ErrTradeNotFound             = errors.New("trade session not found or has expired")
	ErrNotTradeParticipant       = errors.New("character is not a participant in this trade session")
	ErrInvalidTradeState         = errors.New("action is not allowed in the current trade session state")
	ErrTradeAlreadyBothLocked    = errors.New("both sides have locked offers; cancel trade if you wish to abort")
	ErrTradeNotBothLocked        = errors.New("both participants must lock their offers before confirming")
	ErrInspectionCountdownActive = errors.New("3.0s anti-scam inspection countdown is still active")
	ErrItemBoundUntradeable      = errors.New("cannot trade soulbound (is_bound=true) or equipped items")
	ErrItemLockedUntradeable     = errors.New("cannot trade player-locked (is_player_locked=true) items")
	ErrTooManyTradeItems         = errors.New("cannot offer more than 8 item slots in a single trade")
	ErrDuplicateOfferItem        = errors.New("duplicate item_instance_id in trade offer")
	ErrNegativeCoinOffer         = errors.New("coin offer cannot be negative")
	ErrStaleFencingEpoch         = errors.New("stale fencing_epoch or dead character rejected during trade")
	ErrTradeConflictAborted      = errors.New("trade aborted due to concurrent item/wallet mutation")
)

type TradeItemSnapshot struct {
	InstanceID   uuid.UUID               `json:"instance_id"`
	ItemDefID    string                  `json:"item_def_id"`
	DisplayName  string                  `json:"display_name"`
	Category     string                  `json:"category"`
	Rarity       string                  `json:"rarity"`
	StackCount   int                     `json:"stack_count"`
	EnhanceLevel int16                   `json:"enhance_level"`
	Affixes      []inventory.RolledAffix `json:"affixes,omitempty"`
}

type TradeSessionSnapshot struct {
	HasActiveTrade      bool                `json:"has_active_trade"`
	TradeID             string              `json:"trade_id,omitempty"`
	State               string              `json:"state,omitempty"`
	ZoneID              string              `json:"zone_id,omitempty"`
	InitiatorCharID     string              `json:"initiator_char_id,omitempty"`
	InitiatorName       string              `json:"initiator_name,omitempty"`
	RecipientCharID     string              `json:"recipient_char_id,omitempty"`
	RecipientName       string              `json:"recipient_name,omitempty"`
	InitiatorCoin       int64               `json:"initiator_coin"`
	RecipientCoin       int64               `json:"recipient_coin"`
	TaxCoinPreview      int64               `json:"tax_coin_preview"`
	InitiatorItems      []TradeItemSnapshot `json:"initiator_items"`
	RecipientItems      []TradeItemSnapshot `json:"recipient_items"`
	InitiatorLocked     bool                `json:"initiator_locked"`
	RecipientLocked     bool                `json:"recipient_locked"`
	InitiatorConfirmed  bool                `json:"initiator_confirmed"`
	RecipientConfirmed  bool                `json:"recipient_confirmed"`
	ConfirmReadyUnixMs  int64               `json:"confirm_ready_unix_ms"`
	RemainingInspectMs  int64               `json:"remaining_inspect_ms"`
	OfferVersion        int                 `json:"offer_version"`
	ActionMessage       string              `json:"action_message,omitempty"`
}

type sessionRecord struct {
	tradeID            uuid.UUID
	state              string
	zoneID             string
	initiatorID        uuid.UUID
	initiatorName      string
	initiatorEpoch     int64
	recipientID        uuid.UUID
	recipientName      string
	recipientEpoch     int64
	initiatorCoin      int64
	recipientCoin      int64
	initiatorItemIDs   []uuid.UUID
	recipientItemIDs   []uuid.UUID
	initiatorItemsView []TradeItemSnapshot
	recipientItemsView []TradeItemSnapshot
	initiatorLocked    bool
	recipientLocked    bool
	initiatorConfirmed bool
	recipientConfirmed bool
	bothLockedAt       time.Time
	settling           bool
	offerVersion       int
	createdAt          time.Time
	expiresAt          time.Time
}

type Service struct {
	pool            *pgxpool.Pool
	catalog         *gamedata.Catalog
	mu              sync.Mutex
	inspectionDelay time.Duration
	inviteTTL       time.Duration
	sessions        map[uuid.UUID]*sessionRecord
	charToTrade     map[uuid.UUID]uuid.UUID
	recentDone      map[uuid.UUID]*sessionRecord // retains settled/cancelled snapshot briefly for partner polling
}

func NewService(pool *pgxpool.Pool, catalog *gamedata.Catalog) *Service {
	return &Service{
		pool:            pool,
		catalog:         catalog,
		inspectionDelay: DefaultInspectDelay,
		inviteTTL:       DefaultInviteTTL,
		sessions:        make(map[uuid.UUID]*sessionRecord),
		charToTrade:     make(map[uuid.UUID]uuid.UUID),
		recentDone:      make(map[uuid.UUID]*sessionRecord),
	}
}

// SetInspectionDelay configures the lock-to-confirm anti-scam delay (default 3.0s).
func (s *Service) SetInspectionDelay(d time.Duration) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.inspectionDelay = d
}

// ComputeTradeTax returns Floor(coinGiven * 5%).
func ComputeTradeTax(coinGiven int64) int64 {
	if coinGiven <= 0 {
		return 0
	}
	return (coinGiven * TradeTaxRateBP) / 10000
}

func (s *Service) purgeExpiredLocked(now time.Time) {
	for id, sess := range s.sessions {
		if sess.state == StatePendingInvite && !now.Before(sess.expiresAt) {
			delete(s.charToTrade, sess.initiatorID)
			delete(s.charToTrade, sess.recipientID)
			delete(s.sessions, id)
		}
	}
	if len(s.recentDone) > 256 {
		clear(s.recentDone)
	}
}

func (s *Service) toSnapshotLocked(sess *sessionRecord, now time.Time, msg string) TradeSessionSnapshot {
	if sess == nil {
		return TradeSessionSnapshot{
			HasActiveTrade: false,
			InitiatorItems: []TradeItemSnapshot{},
			RecipientItems: []TradeItemSnapshot{},
			ActionMessage:  msg,
		}
	}
	active := (sess.state == StatePendingInvite || sess.state == StateOpen || sess.state == StateBothLocked)
	var readyMs, remMs int64
	if !sess.bothLockedAt.IsZero() {
		readyAt := sess.bothLockedAt.Add(s.inspectionDelay)
		readyMs = readyAt.UnixMilli()
		if readyAt.After(now) {
			remMs = readyAt.Sub(now).Milliseconds()
		}
	}
	initItems := sess.initiatorItemsView
	if initItems == nil {
		initItems = []TradeItemSnapshot{}
	}
	recipItems := sess.recipientItemsView
	if recipItems == nil {
		recipItems = []TradeItemSnapshot{}
	}
	return TradeSessionSnapshot{
		HasActiveTrade:     active,
		TradeID:            sess.tradeID.String(),
		State:              sess.state,
		ZoneID:             sess.zoneID,
		InitiatorCharID:    sess.initiatorID.String(),
		InitiatorName:      sess.initiatorName,
		RecipientCharID:    sess.recipientID.String(),
		RecipientName:      sess.recipientName,
		InitiatorCoin:      sess.initiatorCoin,
		RecipientCoin:      sess.recipientCoin,
		TaxCoinPreview:     ComputeTradeTax(sess.initiatorCoin) + ComputeTradeTax(sess.recipientCoin),
		InitiatorItems:     initItems,
		RecipientItems:     recipItems,
		InitiatorLocked:    sess.initiatorLocked,
		RecipientLocked:    sess.recipientLocked,
		InitiatorConfirmed: sess.initiatorConfirmed,
		RecipientConfirmed: sess.recipientConfirmed,
		ConfirmReadyUnixMs: readyMs,
		RemainingInspectMs: remMs,
		OfferVersion:       sess.offerVersion,
		ActionMessage:      msg,
	}
}

func (s *Service) GetActiveSession(charID uuid.UUID) TradeSessionSnapshot {
	now := time.Now()
	s.mu.Lock()
	defer s.mu.Unlock()
	s.purgeExpiredLocked(now)

	if tid, ok := s.charToTrade[charID]; ok {
		if sess, exists := s.sessions[tid]; exists {
			return s.toSnapshotLocked(sess, now, "")
		}
	}
	if prev, ok := s.recentDone[charID]; ok {
		delete(s.recentDone, charID)
		return s.toSnapshotLocked(prev, now, "")
	}
	return s.toSnapshotLocked(nil, now, "")
}

func (s *Service) lookupCharNameAndVerifyAlive(ctx context.Context, charID uuid.UUID, expectedEpoch int64) (string, error) {
	var name string
	var alive bool
	var dbEpoch int64
	err := s.pool.QueryRow(ctx, `
		SELECT character_name::text, is_alive, fencing_epoch
		  FROM characters
		 WHERE id = $1 AND deleted_at IS NULL
	`, charID).Scan(&name, &alive, &dbEpoch)
	if err != nil {
		return "", ErrStaleFencingEpoch
	}
	if !alive || (expectedEpoch > 0 && dbEpoch != expectedEpoch) {
		return "", ErrStaleFencingEpoch
	}
	return name, nil
}

func (s *Service) RequestTrade(
	ctx context.Context,
	initiatorID uuid.UUID,
	initiatorEpoch int64,
	recipientID uuid.UUID,
	zoneID string,
) (TradeSessionSnapshot, error) {
	if initiatorID == uuid.Nil || recipientID == uuid.Nil || initiatorID == recipientID {
		return TradeSessionSnapshot{}, ErrSelfTrade
	}
	initName, err := s.lookupCharNameAndVerifyAlive(ctx, initiatorID, initiatorEpoch)
	if err != nil {
		return TradeSessionSnapshot{}, err
	}
	recipName, err := s.lookupCharNameAndVerifyAlive(ctx, recipientID, 0)
	if err != nil {
		return TradeSessionSnapshot{}, err
	}
	zoneID = strings.TrimSpace(zoneID)
	if zoneID == "" {
		zoneID = "zone.p01_west_hub"
	}

	now := time.Now()
	s.mu.Lock()
	defer s.mu.Unlock()
	s.purgeExpiredLocked(now)

	if _, busy := s.charToTrade[initiatorID]; busy {
		return TradeSessionSnapshot{}, ErrPlayerBusyInTrade
	}
	if _, busy := s.charToTrade[recipientID]; busy {
		return TradeSessionSnapshot{}, ErrPlayerBusyInTrade
	}

	tid := uuid.New()
	sess := &sessionRecord{
		tradeID:            tid,
		state:              StatePendingInvite,
		zoneID:             zoneID,
		initiatorID:        initiatorID,
		initiatorName:      initName,
		initiatorEpoch:     initiatorEpoch,
		recipientID:        recipientID,
		recipientName:      recipName,
		initiatorItemsView: []TradeItemSnapshot{},
		recipientItemsView: []TradeItemSnapshot{},
		createdAt:          now,
		expiresAt:          now.Add(s.inviteTTL),
	}
	s.sessions[tid] = sess
	s.charToTrade[initiatorID] = tid
	s.charToTrade[recipientID] = tid
	delete(s.recentDone, initiatorID)
	delete(s.recentDone, recipientID)

	return s.toSnapshotLocked(sess, now, fmt.Sprintf("Da gui loi moi giao dich toi %s!", recipName)), nil
}

func (s *Service) RespondTrade(
	ctx context.Context,
	recipientID uuid.UUID,
	recipientEpoch int64,
	tradeIDStr string,
	accept bool,
) (TradeSessionSnapshot, error) {
	if _, err := s.lookupCharNameAndVerifyAlive(ctx, recipientID, recipientEpoch); err != nil {
		return TradeSessionSnapshot{}, err
	}

	now := time.Now()
	s.mu.Lock()
	defer s.mu.Unlock()
	s.purgeExpiredLocked(now)

	var tid uuid.UUID
	if strings.TrimSpace(tradeIDStr) != "" {
		parsed, err := uuid.Parse(strings.TrimSpace(tradeIDStr))
		if err != nil {
			return TradeSessionSnapshot{}, ErrTradeNotFound
		}
		tid = parsed
	} else {
		tid = s.charToTrade[recipientID]
	}

	sess, ok := s.sessions[tid]
	if !ok || sess.state != StatePendingInvite {
		return TradeSessionSnapshot{}, ErrTradeNotFound
	}
	if sess.recipientID != recipientID {
		return TradeSessionSnapshot{}, ErrNotTradeParticipant
	}

	if !accept {
		sess.state = StateCancelled
		delete(s.charToTrade, sess.initiatorID)
		delete(s.charToTrade, sess.recipientID)
		delete(s.sessions, tid)
		s.recentDone[sess.initiatorID] = sess
		return s.toSnapshotLocked(sess, now, "Da tu choi giao dich."), nil
	}

	sess.recipientEpoch = recipientEpoch
	sess.state = StateOpen
	return s.toSnapshotLocked(sess, now, "Da mo phien giao dich P2P!"), nil
}

func (s *Service) validateOfferAgainstDB(ctx context.Context, ownerID uuid.UUID, coinOffer int64, itemIDs []uuid.UUID) ([]TradeItemSnapshot, error) {
	if coinOffer < 0 {
		return nil, ErrNegativeCoinOffer
	}
	if len(itemIDs) > MaxItemsPerOffer {
		return nil, ErrTooManyTradeItems
	}
	seen := make(map[uuid.UUID]bool, len(itemIDs))
	for _, id := range itemIDs {
		if id == uuid.Nil || seen[id] {
			return nil, ErrDuplicateOfferItem
		}
		seen[id] = true
	}

	if coinOffer > 0 {
		var curCoin int64
		err := s.pool.QueryRow(ctx, `
			SELECT COALESCE(standard_coin, 0) FROM character_wallets WHERE character_id = $1
		`, ownerID).Scan(&curCoin)
		if err != nil && !errors.Is(err, pgx.ErrNoRows) {
			return nil, err
		}
		if curCoin < coinOffer {
			return nil, economy.ErrInsufficientCoins
		}
	}

	views := make([]TradeItemSnapshot, 0, len(itemIDs))
	for _, id := range itemIDs {
		var defID, rarity, bagType string
		var stack int
		var enh int16
		var affixRaw []byte
		var isBound, isLocked bool
		err := s.pool.QueryRow(ctx, `
			SELECT item_def_id, rarity, bag_type, stack_count, enhance_level, affix_data, is_bound, is_player_locked
			  FROM inventory_items
			 WHERE id = $1 AND character_id = $2
		`, id, ownerID).Scan(&defID, &rarity, &bagType, &stack, &enh, &affixRaw, &isBound, &isLocked)
		if err != nil {
			if errors.Is(err, pgx.ErrNoRows) {
				return nil, inventory.ErrItemNotFound
			}
			return nil, err
		}
		if bagType != "INVENTORY" || isBound {
			return nil, ErrItemBoundUntradeable
		}
		if isLocked {
			return nil, ErrItemLockedUntradeable
		}
		dispName := defID
		cat := "MATERIAL"
		if s.catalog != nil {
			if def, ok := s.catalog.Items[defID]; ok {
				dispName = def.DisplayName
				cat = gamedata.MapSQLItemCategory(def.ItemType)
				if !def.IsTradeable {
					return nil, ErrItemBoundUntradeable
				}
			}
		}
		var affixes []inventory.RolledAffix
		_ = json.Unmarshal(affixRaw, &affixes)
		views = append(views, TradeItemSnapshot{
			InstanceID:   id,
			ItemDefID:    defID,
			DisplayName:  dispName,
			Category:     cat,
			Rarity:       rarity,
			StackCount:   stack,
			EnhanceLevel: enh,
			Affixes:      affixes,
		})
	}
	return views, nil
}

// UpdateOffer updates the caller's coin + item offer.
// Anti-scam invariant: if the partner had already clicked Lock (while only 1 side was locked),
// BOTH locks reset immediately to false! Once BOTH sides are locked, offers are hard-frozen.
func (s *Service) UpdateOffer(
	ctx context.Context,
	actorID uuid.UUID,
	actorEpoch int64,
	tradeIDStr string,
	coinOffer int64,
	itemIDs []uuid.UUID,
) (TradeSessionSnapshot, error) {
	if _, err := s.lookupCharNameAndVerifyAlive(ctx, actorID, actorEpoch); err != nil {
		return TradeSessionSnapshot{}, err
	}
	views, err := s.validateOfferAgainstDB(ctx, actorID, coinOffer, itemIDs)
	if err != nil {
		return TradeSessionSnapshot{}, err
	}

	now := time.Now()
	s.mu.Lock()
	defer s.mu.Unlock()

	var tid uuid.UUID
	if strings.TrimSpace(tradeIDStr) != "" {
		tid, _ = uuid.Parse(strings.TrimSpace(tradeIDStr))
	} else {
		tid = s.charToTrade[actorID]
	}
	sess, ok := s.sessions[tid]
	if !ok {
		return TradeSessionSnapshot{}, ErrTradeNotFound
	}
	if sess.initiatorID != actorID && sess.recipientID != actorID {
		return TradeSessionSnapshot{}, ErrNotTradeParticipant
	}
	if sess.state == StateBothLocked || sess.settling {
		return TradeSessionSnapshot{}, ErrTradeAlreadyBothLocked
	}
	if sess.state != StateOpen {
		return TradeSessionSnapshot{}, ErrInvalidTradeState
	}

	copiedIDs := make([]uuid.UUID, len(itemIDs))
	copy(copiedIDs, itemIDs)

	if sess.initiatorID == actorID {
		sess.initiatorEpoch = actorEpoch
		sess.initiatorCoin = coinOffer
		sess.initiatorItemIDs = copiedIDs
		sess.initiatorItemsView = views
	} else {
		sess.recipientEpoch = actorEpoch
		sess.recipientCoin = coinOffer
		sess.recipientItemIDs = copiedIDs
		sess.recipientItemsView = views
	}

	// Anti-scam reset: ANY offer edit resets BOTH participants' Lock & Confirm flags!
	sess.initiatorLocked = false
	sess.recipientLocked = false
	sess.initiatorConfirmed = false
	sess.recipientConfirmed = false
	sess.bothLockedAt = time.Time{}
	sess.offerVersion++

	return s.toSnapshotLocked(sess, now, "Da cap nhat vat pham/Vang giao dich (reset trang thai Khoa)."), nil
}

func (s *Service) LockOffer(
	ctx context.Context,
	actorID uuid.UUID,
	actorEpoch int64,
	tradeIDStr string,
) (TradeSessionSnapshot, error) {
	if _, err := s.lookupCharNameAndVerifyAlive(ctx, actorID, actorEpoch); err != nil {
		return TradeSessionSnapshot{}, err
	}

	s.mu.Lock()
	var tid uuid.UUID
	if strings.TrimSpace(tradeIDStr) != "" {
		tid, _ = uuid.Parse(strings.TrimSpace(tradeIDStr))
	} else {
		tid = s.charToTrade[actorID]
	}
	sess, ok := s.sessions[tid]
	if !ok {
		s.mu.Unlock()
		return TradeSessionSnapshot{}, ErrTradeNotFound
	}
	if sess.initiatorID != actorID && sess.recipientID != actorID {
		s.mu.Unlock()
		return TradeSessionSnapshot{}, ErrNotTradeParticipant
	}
	if sess.state != StateOpen && sess.state != StateBothLocked {
		s.mu.Unlock()
		return TradeSessionSnapshot{}, ErrInvalidTradeState
	}
	var actorCoin int64
	var actorItems []uuid.UUID
	if sess.initiatorID == actorID {
		actorCoin = sess.initiatorCoin
		actorItems = append([]uuid.UUID(nil), sess.initiatorItemIDs...)
	} else {
		actorCoin = sess.recipientCoin
		actorItems = append([]uuid.UUID(nil), sess.recipientItemIDs...)
	}
	s.mu.Unlock()

	// Re-validate actor's offer right before locking!
	views, err := s.validateOfferAgainstDB(ctx, actorID, actorCoin, actorItems)
	if err != nil {
		return TradeSessionSnapshot{}, err
	}

	now := time.Now()
	s.mu.Lock()
	defer s.mu.Unlock()

	sess, ok = s.sessions[tid]
	if !ok {
		return TradeSessionSnapshot{}, ErrTradeNotFound
	}
	if sess.settling || (sess.state != StateOpen && sess.state != StateBothLocked) {
		return TradeSessionSnapshot{}, ErrInvalidTradeState
	}

	if sess.initiatorID == actorID {
		sess.initiatorEpoch = actorEpoch
		sess.initiatorItemsView = views
		sess.initiatorLocked = true
	} else {
		sess.recipientEpoch = actorEpoch
		sess.recipientItemsView = views
		sess.recipientLocked = true
	}

	msg := "Da khoa phan giao dich cua ban!"
	if sess.initiatorLocked && sess.recipientLocked && sess.state != StateBothLocked {
		sess.state = StateBothLocked
		sess.bothLockedAt = now
		msg = "Ca 2 ben da KHOA! Vui long kiem tra ky vat pham trong 3 giay truoc khi XAC NHAN."
	}
	return s.toSnapshotLocked(sess, now, msg), nil
}

func (s *Service) CancelTrade(actorID uuid.UUID, tradeIDStr string) (TradeSessionSnapshot, error) {
	now := time.Now()
	s.mu.Lock()
	defer s.mu.Unlock()

	var tid uuid.UUID
	if strings.TrimSpace(tradeIDStr) != "" {
		tid, _ = uuid.Parse(strings.TrimSpace(tradeIDStr))
	} else {
		tid = s.charToTrade[actorID]
	}
	sess, ok := s.sessions[tid]
	if !ok {
		return s.toSnapshotLocked(nil, now, "Khong co phien giao dich nao dang mo."), nil
	}
	if sess.initiatorID != actorID && sess.recipientID != actorID {
		return TradeSessionSnapshot{}, ErrNotTradeParticipant
	}
	if sess.settling {
		return TradeSessionSnapshot{}, ErrInvalidTradeState
	}

	sess.state = StateCancelled
	delete(s.charToTrade, sess.initiatorID)
	delete(s.charToTrade, sess.recipientID)
	delete(s.sessions, tid)

	otherID := sess.initiatorID
	if actorID == sess.initiatorID {
		otherID = sess.recipientID
	}
	s.recentDone[otherID] = sess

	// Persist audit record for cancelled trade if it had already reached OPEN/BOTH_LOCKED
	go func(copyRec sessionRecord) {
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		if copyRec.initiatorEpoch > 0 && copyRec.recipientEpoch > 0 {
			_, _ = s.pool.Exec(ctx, `
				INSERT INTO trade_transactions (
					trade_id, idempotency_key, initiator_char_id, recipient_char_id,
					trade_state, initiator_coin_given, recipient_coin_given, tax_coin_burned,
					zone_id, initiator_epoch, recipient_epoch
				) VALUES ($1, $2, $3, $4, 'CANCELLED', $5, $6, 0, $7, $8, $9)
				ON CONFLICT DO NOTHING
			`, copyRec.tradeID, "trade-cancel:"+copyRec.tradeID.String(),
				copyRec.initiatorID, copyRec.recipientID,
				copyRec.initiatorCoin, copyRec.recipientCoin,
				copyRec.zoneID, copyRec.initiatorEpoch, copyRec.recipientEpoch)
		}
	}(*sess)

	return s.toSnapshotLocked(sess, now, "Da huy giao dich."), nil
}

func (s *Service) CleanupOnDisconnect(charID uuid.UUID) {
	if charID == uuid.Nil {
		return
	}
	_, _ = s.CancelTrade(charID, "")
}

// ConfirmAndSettleTradeFenced marks actor's final confirmation after the 3.0s inspection window
// and executes the single ACID PostgreSQL 18 settlement once BOTH participants have confirmed.
func (s *Service) ConfirmAndSettleTradeFenced(
	ctx context.Context,
	actorID uuid.UUID,
	actorEpoch int64,
	serverID string,
	tradeIDStr string,
) (TradeSessionSnapshot, error) {
	now := time.Now()
	s.mu.Lock()
	var tid uuid.UUID
	if strings.TrimSpace(tradeIDStr) != "" {
		tid, _ = uuid.Parse(strings.TrimSpace(tradeIDStr))
	} else {
		tid = s.charToTrade[actorID]
	}
	sess, ok := s.sessions[tid]
	if !ok {
		if prev, doneOk := s.recentDone[actorID]; doneOk {
			delete(s.recentDone, actorID)
			snap := s.toSnapshotLocked(prev, now, "Giao dich da hoan tat!")
			s.mu.Unlock()
			return snap, nil
		}
		s.mu.Unlock()
		return TradeSessionSnapshot{}, ErrTradeNotFound
	}
	if sess.initiatorID != actorID && sess.recipientID != actorID {
		s.mu.Unlock()
		return TradeSessionSnapshot{}, ErrNotTradeParticipant
	}
	if sess.state != StateBothLocked {
		s.mu.Unlock()
		return TradeSessionSnapshot{}, ErrTradeNotBothLocked
	}
	if now.Sub(sess.bothLockedAt) < s.inspectionDelay {
		s.mu.Unlock()
		return TradeSessionSnapshot{}, ErrInspectionCountdownActive
	}
	if sess.settling {
		snap := s.toSnapshotLocked(sess, now, "Dang xu ly giao dich...")
		s.mu.Unlock()
		return snap, nil
	}

	if sess.initiatorID == actorID {
		sess.initiatorEpoch = actorEpoch
		sess.initiatorConfirmed = true
	} else {
		sess.recipientEpoch = actorEpoch
		sess.recipientConfirmed = true
	}

	if !sess.initiatorConfirmed || !sess.recipientConfirmed {
		snap := s.toSnapshotLocked(sess, now, "Da xac nhan! Dang cho doi phuong xac nhan...")
		s.mu.Unlock()
		return snap, nil
	}

	// Both sides confirmed -> claim exclusive settlement ownership!
	sess.settling = true
	sessCopy := *sess
	sessCopy.initiatorItemIDs = append([]uuid.UUID(nil), sess.initiatorItemIDs...)
	sessCopy.recipientItemIDs = append([]uuid.UUID(nil), sess.recipientItemIDs...)
	sessCopy.initiatorItemsView = append([]TradeItemSnapshot(nil), sess.initiatorItemsView...)
	sessCopy.recipientItemsView = append([]TradeItemSnapshot(nil), sess.recipientItemsView...)
	s.mu.Unlock()

	err := s.settleInPostgres(ctx, &sessCopy, serverID)

	finishNow := time.Now()
	s.mu.Lock()
	defer s.mu.Unlock()

	delete(s.charToTrade, sess.initiatorID)
	delete(s.charToTrade, sess.recipientID)
	delete(s.sessions, tid)

	otherID := sess.initiatorID
	if actorID == sess.initiatorID {
		otherID = sess.recipientID
	}

	if err != nil {
		sess.state = StateAborted
		sess.settling = false
		s.recentDone[otherID] = sess
		return TradeSessionSnapshot{}, err
	}

	sess.state = StateSettled
	sess.settling = false
	s.recentDone[otherID] = sess
	return s.toSnapshotLocked(sess, finishNow, "Giao dich thanh cong!"), nil
}

func (s *Service) settleInPostgres(ctx context.Context, sess *sessionRecord, serverID string) error {
	if strings.TrimSpace(serverID) == "" {
		serverID = "ue5-ded-7777"
	}

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	// 1. Deterministic lexicographical UUID lock order (prevents deadlocks under 50-goroutine cross-trades!)
	firstID, firstEpoch := sess.initiatorID, sess.initiatorEpoch
	secondID, secondEpoch := sess.recipientID, sess.recipientEpoch
	if strings.Compare(firstID.String(), secondID.String()) > 0 {
		firstID, firstEpoch, secondID, secondEpoch = secondID, secondEpoch, firstID, firstEpoch
	}

	for _, pair := range []struct {
		id    uuid.UUID
		epoch int64
	}{
		{firstID, firstEpoch},
		{secondID, secondEpoch},
	} {
		if pair.epoch < 1 {
			return ErrStaleFencingEpoch
		}
		var lockedID uuid.UUID
		if err := tx.QueryRow(ctx, `
			SELECT id
			  FROM characters
			 WHERE id = $1 AND fencing_epoch = $2 AND is_alive = TRUE AND deleted_at IS NULL
			   FOR UPDATE
		`, pair.id, pair.epoch).Scan(&lockedID); err != nil {
			if errors.Is(err, pgx.ErrNoRows) {
				return ErrStaleFencingEpoch
			}
			return err
		}
	}

	// 2. Lock wallets in deterministic UUID order
	for _, cid := range []uuid.UUID{firstID, secondID} {
		if err := economy.EnsureWalletTx(ctx, tx, cid); err != nil {
			return err
		}
		var bal int64
		if err := tx.QueryRow(ctx, `
			SELECT standard_coin FROM character_wallets WHERE character_id = $1 FOR UPDATE
		`, cid).Scan(&bal); err != nil {
			return err
		}
	}

	// 3. Lock all INVENTORY rows for both characters in deterministic (character_id, item_id) order
	type bagRow struct {
		id       uuid.UUID
		slot     int
		isBound  bool
		isLocked bool
	}
	loadLockedBag := func(cid uuid.UUID) (map[uuid.UUID]bagRow, map[int]bool, error) {
		rows, err := tx.Query(ctx, `
			SELECT id, COALESCE(bag_slot_index, 0), is_bound, is_player_locked
			  FROM inventory_items
			 WHERE character_id = $1 AND bag_type = 'INVENTORY'
			 ORDER BY id ASC
			   FOR UPDATE
		`, cid)
		if err != nil {
			return nil, nil, err
		}
		defer rows.Close()
		byID := make(map[uuid.UUID]bagRow)
		occ := make(map[int]bool)
		for rows.Next() {
			var r bagRow
			if err := rows.Scan(&r.id, &r.slot, &r.isBound, &r.isLocked); err != nil {
				return nil, nil, err
			}
			byID[r.id] = r
			occ[r.slot] = true
		}
		return byID, occ, rows.Err()
	}

	var initBag, recipBag map[uuid.UUID]bagRow
	var initOcc, recipOcc map[int]bool
	if firstID == sess.initiatorID {
		initBag, initOcc, err = loadLockedBag(sess.initiatorID)
		if err != nil {
			return err
		}
		recipBag, recipOcc, err = loadLockedBag(sess.recipientID)
		if err != nil {
			return err
		}
	} else {
		recipBag, recipOcc, err = loadLockedBag(sess.recipientID)
		if err != nil {
			return err
		}
		initBag, initOcc, err = loadLockedBag(sess.initiatorID)
		if err != nil {
			return err
		}
	}

	// 4. Re-verify every offered item is still in INVENTORY, unbound, and unlocked!
	for _, iid := range sess.initiatorItemIDs {
		r, ok := initBag[iid]
		if !ok {
			return ErrTradeConflictAborted
		}
		if r.isBound {
			return ErrItemBoundUntradeable
		}
		if r.isLocked {
			return ErrItemLockedUntradeable
		}
		delete(initOcc, r.slot)
	}
	for _, iid := range sess.recipientItemIDs {
		r, ok := recipBag[iid]
		if !ok {
			return ErrTradeConflictAborted
		}
		if r.isBound {
			return ErrItemBoundUntradeable
		}
		if r.isLocked {
			return ErrItemLockedUntradeable
		}
		delete(recipOcc, r.slot)
	}

	// 5. Verify 48-slot bag capacity on both characters after item exchange
	if len(initOcc)+len(sess.recipientItemIDs) > inventory.MaxBagSlots {
		return inventory.ErrBagFull
	}
	if len(recipOcc)+len(sess.initiatorItemIDs) > inventory.MaxBagSlots {
		return inventory.ErrBagFull
	}

	// 6. Transfer items atomically: clear bag_slot_index first, then assign lowest free slots [0..47]
	allMoving := make([]uuid.UUID, 0, len(sess.initiatorItemIDs)+len(sess.recipientItemIDs))
	allMoving = append(allMoving, sess.initiatorItemIDs...)
	allMoving = append(allMoving, sess.recipientItemIDs...)
	sort.Slice(allMoving, func(i, j int) bool {
		return strings.Compare(allMoving[i].String(), allMoving[j].String()) < 0
	})
	for _, mid := range allMoving {
		if _, err := tx.Exec(ctx, `UPDATE inventory_items SET bag_slot_index = NULL WHERE id = $1`, mid); err != nil {
			return err
		}
	}

	assignNextFreeSlot := func(occ map[int]bool) (int, error) {
		for s := 0; s < inventory.MaxBagSlots; s++ {
			if !occ[s] {
				occ[s] = true
				return s, nil
			}
		}
		return -1, inventory.ErrBagFull
	}

	for _, iid := range sess.initiatorItemIDs {
		slot, err := assignNextFreeSlot(recipOcc)
		if err != nil {
			return err
		}
		if _, err := tx.Exec(ctx, `
			UPDATE inventory_items
			   SET character_id = $2, bag_slot_index = $3, lock_reason = NULL, updated_at = NOW()
			 WHERE id = $1
		`, iid, sess.recipientID, slot); err != nil {
			return err
		}
	}
	for _, iid := range sess.recipientItemIDs {
		slot, err := assignNextFreeSlot(initOcc)
		if err != nil {
			return err
		}
		if _, err := tx.Exec(ctx, `
			UPDATE inventory_items
			   SET character_id = $2, bag_slot_index = $3, lock_reason = NULL, updated_at = NOW()
			 WHERE id = $1
		`, iid, sess.initiatorID, slot); err != nil {
			return err
		}
	}

	// 7. Settle Vàng with 5% Tax Burn while preserving 100% double-entry currency_ledger parity!
	tradeRef := sess.tradeID.String()
	taxInit := ComputeTradeTax(sess.initiatorCoin)
	netToRecip := sess.initiatorCoin - taxInit

	taxRecip := ComputeTradeTax(sess.recipientCoin)
	netToInit := sess.recipientCoin - taxRecip

	if netToRecip > 0 {
		if _, err := economy.ApplyCoinDeltaTx(
			ctx, tx, sess.initiatorID, sess.initiatorEpoch, serverID,
			-netToRecip, "PLAYER_TRADE", "TRADE", tradeRef, "trade:"+tradeRef+":init_give",
		); err != nil {
			return err
		}
	}
	if taxInit > 0 {
		if _, err := economy.ApplyCoinDeltaTx(
			ctx, tx, sess.initiatorID, sess.initiatorEpoch, serverID,
			-taxInit, "TRADE_TAX", "TRADE", tradeRef, "trade:"+tradeRef+":init_tax",
		); err != nil {
			return err
		}
	}
	if netToRecip > 0 {
		if _, err := economy.ApplyCoinDeltaTx(
			ctx, tx, sess.recipientID, sess.recipientEpoch, serverID,
			netToRecip, "PLAYER_TRADE", "TRADE", tradeRef, "trade:"+tradeRef+":recip_recv",
		); err != nil {
			return err
		}
	}

	if netToInit > 0 {
		if _, err := economy.ApplyCoinDeltaTx(
			ctx, tx, sess.recipientID, sess.recipientEpoch, serverID,
			-netToInit, "PLAYER_TRADE", "TRADE", tradeRef, "trade:"+tradeRef+":recip_give",
		); err != nil {
			return err
		}
	}
	if taxRecip > 0 {
		if _, err := economy.ApplyCoinDeltaTx(
			ctx, tx, sess.recipientID, sess.recipientEpoch, serverID,
			-taxRecip, "TRADE_TAX", "TRADE", tradeRef, "trade:"+tradeRef+":recip_tax",
		); err != nil {
			return err
		}
	}
	if netToInit > 0 {
		if _, err := economy.ApplyCoinDeltaTx(
			ctx, tx, sess.initiatorID, sess.initiatorEpoch, serverID,
			netToInit, "PLAYER_TRADE", "TRADE", tradeRef, "trade:"+tradeRef+":init_recv",
		); err != nil {
			return err
		}
	}

	// 8. Insert immutable audit record into trade_transactions (SETTLED)
	initViews := sess.initiatorItemsView
	if initViews == nil {
		initViews = []TradeItemSnapshot{}
	}
	recipViews := sess.recipientItemsView
	if recipViews == nil {
		recipViews = []TradeItemSnapshot{}
	}
	initItemsJSON, _ := json.Marshal(initViews)
	recipItemsJSON, _ := json.Marshal(recipViews)
	totalTax := taxInit + taxRecip

	if _, err := tx.Exec(ctx, `
		INSERT INTO trade_transactions (
			trade_id, idempotency_key, initiator_char_id, recipient_char_id,
			trade_state, initiator_coin_given, recipient_coin_given, tax_coin_burned,
			initiator_items_given, recipient_items_given, zone_id,
			initiator_epoch, recipient_epoch, settled_at
		) VALUES (
			$1, $2, $3, $4,
			'SETTLED', $5, $6, $7,
			$8::jsonb, $9::jsonb, $10,
			$11, $12, NOW()
		)
	`, sess.tradeID, "trade-settle:"+tradeRef, sess.initiatorID, sess.recipientID,
		sess.initiatorCoin, sess.recipientCoin, totalTax,
		string(initItemsJSON), string(recipItemsJSON), sess.zoneID,
		sess.initiatorEpoch, sess.recipientEpoch); err != nil {
		return fmt.Errorf("insert trade_transactions: %w", err)
	}

	return tx.Commit(ctx)
}
