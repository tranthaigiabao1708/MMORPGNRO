package economy

import (
	"context"
	"errors"
	"fmt"
	"strings"

	"github.com/google/uuid"
	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

const (
	CurrencyStandardCoin = "currency.standard_coin"
	CurrencyMasteryPoint = "currency.mastery_point"
)

var (
	ErrInsufficientCoins = errors.New("insufficient Vàng (standard_coin) balance")
	ErrInvalidCoinDelta  = errors.New("coin delta must be non-zero and fencing_epoch >= 1")
)

type WalletState struct {
	CharacterID        uuid.UUID `json:"character_id"`
	StandardCoin       int64     `json:"standard_coin"`
	MasteryPoint       int64     `json:"mastery_point"`
	LifetimeCoinEarned int64     `json:"lifetime_coin_earned"`
	LifetimeCoinSpent  int64     `json:"lifetime_coin_spent"`
}

type Service struct {
	pool *pgxpool.Pool
}

func NewService(pool *pgxpool.Pool) *Service {
	return &Service{pool: pool}
}

// EnsureWalletTx guarantees a character_wallets row exists for characterID (initialized at 0 Vàng).
func EnsureWalletTx(ctx context.Context, tx pgx.Tx, characterID uuid.UUID) error {
	_, err := tx.Exec(ctx, `
		INSERT INTO character_wallets (character_id, standard_coin, mastery_point, lifetime_coin_earned, lifetime_coin_spent)
		VALUES ($1, 0, 0, 0, 0)
		ON CONFLICT (character_id) DO NOTHING
	`, characterID)
	return err
}

// GetWallet reads a character's current wallet state (returning 0s if not yet initialized).
func (s *Service) GetWallet(ctx context.Context, characterID uuid.UUID) (WalletState, error) {
	st := WalletState{CharacterID: characterID}
	err := s.pool.QueryRow(ctx, `
		SELECT standard_coin, mastery_point, lifetime_coin_earned, lifetime_coin_spent
		  FROM character_wallets
		 WHERE character_id = $1
	`, characterID).Scan(&st.StandardCoin, &st.MasteryPoint, &st.LifetimeCoinEarned, &st.LifetimeCoinSpent)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return st, nil
		}
		return st, err
	}
	return st, nil
}

// ApplyCoinDeltaTx atomically mutates character_wallets.standard_coin AND appends an immutable audit row
// to currency_ledger within the caller's ACID transaction.
// Enforces:
//   - idempotency_key deduplication on currency_ledger
//   - non-negative balance (ErrInsufficientCoins if balance + delta < 0)
//   - strict double-entry invariant: standard_coin == COALESCE(SUM(currency_ledger.delta_amount), 0)
func ApplyCoinDeltaTx(
	ctx context.Context,
	tx pgx.Tx,
	characterID uuid.UUID,
	fencingEpoch int64,
	serverID string,
	delta int64,
	reasonCode string,
	refType string,
	refID string,
	idempotencyKey string,
) (int64, error) {
	if fencingEpoch < 1 || delta == 0 {
		return 0, ErrInvalidCoinDelta
	}
	idemKey := strings.TrimSpace(idempotencyKey)
	if idemKey == "" {
		return 0, errors.New("idempotency_key is required for currency_ledger mutation")
	}
	if strings.TrimSpace(serverID) == "" {
		serverID = "ue5-ded-7777"
	}

	if err := EnsureWalletTx(ctx, tx, characterID); err != nil {
		return 0, fmt.Errorf("ensure wallet: %w", err)
	}

	// Lock wallet row first so concurrent callers with identical or distinct idempotency keys serialize cleanly!
	var curBalance int64
	if err := tx.QueryRow(ctx, `
		SELECT standard_coin FROM character_wallets WHERE character_id = $1 FOR UPDATE
	`, characterID).Scan(&curBalance); err != nil {
		return 0, fmt.Errorf("lock wallet: %w", err)
	}

	// Check if this idempotency_key was already recorded on currency_ledger
	var existingBalanceAfter int64
	err := tx.QueryRow(ctx, `
		SELECT balance_after FROM currency_ledger WHERE idempotency_key = $1
	`, idemKey).Scan(&existingBalanceAfter)
	if err == nil {
		return existingBalanceAfter, nil
	}
	if !errors.Is(err, pgx.ErrNoRows) {
		return 0, fmt.Errorf("check ledger idempotency: %w", err)
	}

	newBalance := curBalance + delta
	if newBalance < 0 {
		return curBalance, ErrInsufficientCoins
	}

	var earnedAdd, spentAdd int64
	if delta > 0 {
		earnedAdd = delta
	} else {
		spentAdd = -delta
	}

	if _, err := tx.Exec(ctx, `
		UPDATE character_wallets
		   SET standard_coin = $2,
		       lifetime_coin_earned = lifetime_coin_earned + $3,
		       lifetime_coin_spent = lifetime_coin_spent + $4,
		       updated_at = NOW()
		 WHERE character_id = $1
	`, characterID, newBalance, earnedAdd, spentAdd); err != nil {
		return 0, fmt.Errorf("update character_wallets: %w", err)
	}

	var refTypePtr, refIDPtr *string
	if strings.TrimSpace(refType) != "" {
		rt := strings.TrimSpace(refType)
		refTypePtr = &rt
	}
	if strings.TrimSpace(refID) != "" {
		ri := strings.TrimSpace(refID)
		refIDPtr = &ri
	}

	if _, err := tx.Exec(ctx, `
		INSERT INTO currency_ledger (
			idempotency_key, character_id, currency_type, delta_amount, balance_after,
			reason_code, reference_type, reference_id, fencing_epoch, server_id
		) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)
	`, idemKey, characterID, CurrencyStandardCoin, delta, newBalance,
		reasonCode, refTypePtr, refIDPtr, fencingEpoch, serverID); err != nil {
		return 0, fmt.Errorf("insert currency_ledger: %w", err)
	}

	return newBalance, nil
}

// VerifyLedgerParity checks that character_wallets.standard_coin equals SUM(delta_amount) in currency_ledger.
func (s *Service) VerifyLedgerParity(ctx context.Context, characterID uuid.UUID) (walletCoin int64, ledgerSum int64, err error) {
	w, err := s.GetWallet(ctx, characterID)
	if err != nil {
		return 0, 0, err
	}
	err = s.pool.QueryRow(ctx, `
		SELECT COALESCE(SUM(delta_amount), 0)
		  FROM currency_ledger
		 WHERE character_id = $1 AND currency_type = $2
	`, characterID, CurrencyStandardCoin).Scan(&ledgerSum)
	return w.StandardCoin, ledgerSum, err
}
