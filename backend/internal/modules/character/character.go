package character

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"sync"
	"time"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/modules/progression"
	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgconn"
	"github.com/jackc/pgx/v5/pgxpool"
)

var (
	ErrCharacterNameTaken   = errors.New("character name is already taken")
	ErrCharacterSlotLimit   = errors.New("maximum character slot limit reached for account")
	ErrInvalidCharacterSpec = errors.New("invalid character name or race_id")
	ErrCharacterNotFound    = errors.New("character not found")
	ErrNotCharacterOwner    = errors.New("character does not belong to authenticated account")
	ErrStaleFencingEpoch    = errors.New("stale fencing_epoch rejected by PostgreSQL lease fence")
)

var validRaces = map[string]bool{
	"race.zenith_warrior":  true,
	"race.terran_adept":    true,
	"race.mystic_elder":    true,
	"race.frost_archon":    true,
	"race.chaos_morph":     true,
	"race.cyber_construct": true,
}

// CombatCounters is persisted inside appearance_data JSONB & character_save_snapshots without altering SQL DDL.
type CombatCounters struct {
	EnemyKills       int32 `json:"enemy_kills"`
	PvPKills         int32 `json:"pvp_kills"`
	Deaths           int32 `json:"deaths"`
	HasSavedPosition bool  `json:"has_saved_pos"`
}

// CharacterState represents the complete authoritative persistent state of a character.
type CharacterState struct {
	ID                    uuid.UUID `json:"character_id"`
	AccountID             uuid.UUID `json:"account_id"`
	CharacterName         string    `json:"character_name"`
	RaceID                string    `json:"race_id"`
	Level                 int       `json:"level"`
	CurrentExp            int64     `json:"current_exp"`
	NextLevelExp          int64     `json:"next_level_exp"`
	ZoneID                string    `json:"zone_id"`
	PosX                  float64   `json:"pos_x"`
	PosY                  float64   `json:"pos_y"`
	PosZ                  float64   `json:"pos_z"`
	RotYaw                float32   `json:"rot_yaw"`
	HasSavedPosition      bool      `json:"has_saved_pos"`
	IsAlive               bool      `json:"is_alive"`
	HP                    float64   `json:"hp"`
	MaxHP                 float64   `json:"max_hp"`
	Energy                float64   `json:"energy"`
	MaxEnergy             float64   `json:"max_energy"`
	AttackPowerMultiplier float64   `json:"attack_power_multiplier"`
	EnemyKills            int32     `json:"enemy_kills"`
	PvPKills              int32     `json:"pvp_kills"`
	Deaths                int32     `json:"deaths"`
	FencingEpoch          int64     `json:"fencing_epoch"`
	UpdatedAt             time.Time `json:"updated_at"`
}

// SaveCheckpointInput carries state flushed from an authoritative UE5 Dedicated Server.
type SaveCheckpointInput struct {
	CharacterID    uuid.UUID `json:"character_id"`
	FencingEpoch   int64     `json:"fencing_epoch"`
	ServerID       string    `json:"server_id"`
	SnapshotReason string    `json:"snapshot_reason"` // PERIODIC_60S | LOGOUT | LEVEL_UP | ZONE_TRANSFER
	ZoneID         string    `json:"zone_id"`
	PosX           float64   `json:"pos_x"`
	PosY           float64   `json:"pos_y"`
	PosZ           float64   `json:"pos_z"`
	RotYaw         float32   `json:"rot_yaw"`
	IsAlive        bool      `json:"is_alive"`
	HP             float64   `json:"hp"`
	Energy         float64   `json:"energy"`
	EnemyKills     int32     `json:"enemy_kills"`
	PvPKills       int32     `json:"pvp_kills"`
	Deaths         int32     `json:"deaths"`
}

// GrantEXPResult is returned when a mob/player defeat grants EXP on the Dedicated Server.
type GrantEXPResult struct {
	CharacterState
	ExpGranted int64 `json:"exp_granted"`
	LeveledUp  bool  `json:"leveled_up"`
	OldLevel   int   `json:"old_level"`
	Idempotent bool  `json:"idempotent"`
}

// Service owns exclusive write access to characters, character_attributes, and character_save_snapshots.
type Service struct {
	pool *pgxpool.Pool

	idemMu       sync.Mutex
	seenKill     map[string]GrantEXPResult
	inFlightKill map[string]chan struct{}
}

func NewService(pool *pgxpool.Pool) *Service {
	return &Service{
		pool:         pool,
		seenKill:     make(map[string]GrantEXPResult),
		inFlightKill: make(map[string]chan struct{}),
	}
}

// CreateCharacter creates a Level 1 character + character_attributes row inside an ACID transaction.
func (s *Service) CreateCharacter(ctx context.Context, accountID uuid.UUID, name, raceID string) (CharacterState, error) {
	name = strings.TrimSpace(name)
	raceID = strings.TrimSpace(raceID)
	if raceID == "" {
		raceID = "race.zenith_warrior"
	}
	if len(name) < 3 || len(name) > 20 || !validRaces[raceID] {
		return CharacterState{}, ErrInvalidCharacterSpec
	}

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return CharacterState{}, fmt.Errorf("begin tx: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var maxSlots int16
	if err := tx.QueryRow(ctx, `SELECT max_character_slots FROM accounts WHERE id = $1 FOR UPDATE`, accountID).Scan(&maxSlots); err != nil {
		return CharacterState{}, fmt.Errorf("lock account slots: %w", err)
	}

	var currentCount int
	if err := tx.QueryRow(ctx, `SELECT COUNT(*) FROM characters WHERE account_id = $1 AND deleted_at IS NULL`, accountID).Scan(&currentCount); err != nil {
		return CharacterState{}, fmt.Errorf("count characters: %w", err)
	}
	if currentCount >= int(maxSlots) {
		return CharacterState{}, ErrCharacterSlotLimit
	}

	baseStats := progression.CompileCharacterStats(raceID, 1)
	metaBytes, _ := json.Marshal(CombatCounters{})

	var st CharacterState
	err = tx.QueryRow(ctx, `
		INSERT INTO characters (
			account_id, character_name, race_id, appearance_data,
			level, current_exp, zone_id, pos_x, pos_y, pos_z, rot_yaw,
			safe_respawn_zone, safe_respawn_x, safe_respawn_y, safe_respawn_z,
			is_alive, fencing_epoch
		) VALUES (
			$1, $2, $3, $4::jsonb,
			1, 0, 'zone.p01_west_hub', 0.0, 0.0, 112.0, 0.0,
			'zone.p01_west_hub', 0.0, 0.0, 112.0,
			TRUE, 0
		)
		RETURNING id, account_id, character_name, race_id, level, current_exp, zone_id,
		          pos_x, pos_y, pos_z, rot_yaw, is_alive, fencing_epoch, updated_at
	`, accountID, name, raceID, string(metaBytes)).Scan(
		&st.ID, &st.AccountID, &st.CharacterName, &st.RaceID, &st.Level, &st.CurrentExp, &st.ZoneID,
		&st.PosX, &st.PosY, &st.PosZ, &st.RotYaw, &st.IsAlive, &st.FencingEpoch, &st.UpdatedAt,
	)
	if err != nil {
		var pgErr *pgconn.PgError
		if errors.As(err, &pgErr) && pgErr.Code == "23505" {
			return CharacterState{}, ErrCharacterNameTaken
		}
		return CharacterState{}, fmt.Errorf("insert character: %w", err)
	}

	_, err = tx.Exec(ctx, `
		INSERT INTO character_attributes (
			character_id, hp, max_hp, energy, max_energy, phys_atk, ki_atk, defense
		) VALUES ($1, $2, $2, $3, $3, $4, $5, $6)
	`, st.ID, baseStats.MaxHP, baseStats.MaxEnergy, baseStats.PhysAtk, baseStats.KiAtk, baseStats.Defense)
	if err != nil {
		return CharacterState{}, fmt.Errorf("insert character_attributes: %w", err)
	}

	_, err = tx.Exec(ctx, `INSERT INTO character_potential (character_id) VALUES ($1)`, st.ID)
	if err != nil {
		return CharacterState{}, fmt.Errorf("insert character_potential: %w", err)
	}

	if err := tx.Commit(ctx); err != nil {
		return CharacterState{}, fmt.Errorf("commit create character: %w", err)
	}

	st.HP = baseStats.MaxHP
	st.MaxHP = baseStats.MaxHP
	st.Energy = baseStats.MaxEnergy
	st.MaxEnergy = baseStats.MaxEnergy
	st.AttackPowerMultiplier = baseStats.AttackPowerMultiplier
	st.NextLevelExp = progression.ExpRequiredForNextLevel(st.Level)
	return st, nil
}

// ListByAccount returns all non-deleted characters owned by accountID.
func (s *Service) ListByAccount(ctx context.Context, accountID uuid.UUID) ([]CharacterState, error) {
	rows, err := s.pool.Query(ctx, `
		SELECT c.id, c.account_id, c.character_name, c.race_id, c.level, c.current_exp, c.zone_id,
		       c.pos_x, c.pos_y, c.pos_z, c.rot_yaw, c.is_alive, c.fencing_epoch, c.updated_at,
		       c.appearance_data,
		       a.hp, a.max_hp, a.energy, a.max_energy
		  FROM characters c
		  JOIN character_attributes a ON a.character_id = c.id
		 WHERE c.account_id = $1 AND c.deleted_at IS NULL
		 ORDER BY c.created_at ASC
	`, accountID)
	if err != nil {
		return nil, fmt.Errorf("list characters: %w", err)
	}
	defer rows.Close()

	var out []CharacterState
	for rows.Next() {
		var st CharacterState
		var metaRaw []byte
		if err := rows.Scan(
			&st.ID, &st.AccountID, &st.CharacterName, &st.RaceID, &st.Level, &st.CurrentExp, &st.ZoneID,
			&st.PosX, &st.PosY, &st.PosZ, &st.RotYaw, &st.IsAlive, &st.FencingEpoch, &st.UpdatedAt,
			&metaRaw,
			&st.HP, &st.MaxHP, &st.Energy, &st.MaxEnergy,
		); err != nil {
			return nil, err
		}
		var counters CombatCounters
		_ = json.Unmarshal(metaRaw, &counters)
		st.EnemyKills = counters.EnemyKills
		st.PvPKills = counters.PvPKills
		st.Deaths = counters.Deaths
		st.HasSavedPosition = counters.HasSavedPosition
		st.NextLevelExp = progression.ExpRequiredForNextLevel(st.Level)
		st.AttackPowerMultiplier = progression.CompileCharacterStats(st.RaceID, st.Level).AttackPowerMultiplier
		out = append(out, st)
	}
	return out, rows.Err()
}

// VerifyOwnership confirms characterID belongs to accountID and returns its current zone_id.
func (s *Service) VerifyOwnership(ctx context.Context, accountID, characterID uuid.UUID) (string, error) {
	var ownerID uuid.UUID
	var zoneID string
	err := s.pool.QueryRow(ctx, `
		SELECT account_id, zone_id FROM characters WHERE id = $1 AND deleted_at IS NULL
	`, characterID).Scan(&ownerID, &zoneID)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return "", ErrCharacterNotFound
		}
		return "", err
	}
	if ownerID != accountID {
		return "", ErrNotCharacterOwner
	}
	return zoneID, nil
}

// AdvanceEpochAndLoad atomically increments fencing_epoch (+1) in PostgreSQL and returns the full CharacterState.
// Any previous server still holding an older fencing_epoch is immediately fenced out at the SQL layer!
func (s *Service) AdvanceEpochAndLoad(ctx context.Context, characterID uuid.UUID) (CharacterState, error) {
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return CharacterState{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var st CharacterState
	var metaRaw []byte
	err = tx.QueryRow(ctx, `
		UPDATE characters
		   SET fencing_epoch = fencing_epoch + 1,
		       last_saved_at = NOW()
		 WHERE id = $1 AND deleted_at IS NULL
		RETURNING id, account_id, character_name, race_id, level, current_exp, zone_id,
		          pos_x, pos_y, pos_z, rot_yaw, is_alive, fencing_epoch, updated_at, appearance_data
	`, characterID).Scan(
		&st.ID, &st.AccountID, &st.CharacterName, &st.RaceID, &st.Level, &st.CurrentExp, &st.ZoneID,
		&st.PosX, &st.PosY, &st.PosZ, &st.RotYaw, &st.IsAlive, &st.FencingEpoch, &st.UpdatedAt, &metaRaw,
	)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return CharacterState{}, ErrCharacterNotFound
		}
		return CharacterState{}, fmt.Errorf("advance fencing_epoch: %w", err)
	}

	err = tx.QueryRow(ctx, `
		SELECT hp, max_hp, energy, max_energy
		  FROM character_attributes
		 WHERE character_id = $1
	`, characterID).Scan(&st.HP, &st.MaxHP, &st.Energy, &st.MaxEnergy)
	if err != nil {
		return CharacterState{}, fmt.Errorf("load character_attributes: %w", err)
	}

	if err := tx.Commit(ctx); err != nil {
		return CharacterState{}, err
	}

	var counters CombatCounters
	_ = json.Unmarshal(metaRaw, &counters)
	st.EnemyKills = counters.EnemyKills
	st.PvPKills = counters.PvPKills
	st.Deaths = counters.Deaths
	st.HasSavedPosition = counters.HasSavedPosition

	compiled := progression.CompileCharacterStats(st.RaceID, st.Level)
	st.MaxHP = compiled.MaxHP
	st.MaxEnergy = compiled.MaxEnergy
	st.AttackPowerMultiplier = compiled.AttackPowerMultiplier
	st.NextLevelExp = progression.ExpRequiredForNextLevel(st.Level)

	// If character logged out while dead (0 HP), revive cleanly at safe checkpoint HP on next login
	if !st.IsAlive || st.HP <= 0.0 {
		st.IsAlive = true
		st.HP = st.MaxHP
		st.Energy = st.MaxEnergy
	} else {
		if st.HP > st.MaxHP {
			st.HP = st.MaxHP
		}
		if st.Energy > st.MaxEnergy {
			st.Energy = st.MaxEnergy
		}
	}
	return st, nil
}

// SaveCheckpointFenced writes character transform, vitals, and audit snapshot ONLY IF fencing_epoch matches!
func (s *Service) SaveCheckpointFenced(ctx context.Context, in SaveCheckpointInput) (CharacterState, error) {
	if in.FencingEpoch < 1 {
		return CharacterState{}, ErrStaleFencingEpoch
	}
	zoneID := strings.TrimSpace(in.ZoneID)
	if zoneID == "" {
		zoneID = "zone.p01_west_hub"
	}
	reason := strings.TrimSpace(in.SnapshotReason)
	switch reason {
	case "PERIODIC_60S", "ZONE_TRANSFER", "LOGOUT", "LEVEL_UP", "ADMIN_BACKUP":
	default:
		reason = "PERIODIC_60S"
	}
	serverID := strings.TrimSpace(in.ServerID)
	if serverID == "" {
		serverID = "ue5-ded-7777"
	}

	metaBytes, _ := json.Marshal(CombatCounters{
		EnemyKills:       in.EnemyKills,
		PvPKills:         in.PvPKills,
		Deaths:           in.Deaths,
		HasSavedPosition: true,
	})

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return CharacterState{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var st CharacterState
	err = tx.QueryRow(ctx, `
		UPDATE characters
		   SET zone_id = $3,
		       pos_x = $4,
		       pos_y = $5,
		       pos_z = $6,
		       rot_yaw = $7,
		       is_alive = $8,
		       appearance_data = $9::jsonb,
		       last_saved_at = NOW()
		 WHERE id = $1 AND fencing_epoch = $2 AND deleted_at IS NULL
		RETURNING id, account_id, character_name, race_id, level, current_exp, zone_id,
		          pos_x, pos_y, pos_z, rot_yaw, is_alive, fencing_epoch, updated_at
	`, in.CharacterID, in.FencingEpoch, zoneID, in.PosX, in.PosY, in.PosZ, in.RotYaw, in.IsAlive, string(metaBytes)).Scan(
		&st.ID, &st.AccountID, &st.CharacterName, &st.RaceID, &st.Level, &st.CurrentExp, &st.ZoneID,
		&st.PosX, &st.PosY, &st.PosZ, &st.RotYaw, &st.IsAlive, &st.FencingEpoch, &st.UpdatedAt,
	)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return CharacterState{}, ErrStaleFencingEpoch
		}
		return CharacterState{}, fmt.Errorf("fenced update character: %w", err)
	}

	compiled := progression.CompileCharacterStats(st.RaceID, st.Level)
	clampedHP := clampFloat(in.HP, 0.0, compiled.MaxHP)
	clampedEnergy := clampFloat(in.Energy, 0.0, compiled.MaxEnergy)

	_, err = tx.Exec(ctx, `
		UPDATE character_attributes
		   SET hp = $2, max_hp = $3, energy = $4, max_energy = $5,
		       phys_atk = $6, ki_atk = $7, defense = $8
		 WHERE character_id = $1
	`, st.ID, clampedHP, compiled.MaxHP, clampedEnergy, compiled.MaxEnergy, compiled.PhysAtk, compiled.KiAtk, compiled.Defense)
	if err != nil {
		return CharacterState{}, fmt.Errorf("update character_attributes: %w", err)
	}

	snapPayload, _ := json.Marshal(map[string]any{
		"level":       st.Level,
		"current_exp": st.CurrentExp,
		"hp":          clampedHP,
		"max_hp":      compiled.MaxHP,
		"energy":      clampedEnergy,
		"max_energy":  compiled.MaxEnergy,
		"enemy_kills": in.EnemyKills,
		"pvp_kills":   in.PvPKills,
		"deaths":      in.Deaths,
	})
	sum := sha256.Sum256(snapPayload)
	snapHash := hex.EncodeToString(sum[:])

	_, err = tx.Exec(ctx, `
		INSERT INTO character_save_snapshots (
			character_id, fencing_epoch, snapshot_reason, zone_id, pos_x, pos_y, pos_z,
			state_payload, payload_sha256, server_id
		) VALUES ($1, $2, $3, $4, $5, $6, $7, $8::jsonb, $9, $10)
	`, st.ID, st.FencingEpoch, reason, st.ZoneID, st.PosX, st.PosY, st.PosZ, string(snapPayload), snapHash, serverID)
	if err != nil {
		return CharacterState{}, fmt.Errorf("insert character_save_snapshot: %w", err)
	}

	if err := tx.Commit(ctx); err != nil {
		return CharacterState{}, err
	}

	st.HP = clampedHP
	st.MaxHP = compiled.MaxHP
	st.Energy = clampedEnergy
	st.MaxEnergy = compiled.MaxEnergy
	st.AttackPowerMultiplier = compiled.AttackPowerMultiplier
	st.NextLevelExp = progression.ExpRequiredForNextLevel(st.Level)
	st.EnemyKills = in.EnemyKills
	st.PvPKills = in.PvPKills
	st.Deaths = in.Deaths
	st.HasSavedPosition = true
	return st, nil
}

// GrantEXPFenced awards EXP atomically under fencing_epoch + idempotencyKey and computes Level-Up stat upgrades!
func (s *Service) GrantEXPFenced(
	ctx context.Context,
	characterID uuid.UUID,
	fencingEpoch int64,
	serverID string,
	expGain int64,
	idempotencyKey string,
) (GrantEXPResult, error) {
	if fencingEpoch < 1 {
		return GrantEXPResult{}, ErrStaleFencingEpoch
	}
	if expGain <= 0 {
		expGain = progression.DefaultMobKillEXP
	}
	idemKey := strings.TrimSpace(idempotencyKey)
	if idemKey != "" {
		for {
			s.idemMu.Lock()
			if cached, ok := s.seenKill[idemKey]; ok {
				s.idemMu.Unlock()
				cached.Idempotent = true
				return cached, nil
			}
			if waitCh, busy := s.inFlightKill[idemKey]; busy {
				s.idemMu.Unlock()
				select {
				case <-waitCh:
					continue
				case <-ctx.Done():
					return GrantEXPResult{}, ctx.Err()
				}
			}
			waitCh := make(chan struct{})
			s.inFlightKill[idemKey] = waitCh
			s.idemMu.Unlock()
			defer func() {
				s.idemMu.Lock()
				delete(s.inFlightKill, idemKey)
				close(waitCh)
				s.idemMu.Unlock()
			}()
			break
		}
	}

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return GrantEXPResult{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var st CharacterState
	var metaRaw []byte
	err = tx.QueryRow(ctx, `
		SELECT id, account_id, character_name, race_id, level, current_exp, zone_id,
		       pos_x, pos_y, pos_z, rot_yaw, is_alive, fencing_epoch, updated_at, appearance_data
		  FROM characters
		 WHERE id = $1 AND fencing_epoch = $2 AND deleted_at IS NULL
		   FOR UPDATE
	`, characterID, fencingEpoch).Scan(
		&st.ID, &st.AccountID, &st.CharacterName, &st.RaceID, &st.Level, &st.CurrentExp, &st.ZoneID,
		&st.PosX, &st.PosY, &st.PosZ, &st.RotYaw, &st.IsAlive, &st.FencingEpoch, &st.UpdatedAt, &metaRaw,
	)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return GrantEXPResult{}, ErrStaleFencingEpoch
		}
		return GrantEXPResult{}, err
	}

	oldLevel := st.Level
	newLevel, newExp, leveledUp := progression.ApplyExpGain(st.Level, st.CurrentExp, expGain)

	var counters CombatCounters
	_ = json.Unmarshal(metaRaw, &counters)
	counters.EnemyKills++
	updatedMeta, _ := json.Marshal(counters)

	tag, err := tx.Exec(ctx, `
		UPDATE characters
		   SET level = $3,
		       current_exp = $4,
		       appearance_data = $5::jsonb,
		       last_saved_at = NOW()
		 WHERE id = $1 AND fencing_epoch = $2
	`, characterID, fencingEpoch, newLevel, newExp, string(updatedMeta))
	if err != nil {
		return GrantEXPResult{}, err
	}
	if tag.RowsAffected() == 0 {
		return GrantEXPResult{}, ErrStaleFencingEpoch
	}

	compiled := progression.CompileCharacterStats(st.RaceID, newLevel)
	var curHP, curEnergy float64
	_ = tx.QueryRow(ctx, `SELECT hp, energy FROM character_attributes WHERE character_id = $1`, characterID).Scan(&curHP, &curEnergy)
	if leveledUp {
		// Full HP/Ki restoration upon Level Up!
		curHP = compiled.MaxHP
		curEnergy = compiled.MaxEnergy
	} else {
		curHP = clampFloat(curHP, 1.0, compiled.MaxHP)
		curEnergy = clampFloat(curEnergy, 0.0, compiled.MaxEnergy)
	}

	_, err = tx.Exec(ctx, `
		UPDATE character_attributes
		   SET hp = $2, max_hp = $3, energy = $4, max_energy = $5,
		       phys_atk = $6, ki_atk = $7, defense = $8
		 WHERE character_id = $1
	`, characterID, curHP, compiled.MaxHP, curEnergy, compiled.MaxEnergy, compiled.PhysAtk, compiled.KiAtk, compiled.Defense)
	if err != nil {
		return GrantEXPResult{}, err
	}

	if leveledUp {
		snapPayload, _ := json.Marshal(map[string]any{
			"old_level": oldLevel, "new_level": newLevel, "exp": newExp, "max_hp": compiled.MaxHP,
		})
		sum := sha256.Sum256(snapPayload)
		if strings.TrimSpace(serverID) == "" {
			serverID = "ue5-ded-7777"
		}
		_, _ = tx.Exec(ctx, `
			INSERT INTO character_save_snapshots (
				character_id, fencing_epoch, snapshot_reason, zone_id, pos_x, pos_y, pos_z,
				state_payload, payload_sha256, server_id
			) VALUES ($1, $2, 'LEVEL_UP', $3, $4, $5, $6, $7::jsonb, $8, $9)
		`, characterID, fencingEpoch, st.ZoneID, st.PosX, st.PosY, st.PosZ, string(snapPayload), hex.EncodeToString(sum[:]), serverID)
	}

	if err := tx.Commit(ctx); err != nil {
		return GrantEXPResult{}, err
	}

	st.Level = newLevel
	st.CurrentExp = newExp
	st.NextLevelExp = progression.ExpRequiredForNextLevel(newLevel)
	st.HP = curHP
	st.MaxHP = compiled.MaxHP
	st.Energy = curEnergy
	st.MaxEnergy = compiled.MaxEnergy
	st.AttackPowerMultiplier = compiled.AttackPowerMultiplier
	st.EnemyKills = counters.EnemyKills
	st.PvPKills = counters.PvPKills
	st.Deaths = counters.Deaths
	st.HasSavedPosition = counters.HasSavedPosition

	res := GrantEXPResult{
		CharacterState: st,
		ExpGranted:     expGain,
		LeveledUp:      leveledUp,
		OldLevel:       oldLevel,
	}
	if idemKey != "" {
		s.idemMu.Lock()
		s.seenKill[idemKey] = res
		s.idemMu.Unlock()
	}
	return res, nil
}

func clampFloat(v, minV, maxV float64) float64 {
	if v < minV {
		return minV
	}
	if v > maxV {
		return maxV
	}
	return v
}
