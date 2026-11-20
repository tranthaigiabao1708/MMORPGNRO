package postgres

import (
	"context"
	"errors"
	"fmt"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgconn"
	"github.com/jackc/pgx/v5/pgxpool"
)

type txContextKey struct{}

// DBTX abstracts *pgxpool.Pool and pgx.Tx for repository queries.
type DBTX interface {
	Exec(ctx context.Context, sql string, arguments ...any) (pgconn.CommandTag, error)
	Query(ctx context.Context, sql string, args ...any) (pgx.Rows, error)
	QueryRow(ctx context.Context, sql string, args ...any) pgx.Row
}

// TxObserver records database transaction durations and terminal statuses.
type TxObserver interface {
	ObserveTxDuration(txName string, status string, duration time.Duration)
}

// TxManager manages ACID PostgreSQL transactions across module boundaries via context.Context.
type TxManager struct {
	pool     *pgxpool.Pool
	observer TxObserver
}

// NewTxManager constructs a TxManager backed by pool and an optional TxObserver.
func NewTxManager(pool *pgxpool.Pool, observer TxObserver) *TxManager {
	return &TxManager{
		pool:     pool,
		observer: observer,
	}
}

// Conn returns the active pgx.Tx in ctx if inside WithinTx, otherwise the underlying *pgxpool.Pool.
func (m *TxManager) Conn(ctx context.Context) DBTX {
	if tx, ok := ctx.Value(txContextKey{}).(pgx.Tx); ok && tx != nil {
		return tx
	}
	return m.pool
}

// WithinTx executes fn inside a ReadCommitted PostgreSQL transaction.
// Nested calls reuse the existing pgx.Tx stored in ctx.
func (m *TxManager) WithinTx(ctx context.Context, txName string, fn func(txCtx context.Context) error) (err error) {
	if _, ok := ctx.Value(txContextKey{}).(pgx.Tx); ok {
		return fn(ctx)
	}

	start := time.Now()
	status := "committed"
	defer func() {
		if m.observer != nil {
			m.observer.ObserveTxDuration(txName, status, time.Since(start))
		}
	}()

	tx, err := m.pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		status = "begin_error"
		return fmt.Errorf("begin tx %s: %w", txName, err)
	}

	defer func() {
		if p := recover(); p != nil {
			status = "panic_rollback"
			_ = tx.Rollback(context.Background())
			panic(p)
		}
	}()

	txCtx := context.WithValue(ctx, txContextKey{}, tx)
	if err = fn(txCtx); err != nil {
		status = "rolled_back"
		rbCtx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()
		if rbErr := tx.Rollback(rbCtx); rbErr != nil && !errors.Is(rbErr, pgx.ErrTxClosed) {
			return fmt.Errorf("tx %s failed (%w) and rollback failed: %v", txName, err, rbErr)
		}
		return err
	}

	if err = tx.Commit(ctx); err != nil {
		status = "commit_error"
		return fmt.Errorf("commit tx %s: %w", txName, err)
	}
	return nil
}
