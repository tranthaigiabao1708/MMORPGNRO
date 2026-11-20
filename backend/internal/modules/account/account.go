package account

import (
	"context"
	"crypto/rand"
	"crypto/subtle"
	"encoding/base64"
	"errors"
	"fmt"
	"net/netip"
	"strings"
	"time"

	"github.com/google/uuid"
	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgconn"
	"github.com/jackc/pgx/v5/pgxpool"
	"golang.org/x/crypto/argon2"
)

var (
	ErrAccountAlreadyExists = errors.New("account username or email already exists")
	ErrInvalidCredentials   = errors.New("invalid username or password")
	ErrAccountNotActive     = errors.New("account is suspended or banned")
	ErrInvalidAccountInput  = errors.New("invalid username, email, or password format")
)

const (
	argonTime    uint32 = 2
	argonMemory  uint32 = 64 * 1024 // 64 MB per spec 04_MULTIPLAYER_BACKEND/02
	argonThreads uint8  = 2
	argonKeyLen  uint32 = 32
	argonSaltLen        = 16
)

// Account represents a row in the PostgreSQL accounts table.
type Account struct {
	ID                uuid.UUID
	Username          string
	Email             string
	AccountStatus     string
	MaxCharacterSlots int16
	CreatedAt         time.Time
}

// Service manages exclusive access to the PostgreSQL accounts table.
type Service struct {
	pool *pgxpool.Pool
}

func NewService(pool *pgxpool.Pool) *Service {
	return &Service{pool: pool}
}

// HashPasswordArgon2id produces a standard PHC-formatted Argon2id string.
func HashPasswordArgon2id(password string) (string, error) {
	salt := make([]byte, argonSaltLen)
	if _, err := rand.Read(salt); err != nil {
		return "", fmt.Errorf("generate salt: %w", err)
	}
	hash := argon2.IDKey([]byte(password), salt, argonTime, argonMemory, argonThreads, argonKeyLen)
	b64Salt := base64.RawStdEncoding.EncodeToString(salt)
	b64Hash := base64.RawStdEncoding.EncodeToString(hash)
	return fmt.Sprintf("$argon2id$v=%d$m=%d,t=%d,p=%d$%s$%s",
		argon2.Version, argonMemory, argonTime, argonThreads, b64Salt, b64Hash), nil
}

// VerifyPasswordArgon2id verifies a plaintext password against a PHC Argon2id hash in constant time.
func VerifyPasswordArgon2id(password, encodedHash string) (bool, error) {
	parts := strings.Split(encodedHash, "$")
	if len(parts) != 6 || parts[1] != "argon2id" {
		return false, fmt.Errorf("invalid argon2id PHC format")
	}

	var version int
	if _, err := fmt.Sscanf(parts[2], "v=%d", &version); err != nil {
		return false, fmt.Errorf("parse argon2 version: %w", err)
	}

	var memory, timeCost uint32
	var threads uint8
	if _, err := fmt.Sscanf(parts[3], "m=%d,t=%d,p=%d", &memory, &timeCost, &threads); err != nil {
		return false, fmt.Errorf("parse argon2 params: %w", err)
	}

	salt, err := base64.RawStdEncoding.DecodeString(parts[4])
	if err != nil {
		return false, fmt.Errorf("decode salt: %w", err)
	}
	expectedHash, err := base64.RawStdEncoding.DecodeString(parts[5])
	if err != nil {
		return false, fmt.Errorf("decode hash: %w", err)
	}

	computed := argon2.IDKey([]byte(password), salt, timeCost, memory, threads, uint32(len(expectedHash)))
	return subtle.ConstantTimeCompare(computed, expectedHash) == 1, nil
}

// Register creates a new ACTIVE account with an Argon2id password hash.
func (s *Service) Register(ctx context.Context, username, email, password string) (Account, error) {
	username = strings.TrimSpace(username)
	email = strings.TrimSpace(email)
	if len(username) < 3 || len(username) > 32 || !strings.Contains(email, "@") || len(password) < 6 || len(password) > 128 {
		return Account{}, ErrInvalidAccountInput
	}

	pwHash, err := HashPasswordArgon2id(password)
	if err != nil {
		return Account{}, err
	}

	var acc Account
	err = s.pool.QueryRow(ctx, `
		INSERT INTO accounts (username, email, password_hash, account_status)
		VALUES ($1, $2, $3, 'ACTIVE')
		RETURNING id, username, email, account_status, max_character_slots, created_at
	`, username, email, pwHash).Scan(
		&acc.ID, &acc.Username, &acc.Email, &acc.AccountStatus, &acc.MaxCharacterSlots, &acc.CreatedAt,
	)
	if err != nil {
		var pgErr *pgconn.PgError
		if errors.As(err, &pgErr) && pgErr.Code == "23505" {
			return Account{}, ErrAccountAlreadyExists
		}
		return Account{}, fmt.Errorf("insert account: %w", err)
	}
	return acc, nil
}

// Authenticate verifies username + Argon2id password and updates last_login_at.
func (s *Service) Authenticate(ctx context.Context, username, password, clientIP string) (Account, error) {
	username = strings.TrimSpace(username)
	if username == "" || password == "" {
		return Account{}, ErrInvalidCredentials
	}

	var acc Account
	var pwHash string
	err := s.pool.QueryRow(ctx, `
		SELECT id, username, email, password_hash, account_status, max_character_slots, created_at
		  FROM accounts
		 WHERE username = $1
	`, username).Scan(
		&acc.ID, &acc.Username, &acc.Email, &pwHash, &acc.AccountStatus, &acc.MaxCharacterSlots, &acc.CreatedAt,
	)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return Account{}, ErrInvalidCredentials
		}
		return Account{}, fmt.Errorf("query account: %w", err)
	}

	ok, err := VerifyPasswordArgon2id(password, pwHash)
	if err != nil || !ok {
		return Account{}, ErrInvalidCredentials
	}

	if acc.AccountStatus != "ACTIVE" {
		return Account{}, ErrAccountNotActive
	}

	var ipPtr *netip.Addr
	if parsedIP, parseErr := netip.ParseAddr(strings.TrimSpace(clientIP)); parseErr == nil {
		ipPtr = &parsedIP
	}
	_, _ = s.pool.Exec(ctx, `UPDATE accounts SET last_login_at = NOW(), last_login_ip = $2 WHERE id = $1`, acc.ID, ipPtr)
	return acc, nil
}

// GetByID fetches an account by UUID.
func (s *Service) GetByID(ctx context.Context, accountID uuid.UUID) (Account, error) {
	var acc Account
	err := s.pool.QueryRow(ctx, `
		SELECT id, username, email, account_status, max_character_slots, created_at
		  FROM accounts
		 WHERE id = $1
	`, accountID).Scan(
		&acc.ID, &acc.Username, &acc.Email, &acc.AccountStatus, &acc.MaxCharacterSlots, &acc.CreatedAt,
	)
	if err != nil {
		return Account{}, err
	}
	return acc, nil
}
