package guild

import (
	"context"
	"errors"
	"fmt"
	"strings"
	"time"
	"unicode"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/modules/economy"
	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgconn"
	"github.com/jackc/pgx/v5/pgxpool"
)

const (
	GuildCreateCoinFee  int64 = 200
	GuildCreateMinLevel int   = 2

	RoleLeader     = "LEADER"
	RoleViceLeader = "VICE_LEADER"
	RoleElite      = "ELITE"
	RoleMember     = "MEMBER"
)

var (
	ErrStaleFencingEpoch     = errors.New("stale fencing_epoch rejected by PostgreSQL lease fence")
	ErrGuildLevelTooLow      = errors.New("character must reach at least Level 2 to create a Guild")
	ErrInvalidGuildNameOrTag = errors.New("guild_name must be 3-24 chars and guild_tag must be 2-5 alphanumeric chars")
	ErrGuildNameOrTagTaken   = errors.New("guild name or tag is already taken")
	ErrAlreadyInGuild        = errors.New("characteralready belongs to a guild")
	ErrNotInGuild            = errors.New("character does not belong to any guild")
	ErrGuildNotFound         = errors.New("guild not found")
	ErrGuildFull             = errors.New("guild has reached maximum member capacity")
	ErrInsufficientGuildPerm = errors.New("insufficient guild role permission for this action")
	ErrLeaderCannotLeave     = errors.New("guild leader cannot leave while other members remain; transfer leadership or kick members first")
	ErrInvalidDonateAmount   = errors.New("guild donation amount must be positive")
	ErrInvalidRole           = errors.New("invalid guild member_role")
)

type GuildMemberView struct {
	CharacterID        uuid.UUID `json:"character_id"`
	CharacterName      string    `json:"character_name"`
	Level              int       `json:"level"`
	MemberRole         string    `json:"member_role"`
	WeeklyContribution int64     `json:"weekly_contribution"`
	TotalContribution  int64     `json:"total_contribution"`
	JoinedAt           time.Time `json:"joined_at"`
}

type GuildView struct {
	GuildID              uuid.UUID         `json:"guild_id"`
	GuildName            string            `json:"guild_name"`
	GuildTag             string            `json:"guild_tag"`
	LeaderCharID         uuid.UUID         `json:"leader_char_id"`
	LeaderName           string            `json:"leader_name"`
	GuildLevel           int16             `json:"guild_level"`
	GuildExp             int64             `json:"guild_exp"`
	MemberCount          int               `json:"member_count"`
	MemberCap            int16             `json:"member_cap"`
	TreasuryStandardCoin int64             `json:"treasury_standard_coin"`
	MOTD                 string            `json:"motd_announcement"`
	MyRole               string            `json:"my_role"`
	MyWeeklyContribution int64             `json:"my_weekly_contribution"`
	MyTotalContribution  int64             `json:"my_total_contribution"`
	Members              []GuildMemberView `json:"members"`
}

type GuildDirectoryEntry struct {
	GuildID     uuid.UUID `json:"guild_id"`
	GuildName   string    `json:"guild_name"`
	GuildTag    string    `json:"guild_tag"`
	LeaderName  string    `json:"leader_name"`
	GuildLevel  int16     `json:"guild_level"`
	MemberCount int       `json:"member_count"`
	MemberCap   int16     `json:"member_cap"`
	MOTD        string    `json:"motd_announcement"`
}

type GuildSnapshot struct {
	InGuild       bool                  `json:"in_guild"`
	Guild         *GuildView            `json:"guild,omitempty"`
	Directory     []GuildDirectoryEntry `json:"directory"`
	StandardCoin  int64                 `json:"standard_coin"`
	ActionMessage string                `json:"action_message,omitempty"`
}

type CharacterGuildBrief struct {
	GuildID   string
	GuildName string
	GuildTag  string
	GuildRole string
}

type Service struct {
	pool *pgxpool.Pool
}

func NewService(pool *pgxpool.Pool) *Service {
	return &Service{pool: pool}
}

func validateGuildNameAndTag(rawName, rawTag string) (string, string, error) {
	name := strings.TrimSpace(rawName)
	tag := strings.ToUpper(strings.TrimSpace(rawTag))
	nameLen := len([]rune(name))
	tagLen := len([]rune(tag))
	if nameLen < 3 || nameLen > 24 || tagLen < 2 || tagLen > 5 {
		return "", "", ErrInvalidGuildNameOrTag
	}
	for _, r := range name {
		if unicode.IsControl(r) {
			return "", "", ErrInvalidGuildNameOrTag
		}
	}
	for _, r := range tag {
		if !((r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9')) {
			return "", "", ErrInvalidGuildNameOrTag
		}
	}
	return name, tag, nil
}

func lockCharacterFencedTx(ctx context.Context, tx pgx.Tx, charID uuid.UUID, epoch int64) (int, error) {
	if epoch < 1 {
		return 0, ErrStaleFencingEpoch
	}
	var lv int
	err := tx.QueryRow(ctx, `
		SELECT level
		  FROM characters
		 WHERE id = $1 AND fencing_epoch = $2 AND deleted_at IS NULL
		   FOR UPDATE
	`, charID, epoch).Scan(&lv)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return 0, ErrStaleFencingEpoch
		}
		return 0, err
	}
	return lv, nil
}

// LookupCharacterGuildTx returns a character's guild membership summary inside an existing tx (or empty if not in a guild).
func LookupCharacterGuildTx(ctx context.Context, tx pgx.Tx, charID uuid.UUID) (CharacterGuildBrief, error) {
	var b CharacterGuildBrief
	err := tx.QueryRow(ctx, `
		SELECT g.guild_id::text, g.guild_name::text, g.guild_tag::text, gm.member_role
		  FROM guild_members gm
		  JOIN guilds g ON g.guild_id = gm.guild_id
		 WHERE gm.character_id = $1
	`, charID).Scan(&b.GuildID, &b.GuildName, &b.GuildTag, &b.GuildRole)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return CharacterGuildBrief{}, nil
		}
		return CharacterGuildBrief{}, err
	}
	return b, nil
}

func (s *Service) LookupCharacterGuild(ctx context.Context, charID uuid.UUID) (CharacterGuildBrief, error) {
	var b CharacterGuildBrief
	err := s.pool.QueryRow(ctx, `
		SELECT g.guild_id::text, g.guild_name::text, g.guild_tag::text, gm.member_role
		  FROM guild_members gm
		  JOIN guilds g ON g.guild_id = gm.guild_id
		 WHERE gm.character_id = $1
	`, charID).Scan(&b.GuildID, &b.GuildName, &b.GuildTag, &b.GuildRole)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return CharacterGuildBrief{}, nil
		}
		return CharacterGuildBrief{}, err
	}
	return b, nil
}

func (s *Service) getSnapshotInTx(ctx context.Context, tx pgx.Tx, charID uuid.UUID, msg string) (GuildSnapshot, error) {
	snap := GuildSnapshot{
		Directory:     []GuildDirectoryEntry{},
		ActionMessage: msg,
	}

	_ = tx.QueryRow(ctx, `
		SELECT COALESCE(standard_coin, 0) FROM character_wallets WHERE character_id = $1
	`, charID).Scan(&snap.StandardCoin)

	var gv GuildView
	err := tx.QueryRow(ctx, `
		SELECT g.guild_id, g.guild_name::text, g.guild_tag::text, g.leader_char_id, lc.character_name::text,
		       g.guild_level, g.guild_exp, g.member_cap, g.treasury_standard_coin, g.motd_announcement,
		       gm.member_role, gm.weekly_contribution, gm.total_contribution
		  FROM guild_members gm
		  JOIN guilds g ON g.guild_id = gm.guild_id
		  JOIN characters lc ON lc.id = g.leader_char_id
		 WHERE gm.character_id = $1
	`, charID).Scan(
		&gv.GuildID, &gv.GuildName, &gv.GuildTag, &gv.LeaderCharID, &gv.LeaderName,
		&gv.GuildLevel, &gv.GuildExp, &gv.MemberCap, &gv.TreasuryStandardCoin, &gv.MOTD,
		&gv.MyRole, &gv.MyWeeklyContribution, &gv.MyTotalContribution,
	)
	if err == nil {
		snap.InGuild = true
		gv.Members = []GuildMemberView{}
		rows, err := tx.Query(ctx, `
			SELECT gm.character_id, c.character_name::text, c.level, gm.member_role,
			       gm.weekly_contribution, gm.total_contribution, gm.joined_at
			  FROM guild_members gm
			  JOIN characters c ON c.id = gm.character_id
			 WHERE gm.guild_id = $1
			 ORDER BY
			   CASE gm.member_role
			     WHEN 'LEADER' THEN 1
			     WHEN 'VICE_LEADER' THEN 2
			     WHEN 'ELITE' THEN 3
			     ELSE 4
			   END ASC,
			   gm.total_contribution DESC,
			   gm.joined_at ASC
		`, gv.GuildID)
		if err != nil {
			return snap, err
		}
		for rows.Next() {
			var mv GuildMemberView
			if err := rows.Scan(
				&mv.CharacterID, &mv.CharacterName, &mv.Level, &mv.MemberRole,
				&mv.WeeklyContribution, &mv.TotalContribution, &mv.JoinedAt,
			); err != nil {
				rows.Close()
				return snap, err
			}
			gv.Members = append(gv.Members, mv)
		}
		rows.Close()
		gv.MemberCount = len(gv.Members)
		snap.Guild = &gv
	} else if !errors.Is(err, pgx.ErrNoRows) {
		return snap, err
	}

	dirRows, err := tx.Query(ctx, `
		SELECT g.guild_id, g.guild_name::text, g.guild_tag::text, lc.character_name::text,
		       g.guild_level, COUNT(gm.character_id)::int AS member_count, g.member_cap, g.motd_announcement
		  FROM guilds g
		  JOIN characters lc ON lc.id = g.leader_char_id
		  LEFT JOIN guild_members gm ON gm.guild_id = g.guild_id
		 GROUP BY g.guild_id, lc.character_name
		 ORDER BY g.guild_level DESC, g.guild_exp DESC, g.created_at ASC
		 LIMIT 20
	`)
	if err != nil {
		return snap, err
	}
	defer dirRows.Close()
	for dirRows.Next() {
		var d GuildDirectoryEntry
		if err := dirRows.Scan(
			&d.GuildID, &d.GuildName, &d.GuildTag, &d.LeaderName,
			&d.GuildLevel, &d.MemberCount, &d.MemberCap, &d.MOTD,
		); err != nil {
			return snap, err
		}
		snap.Directory = append(snap.Directory, d)
	}
	return snap, dirRows.Err()
}

func (s *Service) GetGuildSnapshot(ctx context.Context, charID uuid.UUID) (GuildSnapshot, error) {
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return GuildSnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	snap, err := s.getSnapshotInTx(ctx, tx, charID, "")
	if err != nil {
		return GuildSnapshot{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return GuildSnapshot{}, err
	}
	return snap, nil
}

// CreateGuildFenced creates a new Guild (requires Level >= 2 and 200 Vàng debited via currency_ledger).
func (s *Service) CreateGuildFenced(
	ctx context.Context,
	charID uuid.UUID,
	fencingEpoch int64,
	serverID string,
	guildName string,
	guildTag string,
	motd string,
	idempotencyKey string,
) (GuildSnapshot, error) {
	cleanName, cleanTag, err := validateGuildNameAndTag(guildName, guildTag)
	if err != nil {
		return GuildSnapshot{}, err
	}
	motd = strings.TrimSpace(motd)
	if len([]rune(motd)) > 500 {
		motd = string([]rune(motd)[:500])
	}
	if motd == "" {
		motd = fmt.Sprintf("Chao mung den Bang hoi [%s] %s!", cleanTag, cleanName)
	}
	idemKey := strings.TrimSpace(idempotencyKey)
	if idemKey == "" {
		idemKey = fmt.Sprintf("guild-create:%s:%s", charID.String(), cleanTag)
	}

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return GuildSnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	charLv, err := lockCharacterFencedTx(ctx, tx, charID, fencingEpoch)
	if err != nil {
		return GuildSnapshot{}, err
	}

	// Idempotent replay check: if this idempotencyKey already executed on currency_ledger, return current snapshot!
	var prevLedgerID int64
	if err := tx.QueryRow(ctx, `SELECT ledger_id FROM currency_ledger WHERE idempotency_key = $1`, idemKey).Scan(&prevLedgerID); err == nil {
		snap, err := s.getSnapshotInTx(ctx, tx, charID, "Bang hoi da duoc khoi tao (idempotent)!")
		if err != nil {
			return GuildSnapshot{}, err
		}
		if err := tx.Commit(ctx); err != nil {
			return GuildSnapshot{}, err
		}
		return snap, nil
	}

	if charLv < GuildCreateMinLevel {
		return GuildSnapshot{}, ErrGuildLevelTooLow
	}

	var existingGuild uuid.UUID
	if err := tx.QueryRow(ctx, `SELECT guild_id FROM guild_members WHERE character_id = $1`, charID).Scan(&existingGuild); err == nil {
		return GuildSnapshot{}, ErrAlreadyInGuild
	}

	var guildID uuid.UUID
	err = tx.QueryRow(ctx, `
		INSERT INTO guilds (
			guild_name, guild_tag, leader_char_id, guild_level, guild_exp,
			member_cap, treasury_standard_coin, motd_announcement
		) VALUES ($1, $2, $3, 1, 0, 30, 0, $4)
		RETURNING guild_id
	`, cleanName, cleanTag, charID, motd).Scan(&guildID)
	if err != nil {
		var pgErr *pgconn.PgError
		if errors.As(err, &pgErr) && pgErr.Code == "23505" {
			return GuildSnapshot{}, ErrGuildNameOrTagTaken
		}
		return GuildSnapshot{}, fmt.Errorf("insert guild: %w", err)
	}

	if _, err := tx.Exec(ctx, `
		INSERT INTO guild_members (guild_id, character_id, member_role, weekly_contribution, total_contribution)
		VALUES ($1, $2, 'LEADER', 0, 0)
	`, guildID, charID); err != nil {
		return GuildSnapshot{}, fmt.Errorf("insert guild leader: %w", err)
	}

	if _, err := economy.ApplyCoinDeltaTx(
		ctx,
		tx,
		charID,
		fencingEpoch,
		serverID,
		-GuildCreateCoinFee,
		"GUILD_CREATE",
		"GUILD",
		guildID.String(),
		idemKey,
	); err != nil {
		return GuildSnapshot{}, err
	}

	snap, err := s.getSnapshotInTx(ctx, tx, charID, fmt.Sprintf("Da thanh lap Bang [%s] %s (-200 Vang)!", cleanTag, cleanName))
	if err != nil {
		return GuildSnapshot{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return GuildSnapshot{}, err
	}
	return snap, nil
}

// JoinGuildFenced enrolls a guildless character into target guildID up to member_cap.
func (s *Service) JoinGuildFenced(
	ctx context.Context,
	charID uuid.UUID,
	fencingEpoch int64,
	guildID uuid.UUID,
) (GuildSnapshot, error) {
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return GuildSnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, err := lockCharacterFencedTx(ctx, tx, charID, fencingEpoch); err != nil {
		return GuildSnapshot{}, err
	}

	var curGuild uuid.UUID
	if err := tx.QueryRow(ctx, `SELECT guild_id FROM guild_members WHERE character_id = $1`, charID).Scan(&curGuild); err == nil {
		return GuildSnapshot{}, ErrAlreadyInGuild
	}

	var gName, gTag string
	var memberCap int16
	err = tx.QueryRow(ctx, `
		SELECT guild_name::text, guild_tag::text, member_cap
		  FROM guilds
		 WHERE guild_id = $1
		   FOR UPDATE
	`, guildID).Scan(&gName, &gTag, &memberCap)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return GuildSnapshot{}, ErrGuildNotFound
		}
		return GuildSnapshot{}, err
	}

	var curCount int
	if err := tx.QueryRow(ctx, `SELECT COUNT(*) FROM guild_members WHERE guild_id = $1`, guildID).Scan(&curCount); err != nil {
		return GuildSnapshot{}, err
	}
	if curCount >= int(memberCap) {
		return GuildSnapshot{}, ErrGuildFull
	}

	if _, err := tx.Exec(ctx, `
		INSERT INTO guild_members (guild_id, character_id, member_role)
		VALUES ($1, $2, 'MEMBER')
	`, guildID, charID); err != nil {
		var pgErr *pgconn.PgError
		if errors.As(err, &pgErr) && pgErr.Code == "23505" {
			return GuildSnapshot{}, ErrAlreadyInGuild
		}
		return GuildSnapshot{}, err
	}

	snap, err := s.getSnapshotInTx(ctx, tx, charID, fmt.Sprintf("Da gia nhap Bang [%s] %s!", gTag, gName))
	if err != nil {
		return GuildSnapshot{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return GuildSnapshot{}, err
	}
	return snap, nil
}

// LeaveGuildFenced removes charID from their guild (or disbands the guild if charID is the sole LEADER).
func (s *Service) LeaveGuildFenced(
	ctx context.Context,
	charID uuid.UUID,
	fencingEpoch int64,
) (GuildSnapshot, error) {
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return GuildSnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, err := lockCharacterFencedTx(ctx, tx, charID, fencingEpoch); err != nil {
		return GuildSnapshot{}, err
	}

	var guildID uuid.UUID
	var role string
	err = tx.QueryRow(ctx, `
		SELECT guild_id, member_role FROM guild_members WHERE character_id = $1 FOR UPDATE
	`, charID).Scan(&guildID, &role)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return GuildSnapshot{}, ErrNotInGuild
		}
		return GuildSnapshot{}, err
	}

	if _, err := tx.Exec(ctx, `SELECT 1 FROM guilds WHERE guild_id = $1 FOR UPDATE`, guildID); err != nil {
		return GuildSnapshot{}, err
	}

	var memberCount int
	if err := tx.QueryRow(ctx, `SELECT COUNT(*) FROM guild_members WHERE guild_id = $1`, guildID).Scan(&memberCount); err != nil {
		return GuildSnapshot{}, err
	}

	var msg string
	if role == RoleLeader {
		if memberCount > 1 {
			return GuildSnapshot{}, ErrLeaderCannotLeave
		}
		if _, err := tx.Exec(ctx, `DELETE FROM guilds WHERE guild_id = $1`, guildID); err != nil {
			return GuildSnapshot{}, err
		}
		msg = "Da giai tan Bang hoi."
	} else {
		if _, err := tx.Exec(ctx, `DELETE FROM guild_members WHERE guild_id = $1 AND character_id = $2`, guildID, charID); err != nil {
			return GuildSnapshot{}, err
		}
		msg = "Da roi khoi Bang hoi."
	}

	snap, err := s.getSnapshotInTx(ctx, tx, charID, msg)
	if err != nil {
		return GuildSnapshot{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return GuildSnapshot{}, err
	}
	return snap, nil
}

// KickMemberFenced allows LEADER (or VICE_LEADER for lower ranks) to remove targetCharID from the guild.
func (s *Service) KickMemberFenced(
	ctx context.Context,
	actorCharID uuid.UUID,
	fencingEpoch int64,
	targetCharID uuid.UUID,
) (GuildSnapshot, error) {
	if actorCharID == targetCharID {
		return GuildSnapshot{}, ErrInsufficientGuildPerm
	}

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return GuildSnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, err := lockCharacterFencedTx(ctx, tx, actorCharID, fencingEpoch); err != nil {
		return GuildSnapshot{}, err
	}

	var guildID uuid.UUID
	var actorRole string
	if err := tx.QueryRow(ctx, `
		SELECT guild_id, member_role FROM guild_members WHERE character_id = $1 FOR UPDATE
	`, actorCharID).Scan(&guildID, &actorRole); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return GuildSnapshot{}, ErrNotInGuild
		}
		return GuildSnapshot{}, err
	}

	var targetRole string
	if err := tx.QueryRow(ctx, `
		SELECT member_role FROM guild_members WHERE guild_id = $1 AND character_id = $2 FOR UPDATE
	`, guildID, targetCharID).Scan(&targetRole); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return GuildSnapshot{}, ErrNotInGuild
		}
		return GuildSnapshot{}, err
	}

	allowed := (actorRole == RoleLeader) ||
		(actorRole == RoleViceLeader && (targetRole == RoleElite || targetRole == RoleMember))
	if !allowed {
		return GuildSnapshot{}, ErrInsufficientGuildPerm
	}

	if _, err := tx.Exec(ctx, `DELETE FROM guild_members WHERE guild_id = $1 AND character_id = $2`, guildID, targetCharID); err != nil {
		return GuildSnapshot{}, err
	}

	snap, err := s.getSnapshotInTx(ctx, tx, actorCharID, "Da truc xuat thanh vien khoi Bang hoi.")
	if err != nil {
		return GuildSnapshot{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return GuildSnapshot{}, err
	}
	return snap, nil
}

// SetMemberRoleFenced updates a member's role (or transfers LEADER atomically).
func (s *Service) SetMemberRoleFenced(
	ctx context.Context,
	leaderCharID uuid.UUID,
	fencingEpoch int64,
	targetCharID uuid.UUID,
	newRole string,
) (GuildSnapshot, error) {
	newRole = strings.ToUpper(strings.TrimSpace(newRole))
	switch newRole {
	case RoleLeader, RoleViceLeader, RoleElite, RoleMember:
	default:
		return GuildSnapshot{}, ErrInvalidRole
	}
	if leaderCharID == targetCharID {
		return GuildSnapshot{}, ErrInsufficientGuildPerm
	}

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return GuildSnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, err := lockCharacterFencedTx(ctx, tx, leaderCharID, fencingEpoch); err != nil {
		return GuildSnapshot{}, err
	}

	var guildID uuid.UUID
	var actorRole string
	if err := tx.QueryRow(ctx, `
		SELECT guild_id, member_role FROM guild_members WHERE character_id = $1 FOR UPDATE
	`, leaderCharID).Scan(&guildID, &actorRole); err != nil {
		return GuildSnapshot{}, ErrNotInGuild
	}
	if actorRole != RoleLeader {
		return GuildSnapshot{}, ErrInsufficientGuildPerm
	}

	var curTargetRole string
	if err := tx.QueryRow(ctx, `
		SELECT member_role FROM guild_members WHERE guild_id = $1 AND character_id = $2 FOR UPDATE
	`, guildID, targetCharID).Scan(&curTargetRole); err != nil {
		return GuildSnapshot{}, ErrNotInGuild
	}

	if newRole == RoleLeader {
		if _, err := tx.Exec(ctx, `UPDATE guilds SET leader_char_id = $2 WHERE guild_id = $1`, guildID, targetCharID); err != nil {
			return GuildSnapshot{}, err
		}
		if _, err := tx.Exec(ctx, `UPDATE guild_members SET member_role = 'VICE_LEADER' WHERE guild_id = $1 AND character_id = $2`, guildID, leaderCharID); err != nil {
			return GuildSnapshot{}, err
		}
		if _, err := tx.Exec(ctx, `UPDATE guild_members SET member_role = 'LEADER' WHERE guild_id = $1 AND character_id = $2`, guildID, targetCharID); err != nil {
			return GuildSnapshot{}, err
		}
	} else {
		if _, err := tx.Exec(ctx, `UPDATE guild_members SET member_role = $3 WHERE guild_id = $1 AND character_id = $2`, guildID, targetCharID, newRole); err != nil {
			return GuildSnapshot{}, err
		}
	}

	snap, err := s.getSnapshotInTx(ctx, tx, leaderCharID, fmt.Sprintf("Da cap nhat chuc vu: %s!", newRole))
	if err != nil {
		return GuildSnapshot{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return GuildSnapshot{}, err
	}
	return snap, nil
}

// UpdateMOTDFenced updates the guild's announcement banner (LEADER or VICE_LEADER).
func (s *Service) UpdateMOTDFenced(
	ctx context.Context,
	actorCharID uuid.UUID,
	fencingEpoch int64,
	motd string,
) (GuildSnapshot, error) {
	motd = strings.TrimSpace(motd)
	if len([]rune(motd)) > 500 {
		motd = string([]rune(motd)[:500])
	}

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return GuildSnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, err := lockCharacterFencedTx(ctx, tx, actorCharID, fencingEpoch); err != nil {
		return GuildSnapshot{}, err
	}

	var guildID uuid.UUID
	var role string
	if err := tx.QueryRow(ctx, `
		SELECT guild_id, member_role FROM guild_members WHERE character_id = $1
	`, actorCharID).Scan(&guildID, &role); err != nil {
		return GuildSnapshot{}, ErrNotInGuild
	}
	if role != RoleLeader && role != RoleViceLeader {
		return GuildSnapshot{}, ErrInsufficientGuildPerm
	}

	if _, err := tx.Exec(ctx, `UPDATE guilds SET motd_announcement = $2 WHERE guild_id = $1`, guildID, motd); err != nil {
		return GuildSnapshot{}, err
	}

	snap, err := s.getSnapshotInTx(ctx, tx, actorCharID, "Da cap nhat thong bao Bang hoi!")
	if err != nil {
		return GuildSnapshot{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return GuildSnapshot{}, err
	}
	return snap, nil
}

// DonateCoinsFenced atomically debits Vàng from character_wallets (via currency_ledger GUILD_DONATE)
// and credits guilds.treasury_standard_coin + guild_exp + member contributions.
func (s *Service) DonateCoinsFenced(
	ctx context.Context,
	charID uuid.UUID,
	fencingEpoch int64,
	serverID string,
	amountCoin int64,
	idempotencyKey string,
) (GuildSnapshot, error) {
	if amountCoin <= 0 {
		return GuildSnapshot{}, ErrInvalidDonateAmount
	}
	idemKey := strings.TrimSpace(idempotencyKey)
	if idemKey == "" {
		idemKey = fmt.Sprintf("guild-donate:%s:%d", charID.String(), time.Now().UnixNano())
	}

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return GuildSnapshot{}, err
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, err := lockCharacterFencedTx(ctx, tx, charID, fencingEpoch); err != nil {
		return GuildSnapshot{}, err
	}

	var guildID uuid.UUID
	if err := tx.QueryRow(ctx, `
		SELECT guild_id FROM guild_members WHERE character_id = $1 FOR UPDATE
	`, charID).Scan(&guildID); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return GuildSnapshot{}, ErrNotInGuild
		}
		return GuildSnapshot{}, err
	}

	if _, err := tx.Exec(ctx, `SELECT 1 FROM guilds WHERE guild_id = $1 FOR UPDATE`, guildID); err != nil {
		return GuildSnapshot{}, err
	}

	// Idempotent check before incrementing guild treasury so duplicate replays never double-credit treasury!
	var existingLedgerID int64
	if err := tx.QueryRow(ctx, `SELECT ledger_id FROM currency_ledger WHERE idempotency_key = $1`, idemKey).Scan(&existingLedgerID); err == nil {
		snap, err := s.getSnapshotInTx(ctx, tx, charID, "Da quyen gop Vang vao Quy Bang (idempotent)!")
		if err != nil {
			return GuildSnapshot{}, err
		}
		if err := tx.Commit(ctx); err != nil {
			return GuildSnapshot{}, err
		}
		return snap, nil
	}

	if _, err := economy.ApplyCoinDeltaTx(
		ctx,
		tx,
		charID,
		fencingEpoch,
		serverID,
		-amountCoin,
		"GUILD_DONATE",
		"GUILD",
		guildID.String(),
		idemKey,
	); err != nil {
		return GuildSnapshot{}, err
	}

	if _, err := tx.Exec(ctx, `
		UPDATE guilds
		   SET treasury_standard_coin = treasury_standard_coin + $2,
		       guild_exp = guild_exp + $2,
		       guild_level = LEAST(50, 1 + ((guild_exp + $2) / 1000))::smallint
		 WHERE guild_id = $1
	`, guildID, amountCoin); err != nil {
		return GuildSnapshot{}, err
	}

	if _, err := tx.Exec(ctx, `
		UPDATE guild_members
		   SET weekly_contribution = weekly_contribution + $3,
		       total_contribution = total_contribution + $3
		 WHERE guild_id = $1 AND character_id = $2
	`, guildID, charID, amountCoin); err != nil {
		return GuildSnapshot{}, err
	}

	snap, err := s.getSnapshotInTx(ctx, tx, charID, fmt.Sprintf("Da gop +%d Vang vao Quy Bang!", amountCoin))
	if err != nil {
		return GuildSnapshot{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return GuildSnapshot{}, err
	}
	return snap, nil
}
