package character

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"strings"
	"sync"
	"time"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/modules/economy"
	"github.com/infinite-universe/backend/internal/modules/gamedata"
	"github.com/infinite-universe/backend/internal/modules/guild"
	"github.com/infinite-universe/backend/internal/modules/inventory"
	"github.com/infinite-universe/backend/internal/modules/party"
	"github.com/infinite-universe/backend/internal/modules/progression"
	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgconn"
	"github.com/jackc/pgx/v5/pgxpool"
)

var (
	ErrCharacterNameTaken     = errors.New("character name is already taken")
	ErrCharacterSlotLimit     = errors.New("maximum character slot limit reached for account")
	ErrInvalidCharacterSpec   = errors.New("invalid character name or race_id")
	ErrCharacterNotFound      = errors.New("character not found")
	ErrNotCharacterOwner      = errors.New("character does not belong to authenticated account")
	ErrStaleFencingEpoch      = errors.New("stale fencing_epoch rejected by PostgreSQL lease fence")
	ErrInvalidPotentialStat   = errors.New("invalid potential stat_type (expected hp, ki, or atk)")
	ErrInsufficientPotential  = errors.New("insufficient potential points for requested stat upgrade")
	ErrDeadCannotTransfer     = errors.New("dead characters cannot initiate zone/channel transfer")
)

var validRaces = map[string]bool{
	"race.zenith_warrior":  true,
	"race.terran_adept":    true,
	"race.mystic_elder":    true,
	"race.frost_archon":    true,
	"race.chaos_morph":     true,
	"race.cyber_construct": true,
}

// PersistedMeta is stored in characters.appearance_data JSONB & character_save_snapshots without altering SQL DDL.
type PersistedMeta struct {
	EnemyKills       int32 `json:"enemy_kills"`
	PvPKills         int32 `json:"pvp_kills"`
	Deaths           int32 `json:"deaths"`
	HasSavedPosition bool  `json:"has_saved_pos"`
	HPUpgrades       int32 `json:"hp_upgrades"`
	KiUpgrades       int32 `json:"ki_upgrades"`
	AtkUpgrades      int32 `json:"atk_upgrades"`
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
	PotentialPoints       int64     `json:"potential_points"`
	StandardCoin          int64     `json:"standard_coin"`
	PityTitanTokens       int       `json:"pity_titan_tokens"`
	ZoneID                string    `json:"zone_id"`
	ChannelID             int       `json:"channel_id"`
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
	AttackPower           float64   `json:"attack_power"`
	AttackPowerMultiplier float64   `json:"attack_power_multiplier"`
	Defense               float64   `json:"defense"`
	HPUpgrades            int32     `json:"hp_upgrades"`
	KiUpgrades            int32     `json:"ki_upgrades"`
	AtkUpgrades           int32     `json:"atk_upgrades"`
	NextHPCost            int64     `json:"next_hp_cost"`
	NextKiCost            int64     `json:"next_ki_cost"`
	NextAtkCost           int64     `json:"next_atk_cost"`
	EnemyKills            int32     `json:"enemy_kills"`
	PvPKills              int32     `json:"pvp_kills"`
	Deaths                int32     `json:"deaths"`
	GuildID               string    `json:"guild_id,omitempty"`
	GuildName             string    `json:"guild_name,omitempty"`
	GuildTag              string    `json:"guild_tag,omitempty"`
	GuildRole             string    `json:"guild_role,omitempty"`
	FencingEpoch          int64     `json:"fencing_epoch"`
	UpdatedAt             time.Time `json:"updated_at"`
}

// SaveCheckpointInput carries state flushed from an authoritative UE5 Dedicated Server.
type SaveCheckpointInput struct {
	CharacterID    uuid.UUID `json:"character_id"`
	FencingEpoch   int64     `json:"fencing_epoch"`
	ServerID       string    `json:"server_id"`
	SnapshotReason string    `json:"snapshot_reason"`
	ZoneID         string    `json:"zone_id"`
	ChannelID      int       `json:"channel_id"`
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

// GrantEXPResult is returned when a monster defeat grants LV EXP + Potential Points on the Dedicated Server.
type GrantEXPResult struct {
	CharacterState
	ExpGranted       int64   `json:"exp_granted"`
	PotentialGranted int64   `json:"potential_granted"`
	PartySynergyMult float64 `json:"party_synergy_mult"`
	LeveledUp        bool    `json:"leveled_up"`
	OldLevel         int     `json:"old_level"`
	Idempotent       bool    `json:"idempotent"`
}

// AllocatePotentialResult is returned when a player spends Potential Points to upgrade HP, Ki, or Attack Power.
type AllocatePotentialResult struct {
	CharacterState
	StatType       string `json:"stat_type"`
	PotentialSpent int64  `json:"potential_spent"`
}

// Service owns exclusive write access to characters, character_attributes, character_potential, and character_save_snapshots.
type Service struct {
	pool    *pgxpool.Pool
	catalog *gamedata.Catalog

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

func (s *Service) SetCatalog(cat *gamedata.Catalog) {
	s.catalog = cat
}

func populateStateFromMetaWithBonus(st *CharacterState, meta PersistedMeta, eqBonus progression.EquipmentBonuses, clampLiveVitals bool) {
	compiled := progression.CompileTotalCharacterStats(meta.HPUpgrades, meta.KiUpgrades, meta.AtkUpgrades, eqBonus)
	st.EnemyKills = meta.EnemyKills
	st.PvPKills = meta.PvPKills
	st.Deaths = meta.Deaths
	st.HasSavedPosition = meta.HasSavedPosition
	st.HPUpgrades = meta.HPUpgrades
	st.KiUpgrades = meta.KiUpgrades
	st.AtkUpgrades = meta.AtkUpgrades
	st.MaxHP = compiled.MaxHP
	st.MaxEnergy = compiled.MaxEnergy
	st.AttackPower = compiled.AttackPower
	st.AttackPowerMultiplier = compiled.AttackPowerMultiplier
	st.Defense = compiled.Defense
	st.NextHPCost = compiled.NextHPCost
	st.NextKiCost = compiled.NextKiCost
	st.NextAtkCost = compiled.NextAtkCost
	st.NextLevelExp = progression.ExpRequiredForNextLevel(st.Level)

	if clampLiveVitals {
		if !st.IsAlive || st.HP <= 0.0 {
			st.IsAlive = true
			st.HP = st.MaxHP
			st.Energy = st.MaxEnergy
		} else {
			st.HP = clampFloat(st.HP, 1.0, st.MaxHP)
			st.Energy = clampFloat(st.Energy, 0.0, st.MaxEnergy)
		}
	}
}

func populateStateFromMeta(st *CharacterState, meta PersistedMeta, clampLiveVitals bool) {
	populateStateFromMetaWithBonus(st, meta, progression.EquipmentBonuses{}, clampLiveVitals)
}

// CreateCharacter creates a Level 1 character with 200 HP, 200 Ki, 100 Attack Power, and 0 Potential Points.
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

	baseStats := progression.CompileCharacterStats(0, 0, 0)
	meta := PersistedMeta{}
	metaBytes, _ := json.Marshal(meta)

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

	_, err = tx.Exec(ctx, `INSERT INTO character_potential (character_id, potential_exp) VALUES ($1, 0)`, st.ID)
	if err != nil {
		return CharacterState{}, fmt.Errorf("insert character_potential: %w", err)
	}

	if err := economy.EnsureWalletTx(ctx, tx, st.ID); err != nil {
		return CharacterState{}, fmt.Errorf("init character_wallets: %w", err)
	}
	if s.catalog != nil {
		if err := inventory.EnsureStarterKitTx(ctx, tx, s.catalog, st.ID); err != nil {
			return CharacterState{}, fmt.Errorf("grant starter kit: %w", err)
		}
	}

	if err := tx.Commit(ctx); err != nil {
		return CharacterState{}, fmt.Errorf("commit create character: %w", err)
	}

	st.HP = baseStats.MaxHP
	st.Energy = baseStats.MaxEnergy
	st.PotentialPoints = 0
	populateStateFromMeta(&st, meta, false)
	return st, nil
}

// ListByAccount returns all non-deleted characters owned by accountID.
func (s *Service) ListByAccount(ctx context.Context, accountID uuid.UUID) ([]CharacterState, error) {
	rows, err := s.pool.Query(ctx, `
		SELECT c.id, c.account_id, c.character_name, c.race_id, c.level, c.current_exp, c.zone_id,
		       c.pos_x, c.pos_y, c.pos_z, c.rot_yaw, c.is_alive, c.fencing_epoch, c.updated_at,
		       c.appearance_data,
		       a.hp, a.max_hp, a.energy, a.max_energy, a.phys_atk, a.defense,
		       p.potential_exp,
		       COALESCE(w.standard_coin, 0),
		       COALESCE(g.guild_id::text, ''), COALESCE(g.guild_name::text, ''),
		       COALESCE(g.guild_tag::text, ''), COALESCE(gm.member_role, '')
		  FROM characters c
		  JOIN character_attributes a ON a.character_id = c.id
		  JOIN character_potential p ON p.character_id = c.id
		  LEFT JOIN character_wallets w ON w.character_id = c.id
		  LEFT JOIN guild_members gm ON gm.character_id = c.id
		  LEFT JOIN guilds g ON g.guild_id = gm.guild_id
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
		var dbMaxHP, dbMaxEnergy, dbPhysAtk, dbDef float64
		if err := rows.Scan(
			&st.ID, &st.AccountID, &st.CharacterName, &st.RaceID, &st.Level, &st.CurrentExp, &st.ZoneID,
			&st.PosX, &st.PosY, &st.PosZ, &st.RotYaw, &st.IsAlive, &st.FencingEpoch, &st.UpdatedAt,
			&metaRaw,
			&st.HP, &dbMaxHP, &st.Energy, &dbMaxEnergy, &dbPhysAtk, &dbDef,
			&st.PotentialPoints,
			&st.StandardCoin,
			&st.GuildID, &st.GuildName, &st.GuildTag, &st.GuildRole,
		); err != nil {
			return nil, err
		}
		var meta PersistedMeta
		_ = json.Unmarshal(metaRaw, &meta)
		populateStateFromMeta(&st, meta, false)
		if dbMaxHP > st.MaxHP {
			st.MaxHP = dbMaxHP
		}
		if dbMaxEnergy > st.MaxEnergy {
			st.MaxEnergy = dbMaxEnergy
		}
		if dbPhysAtk > st.AttackPower {
			st.AttackPower = dbPhysAtk
			st.AttackPowerMultiplier = dbPhysAtk / progression.BaseAttackPower
		}
		if dbDef > st.Defense {
			st.Defense = dbDef
		}
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
		RETURNING id, account_id, character_name, race_id, level, current_exp, zone_id, last_channel_id,
		          pos_x, pos_y, pos_z, rot_yaw, is_alive, fencing_epoch, updated_at, appearance_data
	`, characterID).Scan(
		&st.ID, &st.AccountID, &st.CharacterName, &st.RaceID, &st.Level, &st.CurrentExp, &st.ZoneID, &st.ChannelID,
		&st.PosX, &st.PosY, &st.PosZ, &st.RotYaw, &st.IsAlive, &st.FencingEpoch, &st.UpdatedAt, &metaRaw,
	)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return CharacterState{}, ErrCharacterNotFound
		}
		return CharacterState{}, fmt.Errorf("advance fencing_epoch: %w", err)
	}

	if err := economy.EnsureWalletTx(ctx, tx, characterID); err != nil {
		return CharacterState{}, fmt.Errorf("ensure wallet on claim: %w", err)
	}
	if s.catalog != nil {
		if err := inventory.EnsureStarterKitTx(ctx, tx, s.catalog, characterID); err != nil {
			return CharacterState{}, fmt.Errorf("ensure starter kit on claim: %w", err)
		}
	}

	err = tx.QueryRow(ctx, `
		SELECT a.hp, a.max_hp, a.energy, a.max_energy, p.potential_exp, COALESCE(w.standard_coin, 0)
		  FROM character_attributes a
		  JOIN character_potential p ON p.character_id = a.character_id
		  LEFT JOIN character_wallets w ON w.character_id = a.character_id
		 WHERE a.character_id = $1
	`, characterID).Scan(&st.HP, &st.MaxHP, &st.Energy, &st.MaxEnergy, &st.PotentialPoints, &st.StandardCoin)
	if err != nil {
		return CharacterState{}, fmt.Errorf("load character_attributes & potential: %w", err)
	}

	eqBonus, err := inventory.ComputeEquippedBonusesTx(ctx, tx, s.catalog, characterID)
	if err != nil {
		return CharacterState{}, fmt.Errorf("compute equipped bonuses on claim: %w", err)
	}

	if gb, gbErr := guild.LookupCharacterGuildTx(ctx, tx, characterID); gbErr == nil {
		st.GuildID = gb.GuildID
		st.GuildName = gb.GuildName
		st.GuildTag = gb.GuildTag
		st.GuildRole = gb.GuildRole
	}
	_ = tx.QueryRow(ctx, `
		SELECT COALESCE(amount, 0) FROM boss_pity_tokens
		 WHERE character_id = $1 AND token_id = 'token.boss_highland_titan'
	`, characterID).Scan(&st.PityTitanTokens)

	var meta PersistedMeta
	_ = json.Unmarshal(metaRaw, &meta)
	populateStateFromMetaWithBonus(&st, meta, eqBonus, true)

	_, _ = tx.Exec(ctx, `
		UPDATE character_attributes
		   SET hp = $2, max_hp = $3, energy = $4, max_energy = $5,
		       phys_atk = $6, ki_atk = $7, defense = $8
		 WHERE character_id = $1
	`, characterID, st.HP, st.MaxHP, st.Energy, st.MaxEnergy, st.AttackPower, st.AttackPower, st.Defense)

	if err := tx.Commit(ctx); err != nil {
		return CharacterState{}, err
	}

	return st, nil
}

// SaveCheckpointFenced writes character transform, vitals, and audit snapshot ONLY IF fencing_epoch matches,
// while preserving allocated Potential upgrade counts in appearance_data!
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

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return CharacterState{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	// Lock row under fencing_epoch first so concurrent Potential allocations are preserved!
	var existingMetaRaw []byte
	err = tx.QueryRow(ctx, `
		SELECT appearance_data
		  FROM characters
		 WHERE id = $1 AND fencing_epoch = $2 AND deleted_at IS NULL
		   FOR UPDATE
	`, in.CharacterID, in.FencingEpoch).Scan(&existingMetaRaw)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return CharacterState{}, ErrStaleFencingEpoch
		}
		return CharacterState{}, fmt.Errorf("lock character for save: %w", err)
	}

	var meta PersistedMeta
	_ = json.Unmarshal(existingMetaRaw, &meta)
	if in.EnemyKills > meta.EnemyKills {
		meta.EnemyKills = in.EnemyKills
	}
	meta.PvPKills = in.PvPKills
	meta.Deaths = in.Deaths
	meta.HasSavedPosition = true
	metaBytes, _ := json.Marshal(meta)

	var st CharacterState
	err = tx.QueryRow(ctx, `
		UPDATE characters
		   SET zone_id = $3,
		       last_channel_id = CASE WHEN $10 >= 1 THEN $10 ELSE last_channel_id END,
		       pos_x = $4,
		       pos_y = $5,
		       pos_z = $6,
		       rot_yaw = $7,
		       is_alive = $8,
		       appearance_data = $9::jsonb,
		       last_saved_at = NOW()
		 WHERE id = $1 AND fencing_epoch = $2
		RETURNING id, account_id, character_name, race_id, level, current_exp, zone_id, last_channel_id,
		          pos_x, pos_y, pos_z, rot_yaw, is_alive, fencing_epoch, updated_at
	`, in.CharacterID, in.FencingEpoch, zoneID, in.PosX, in.PosY, in.PosZ, in.RotYaw, in.IsAlive, string(metaBytes), in.ChannelID).Scan(
		&st.ID, &st.AccountID, &st.CharacterName, &st.RaceID, &st.Level, &st.CurrentExp, &st.ZoneID, &st.ChannelID,
		&st.PosX, &st.PosY, &st.PosZ, &st.RotYaw, &st.IsAlive, &st.FencingEpoch, &st.UpdatedAt,
	)
	if err != nil {
		return CharacterState{}, fmt.Errorf("fenced update character: %w", err)
	}

	eqBonus, err := inventory.ComputeEquippedBonusesTx(ctx, tx, s.catalog, st.ID)
	if err != nil {
		return CharacterState{}, fmt.Errorf("compute equipped bonuses on save: %w", err)
	}
	compiled := progression.CompileTotalCharacterStats(meta.HPUpgrades, meta.KiUpgrades, meta.AtkUpgrades, eqBonus)
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

	_ = tx.QueryRow(ctx, `SELECT potential_exp FROM character_potential WHERE character_id = $1`, st.ID).Scan(&st.PotentialPoints)
	_ = tx.QueryRow(ctx, `SELECT COALESCE(standard_coin, 0) FROM character_wallets WHERE character_id = $1`, st.ID).Scan(&st.StandardCoin)
	if gb, gbErr := guild.LookupCharacterGuildTx(ctx, tx, st.ID); gbErr == nil {
		st.GuildID = gb.GuildID
		st.GuildName = gb.GuildName
		st.GuildTag = gb.GuildTag
		st.GuildRole = gb.GuildRole
	}

	snapPayload, _ := json.Marshal(map[string]any{
		"level":            st.Level,
		"current_exp":      st.CurrentExp,
		"potential_points": st.PotentialPoints,
		"standard_coin":    st.StandardCoin,
		"hp":               clampedHP,
		"max_hp":           compiled.MaxHP,
		"energy":           clampedEnergy,
		"max_energy":       compiled.MaxEnergy,
		"attack_power":     compiled.AttackPower,
		"defense":          compiled.Defense,
		"hp_upgrades":      meta.HPUpgrades,
		"ki_upgrades":      meta.KiUpgrades,
		"atk_upgrades":     meta.AtkUpgrades,
		"enemy_kills":      meta.EnemyKills,
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
	st.Energy = clampedEnergy
	populateStateFromMetaWithBonus(&st, meta, eqBonus, false)
	return st, nil
}

// GrantEXPFenced awards Điểm Sức mạnh (LV EXP, for Level ONLY — zero automatic stat increase!) AND Điểm Tiềm năng (Potential Points)
// on EVERY valid combat hit (1.5x outgoingDamage when outgoingDamage > 0), incrementing EnemyKills only when killedTarget is true.
func (s *Service) GrantEXPFenced(
	ctx context.Context,
	characterID uuid.UUID,
	fencingEpoch int64,
	serverID string,
	outgoingDamage float64,
	expGain int64,
	potentialGain int64,
	killedTarget bool,
	idempotencyKey string,
) (GrantEXPResult, error) {
	return s.GrantEXPWithPartyFenced(ctx, characterID, fencingEpoch, serverID, outgoingDamage, expGain, potentialGain, 1, killedTarget, idempotencyKey)
}

// GrantEXPWithPartyFenced awards per-hit 1.5x Dame SM & TN scaled by Party Synergy (1.15x..1.45x within 120m).
func (s *Service) GrantEXPWithPartyFenced(
	ctx context.Context,
	characterID uuid.UUID,
	fencingEpoch int64,
	serverID string,
	outgoingDamage float64,
	expGain int64,
	potentialGain int64,
	nearbyPartyMembers int,
	killedTarget bool,
	idempotencyKey string,
) (GrantEXPResult, error) {
	if fencingEpoch < 1 {
		return GrantEXPResult{}, ErrStaleFencingEpoch
	}
	synergyMult := party.ComputeSynergyMultiplier(nearbyPartyMembers)
	if outgoingDamage > 0 {
		baseExp, basePot := progression.ComputeHitProgressionReward(outgoingDamage)
		if synergyMult > 1.0 {
			expGain = max(1, int64(math.Round(float64(baseExp)*synergyMult)))
			potentialGain = max(1, int64(math.Round(float64(basePot)*synergyMult)))
		} else {
			expGain, potentialGain = baseExp, basePot
		}
	} else {
		if expGain <= 0 {
			expGain = progression.DefaultHitEXP
		}
		if potentialGain <= 0 {
			potentialGain = expGain
		}
		if synergyMult > 1.0 {
			expGain = max(1, int64(math.Round(float64(expGain)*synergyMult)))
			potentialGain = max(1, int64(math.Round(float64(potentialGain)*synergyMult)))
		}
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

	var meta PersistedMeta
	_ = json.Unmarshal(metaRaw, &meta)
	if killedTarget {
		meta.EnemyKills++
	}
	updatedMeta, _ := json.Marshal(meta)

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

	// Award Potential Points (Điểm Tiềm năng) in character_potential!
	err = tx.QueryRow(ctx, `
		UPDATE character_potential
		   SET potential_exp = potential_exp + $2
		 WHERE character_id = $1
		RETURNING potential_exp
	`, characterID, potentialGain).Scan(&st.PotentialPoints)
	if err != nil {
		return GrantEXPResult{}, fmt.Errorf("award potential_exp: %w", err)
	}

	// Read current vitals UNCHANGED (Level-Up NEVER increases MaxHP, MaxKi, or AttackPower!)
	var dbMaxHP, dbMaxEnergy, dbPhysAtk, dbDef float64
	_ = tx.QueryRow(ctx, `
		SELECT hp, max_hp, energy, max_energy, phys_atk, defense
		  FROM character_attributes WHERE character_id = $1
	`, characterID).Scan(&st.HP, &dbMaxHP, &st.Energy, &dbMaxEnergy, &dbPhysAtk, &dbDef)
	_ = tx.QueryRow(ctx, `SELECT COALESCE(standard_coin, 0) FROM character_wallets WHERE character_id = $1`, characterID).Scan(&st.StandardCoin)
	if gb, gbErr := guild.LookupCharacterGuildTx(ctx, tx, characterID); gbErr == nil {
		st.GuildID = gb.GuildID
		st.GuildName = gb.GuildName
		st.GuildTag = gb.GuildTag
		st.GuildRole = gb.GuildRole
	}
	_ = tx.QueryRow(ctx, `
		SELECT COALESCE(amount, 0) FROM boss_pity_tokens
		 WHERE character_id = $1 AND token_id = 'token.boss_highland_titan'
	`, characterID).Scan(&st.PityTitanTokens)

	if leveledUp {
		snapPayload, _ := json.Marshal(map[string]any{
			"old_level":        oldLevel,
			"new_level":        newLevel,
			"exp":              newExp,
			"potential_points": st.PotentialPoints,
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
	populateStateFromMeta(&st, meta, false)
	if dbMaxHP > st.MaxHP {
		st.MaxHP = dbMaxHP
	}
	if dbMaxEnergy > st.MaxEnergy {
		st.MaxEnergy = dbMaxEnergy
	}
	if dbPhysAtk > st.AttackPower {
		st.AttackPower = dbPhysAtk
		st.AttackPowerMultiplier = dbPhysAtk / progression.BaseAttackPower
	}
	if dbDef > st.Defense {
		st.Defense = dbDef
	}

	res := GrantEXPResult{
		CharacterState:   st,
		ExpGranted:       expGain,
		PotentialGranted: potentialGain,
		PartySynergyMult: synergyMult,
		LeveledUp:        leveledUp,
		OldLevel:         oldLevel,
	}
	if idemKey != "" {
		s.idemMu.Lock()
		if len(s.seenKill) > 8192 {
			clear(s.seenKill)
		}
		s.seenKill[idemKey] = res
		s.idemMu.Unlock()
	}
	return res, nil
}

// AllocatePotentialFenced spends Potential Points atomically under fencing_epoch to upgrade HP (+100), Ki (+100), or Attack Power (+10).
func (s *Service) AllocatePotentialFenced(
	ctx context.Context,
	characterID uuid.UUID,
	fencingEpoch int64,
	serverID string,
	statType string,
) (AllocatePotentialResult, error) {
	if fencingEpoch < 1 {
		return AllocatePotentialResult{}, ErrStaleFencingEpoch
	}
	statType = strings.ToLower(strings.TrimSpace(statType))
	switch statType {
	case progression.StatTypeHP, progression.StatTypeKi, progression.StatTypeAtk:
	default:
		return AllocatePotentialResult{}, ErrInvalidPotentialStat
	}

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return AllocatePotentialResult{}, err
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
			return AllocatePotentialResult{}, ErrStaleFencingEpoch
		}
		return AllocatePotentialResult{}, err
	}

	var curPotential int64
	if err := tx.QueryRow(ctx, `SELECT potential_exp FROM character_potential WHERE character_id = $1 FOR UPDATE`, characterID).Scan(&curPotential); err != nil {
		return AllocatePotentialResult{}, err
	}

	var curHP, curEnergy float64
	if err := tx.QueryRow(ctx, `SELECT hp, energy FROM character_attributes WHERE character_id = $1 FOR UPDATE`, characterID).Scan(&curHP, &curEnergy); err != nil {
		return AllocatePotentialResult{}, err
	}

	var meta PersistedMeta
	_ = json.Unmarshal(metaRaw, &meta)

	var cost int64
	switch statType {
	case progression.StatTypeHP:
		cost = progression.PotentialUpgradeCost(progression.StatTypeHP, meta.HPUpgrades)
	case progression.StatTypeKi:
		cost = progression.PotentialUpgradeCost(progression.StatTypeKi, meta.KiUpgrades)
	case progression.StatTypeAtk:
		cost = progression.PotentialUpgradeCost(progression.StatTypeAtk, meta.AtkUpgrades)
	}

	if cost <= 0 || curPotential < cost {
		return AllocatePotentialResult{}, ErrInsufficientPotential
	}

	// Deduct Potential Points and apply chosen stat upgrade
	newPotential := curPotential - cost
	switch statType {
	case progression.StatTypeHP:
		meta.HPUpgrades++
		curHP += progression.StepUpgradeHP
	case progression.StatTypeKi:
		meta.KiUpgrades++
		curEnergy += progression.StepUpgradeKi
	case progression.StatTypeAtk:
		meta.AtkUpgrades++
	}

	eqBonus, err := inventory.ComputeEquippedBonusesTx(ctx, tx, s.catalog, characterID)
	if err != nil {
		return AllocatePotentialResult{}, err
	}
	compiled := progression.CompileTotalCharacterStats(meta.HPUpgrades, meta.KiUpgrades, meta.AtkUpgrades, eqBonus)
	curHP = clampFloat(curHP, 1.0, compiled.MaxHP)
	curEnergy = clampFloat(curEnergy, 0.0, compiled.MaxEnergy)
	updatedMeta, _ := json.Marshal(meta)

	if _, err := tx.Exec(ctx, `
		UPDATE characters
		   SET appearance_data = $3::jsonb,
		       last_saved_at = NOW()
		 WHERE id = $1 AND fencing_epoch = $2
	`, characterID, fencingEpoch, string(updatedMeta)); err != nil {
		return AllocatePotentialResult{}, err
	}

	if _, err := tx.Exec(ctx, `
		UPDATE character_potential
		   SET potential_exp = $2
		 WHERE character_id = $1
	`, characterID, newPotential); err != nil {
		return AllocatePotentialResult{}, err
	}

	if _, err := tx.Exec(ctx, `
		UPDATE character_attributes
		   SET hp = $2, max_hp = $3, energy = $4, max_energy = $5,
		       phys_atk = $6, ki_atk = $7, defense = $8
		 WHERE character_id = $1
	`, characterID, curHP, compiled.MaxHP, curEnergy, compiled.MaxEnergy, compiled.PhysAtk, compiled.KiAtk, compiled.Defense); err != nil {
		return AllocatePotentialResult{}, err
	}

	_ = tx.QueryRow(ctx, `SELECT COALESCE(standard_coin, 0) FROM character_wallets WHERE character_id = $1`, characterID).Scan(&st.StandardCoin)
	if gb, gbErr := guild.LookupCharacterGuildTx(ctx, tx, characterID); gbErr == nil {
		st.GuildID = gb.GuildID
		st.GuildName = gb.GuildName
		st.GuildTag = gb.GuildTag
		st.GuildRole = gb.GuildRole
	}
	_ = tx.QueryRow(ctx, `
		SELECT COALESCE(amount, 0) FROM boss_pity_tokens
		 WHERE character_id = $1 AND token_id = 'token.boss_highland_titan'
	`, characterID).Scan(&st.PityTitanTokens)

	if err := tx.Commit(ctx); err != nil {
		return AllocatePotentialResult{}, err
	}

	st.HP = curHP
	st.Energy = curEnergy
	st.PotentialPoints = newPotential
	populateStateFromMetaWithBonus(&st, meta, eqBonus, false)

	return AllocatePotentialResult{
		CharacterState: st,
		StatType:       statType,
		PotentialSpent: cost,
	}, nil
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


// PrepareZoneTransferFenced flushes an immutable ZONE_TRANSFER snapshot at the source zone
// and atomically updates destination (zone_id, last_channel_id, pos_x, pos_y, pos_z, rot_yaw) under fencing_epoch.
func (s *Service) PrepareZoneTransferFenced(
	ctx context.Context,
	in SaveCheckpointInput,
	targetZoneID string,
	targetChannelID int,
	destX, destY, destZ float64,
	destYaw float32,
) (CharacterState, error) {
	if !in.IsAlive || in.HP <= 0 {
		return CharacterState{}, ErrDeadCannotTransfer
	}
	in.SnapshotReason = "ZONE_TRANSFER"
	saved, err := s.SaveCheckpointFenced(ctx, in)
	if err != nil {
		return CharacterState{}, err
	}

	cleanTargetZone := strings.TrimSpace(targetZoneID)
	if cleanTargetZone == "" {
		cleanTargetZone = saved.ZoneID
	}
	cleanChan := max(1, targetChannelID)

	res, err := s.pool.Exec(ctx, `
		UPDATE characters
		   SET zone_id = $3,
		       last_channel_id = $4,
		       pos_x = $5,
		       pos_y = $6,
		       pos_z = $7,
		       rot_yaw = $8,
		       last_saved_at = NOW()
		 WHERE id = $1 AND fencing_epoch = $2 AND deleted_at IS NULL
	`, in.CharacterID, in.FencingEpoch, cleanTargetZone, cleanChan, destX, destY, destZ, destYaw)
	if err != nil {
		return CharacterState{}, err
	}
	if res.RowsAffected() == 0 {
		return CharacterState{}, ErrStaleFencingEpoch
	}

	saved.ZoneID = cleanTargetZone
	saved.ChannelID = cleanChan
	saved.PosX = destX
	saved.PosY = destY
	saved.PosZ = destZ
	saved.RotYaw = destYaw
	saved.HasSavedPosition = true
	return saved, nil
}
