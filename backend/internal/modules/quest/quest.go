package quest

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"strings"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/modules/economy"
	"github.com/infinite-universe/backend/internal/modules/progression"
	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

var (
	ErrStaleFencingEpoch = errors.New("stale fencing_epoch rejected during quest operation")
	ErrQuestNotFound     = errors.New("unknown quest_id")
	ErrQuestNotReady     = errors.New("quest objectives are not yet ready to turn in")
)

type QuestDefinition struct {
	QuestID          string
	Title            string
	Description      string
	ObjectiveKey     string
	TargetCount      int
	RewardCoin       int64
	RewardExpAndPot  int64
	RewardTitanToken int
	Repeatable       bool
}

var canonicalQuests = []QuestDefinition{
	{
		QuestID:          "quest.nro_raider_patrol_01",
		Title:            "Tuan Tra Cao Nguyen: Diet 3 Quan Doan Raider",
		Description:      "Tieu diet 3 ten Raider do tham de bao ve Tram Trung Tam.",
		ObjectiveKey:     "kill_raider",
		TargetCount:      3,
		RewardCoin:       300,
		RewardExpAndPot:  500,
		RewardTitanToken: 1,
		Repeatable:       true,
	},
	{
		QuestID:          "quest.nro_titan_slayer_01",
		Title:            "Chinh Phuc Dinh Nui: Ha Guc World Boss Highland Titan",
		Description:      "Hop suc cung To Doi bay sang Khu Boss va ha guc Highland Titan 3 Giai doan.",
		ObjectiveKey:     "kill_boss_titan",
		TargetCount:      1,
		RewardCoin:       1000,
		RewardExpAndPot:  2000,
		RewardTitanToken: 3,
		Repeatable:       true,
	},
}

type objectiveProgressEntry struct {
	Current int `json:"current"`
	Target  int `json:"target"`
}

type QuestEntryView struct {
	QuestID          string `json:"quest_id"`
	Title            string `json:"title"`
	Description      string `json:"description"`
	QuestState       string `json:"quest_state"` // IN_PROGRESS | READY_TO_TURN_IN | COMPLETED
	CurrentCount     int    `json:"current_count"`
	TargetCount      int    `json:"target_count"`
	RepeatCount      int    `json:"repeat_count"`
	RewardCoin       int64  `json:"reward_coin"`
	RewardExpAndPot  int64  `json:"reward_exp_pot"`
	RewardTitanToken int    `json:"reward_titan_token"`
}

type QuestSnapshot struct {
	Quests        []QuestEntryView `json:"quests"`
	ActionMessage string           `json:"action_message,omitempty"`
}

type Service struct {
	pool   *pgxpool.Pool
	byID   map[string]QuestDefinition
}

func NewService(pool *pgxpool.Pool) *Service {
	m := make(map[string]QuestDefinition, len(canonicalQuests))
	for _, q := range canonicalQuests {
		m[q.QuestID] = q
	}
	return &Service{pool: pool, byID: m}
}

func (s *Service) ensureDefaults(ctx context.Context, charID uuid.UUID) error {
	for _, q := range canonicalQuests {
		initObj := map[string]objectiveProgressEntry{
			q.ObjectiveKey: {Current: 0, Target: q.TargetCount},
		}
		raw, _ := json.Marshal(initObj)
		if _, err := s.pool.Exec(ctx, `
			INSERT INTO character_quests (character_id, quest_id, quest_state, objectives_progress, repeat_count)
			VALUES ($1, $2, 'IN_PROGRESS', $3::jsonb, 0)
			ON CONFLICT (character_id, quest_id) DO NOTHING
		`, charID, q.QuestID, string(raw)); err != nil {
			return err
		}
	}
	return nil
}

// GetQuestSnapshot returns all canonical NRO quests and their live progress for characterID.
func (s *Service) GetQuestSnapshot(ctx context.Context, charID uuid.UUID, actionMsg string) (QuestSnapshot, error) {
	if err := s.ensureDefaults(ctx, charID); err != nil {
		return QuestSnapshot{}, err
	}

	rows, err := s.pool.Query(ctx, `
		SELECT quest_id, quest_state, objectives_progress, repeat_count
		  FROM character_quests
		 WHERE character_id = $1
	`, charID)
	if err != nil {
		return QuestSnapshot{}, err
	}
	defer rows.Close()

	type dbRow struct {
		state  string
		objRaw []byte
		rep    int
	}
	byQuest := make(map[string]dbRow)
	for rows.Next() {
		var qid, st string
		var raw []byte
		var rep int
		if err := rows.Scan(&qid, &st, &raw, &rep); err != nil {
			return QuestSnapshot{}, err
		}
		byQuest[qid] = dbRow{state: st, objRaw: raw, rep: rep}
	}

	out := QuestSnapshot{
		Quests:        make([]QuestEntryView, 0, len(canonicalQuests)),
		ActionMessage: actionMsg,
	}
	for _, def := range canonicalQuests {
		r, ok := byQuest[def.QuestID]
		cur := 0
		st := "IN_PROGRESS"
		rep := 0
		if ok {
			st = r.state
			rep = r.rep
			var objMap map[string]objectiveProgressEntry
			if json.Unmarshal(r.objRaw, &objMap) == nil {
				cur = objMap[def.ObjectiveKey].Current
			}
		}
		out.Quests = append(out.Quests, QuestEntryView{
			QuestID:          def.QuestID,
			Title:            def.Title,
			Description:      def.Description,
			QuestState:       st,
			CurrentCount:     cur,
			TargetCount:      def.TargetCount,
			RepeatCount:      rep,
			RewardCoin:       def.RewardCoin,
			RewardExpAndPot:  def.RewardExpAndPot,
			RewardTitanToken: def.RewardTitanToken,
		})
	}
	return out, nil
}

// RecordKillProgress advances IN_PROGRESS quest counters when a player defeats a monster or World Boss.
func (s *Service) RecordKillProgress(ctx context.Context, charID uuid.UUID, entityID string, count int) error {
	if count <= 0 || charID == uuid.Nil {
		return nil
	}
	if err := s.ensureDefaults(ctx, charID); err != nil {
		return err
	}

	isBoss := strings.HasPrefix(strings.ToLower(strings.TrimSpace(entityID)), "boss.")
	targetQuestID := "quest.nro_raider_patrol_01"
	if isBoss {
		targetQuestID = "quest.nro_titan_slayer_01"
	}
	def, ok := s.byID[targetQuestID]
	if !ok {
		return nil
	}

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var state string
	var objRaw []byte
	err = tx.QueryRow(ctx, `
		SELECT quest_state, objectives_progress
		  FROM character_quests
		 WHERE character_id = $1 AND quest_id = $2
		   FOR UPDATE
	`, charID, def.QuestID).Scan(&state, &objRaw)
	if err != nil {
		return err
	}
	if state != "IN_PROGRESS" {
		return nil
	}

	objMap := make(map[string]objectiveProgressEntry)
	_ = json.Unmarshal(objRaw, &objMap)
	cur := min(def.TargetCount, objMap[def.ObjectiveKey].Current+count)
	objMap[def.ObjectiveKey] = objectiveProgressEntry{Current: cur, Target: def.TargetCount}
	newState := "IN_PROGRESS"
	if cur >= def.TargetCount {
		newState = "READY_TO_TURN_IN"
	}
	newBytes, _ := json.Marshal(objMap)

	if _, err := tx.Exec(ctx, `
		UPDATE character_quests
		   SET quest_state = $3,
		       objectives_progress = $4::jsonb,
		       updated_at = NOW()
		 WHERE character_id = $1 AND quest_id = $2
	`, charID, def.QuestID, newState, string(newBytes)); err != nil {
		return err
	}
	return tx.Commit(ctx)
}

// ClaimQuestRewardFenced settles READY_TO_TURN_IN quest rewards (Vàng + SM/TN + Titan Pity Tokens) atomically.
func (s *Service) ClaimQuestRewardFenced(
	ctx context.Context,
	charID uuid.UUID,
	fencingEpoch int64,
	serverID string,
	questID string,
	idempotencyKey string,
) (QuestSnapshot, error) {
	if fencingEpoch < 1 {
		return QuestSnapshot{}, ErrStaleFencingEpoch
	}
	def, ok := s.byID[strings.TrimSpace(questID)]
	if !ok {
		return QuestSnapshot{}, ErrQuestNotFound
	}
	if strings.TrimSpace(serverID) == "" {
		serverID = "ue5-ded-7777"
	}
	cleanIdem := strings.TrimSpace(idempotencyKey)
	if cleanIdem == "" {
		cleanIdem = uuid.NewString()
	}

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return QuestSnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var lv int
	var curExp int64
	if err := tx.QueryRow(ctx, `
		SELECT level, current_exp
		  FROM characters
		 WHERE id = $1 AND fencing_epoch = $2 AND deleted_at IS NULL
		   FOR UPDATE
	`, charID, fencingEpoch).Scan(&lv, &curExp); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return QuestSnapshot{}, ErrStaleFencingEpoch
		}
		return QuestSnapshot{}, err
	}

	var state string
	var repCount int
	if err := tx.QueryRow(ctx, `
		SELECT quest_state, repeat_count
		  FROM character_quests
		 WHERE character_id = $1 AND quest_id = $2
		   FOR UPDATE
	`, charID, def.QuestID).Scan(&state, &repCount); err != nil {
		return QuestSnapshot{}, ErrQuestNotFound
	}
	if state != "READY_TO_TURN_IN" {
		return QuestSnapshot{}, ErrQuestNotReady
	}

	if def.RewardCoin > 0 {
		if _, err := economy.ApplyCoinDeltaTx(
			ctx, tx, charID, fencingEpoch, serverID,
			def.RewardCoin, "QUEST_REWARD", "QUEST", def.QuestID,
			fmt.Sprintf("quest:%s:%s:rep%d:%s", charID.String(), def.QuestID, repCount+1, cleanIdem),
		); err != nil {
			return QuestSnapshot{}, err
		}
	}

	if def.RewardExpAndPot > 0 {
		newLv, remExp, _ := progression.ApplyExpGain(lv, curExp, def.RewardExpAndPot)
		if _, err := tx.Exec(ctx, `
			UPDATE characters SET level = $2, current_exp = $3, last_saved_at = NOW() WHERE id = $1
		`, charID, newLv, remExp); err != nil {
			return QuestSnapshot{}, err
		}
		if _, err := tx.Exec(ctx, `
			UPDATE character_potential SET potential_exp = potential_exp + $2, updated_at = NOW() WHERE character_id = $1
		`, charID, def.RewardExpAndPot); err != nil {
			return QuestSnapshot{}, err
		}
	}

	if def.RewardTitanToken > 0 {
		if _, err := tx.Exec(ctx, `
			INSERT INTO boss_pity_tokens (character_id, token_id, amount, consecutive_dry_kills, lifetime_earned, lifetime_redeemed)
			VALUES ($1, 'token.boss_highland_titan', $2, 0, $2, 0)
			ON CONFLICT (character_id, token_id) DO UPDATE SET
				amount = boss_pity_tokens.amount + EXCLUDED.amount,
				lifetime_earned = boss_pity_tokens.lifetime_earned + EXCLUDED.lifetime_earned,
				updated_at = NOW()
		`, charID, def.RewardTitanToken); err != nil {
			return QuestSnapshot{}, err
		}
	}

	if _, err := tx.Exec(ctx, `
		UPDATE character_quests
		   SET quest_state = 'COMPLETED',
		       repeat_count = repeat_count + 1,
		       completed_at = NOW(),
		       updated_at = NOW()
		 WHERE character_id = $1 AND quest_id = $2
	`, charID, def.QuestID); err != nil {
		return QuestSnapshot{}, err
	}

	if err := tx.Commit(ctx); err != nil {
		return QuestSnapshot{}, err
	}

	return s.GetQuestSnapshot(ctx, charID, fmt.Sprintf("Hoan thanh nhiem vu! Nhan +%d Vang, +%d SM/TN, +%d Huy Chuong Titan!", def.RewardCoin, def.RewardExpAndPot, def.RewardTitanToken))
}

// AcceptRepeatQuestFenced resets a COMPLETED repeatable quest back to IN_PROGRESS (0/target).
func (s *Service) AcceptRepeatQuestFenced(
	ctx context.Context,
	charID uuid.UUID,
	fencingEpoch int64,
	questID string,
) (QuestSnapshot, error) {
	if fencingEpoch < 1 {
		return QuestSnapshot{}, ErrStaleFencingEpoch
	}
	def, ok := s.byID[strings.TrimSpace(questID)]
	if !ok || !def.Repeatable {
		return QuestSnapshot{}, ErrQuestNotFound
	}
	initObj := map[string]objectiveProgressEntry{
		def.ObjectiveKey: {Current: 0, Target: def.TargetCount},
	}
	raw, _ := json.Marshal(initObj)
	if _, err := s.pool.Exec(ctx, `
		UPDATE character_quests cq
		   SET quest_state = 'IN_PROGRESS',
		       objectives_progress = $3::jsonb,
		       accepted_at = NOW(),
		       updated_at = NOW()
		  FROM characters c
		 WHERE cq.character_id = c.id
		   AND c.id = $1
		   AND c.fencing_epoch = $2
		   AND cq.quest_id = $4
		   AND cq.quest_state = 'COMPLETED'
	`, charID, fencingEpoch, string(raw), def.QuestID); err != nil {
		return QuestSnapshot{}, err
	}
	return s.GetQuestSnapshot(ctx, charID, "Da nhan lai nhiem vu hang ngay!")
}
