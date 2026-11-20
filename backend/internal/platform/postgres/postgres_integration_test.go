package postgres_test

import (
	"context"
	"errors"
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/platform/postgres"
	"github.com/jackc/pgx/v5/pgconn"
	"github.com/jackc/pgx/v5/pgxpool"
	"github.com/testcontainers/testcontainers-go"
	tcpostgres "github.com/testcontainers/testcontainers-go/modules/postgres"
	"github.com/testcontainers/testcontainers-go/wait"
)

var (
	basePGDSN string
	dbSeq     atomic.Uint64
)

func TestMain(m *testing.M) {
	os.Exit(runTests(m))
}

func runTests(m *testing.M) int {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
	defer cancel()

	if dsn := strings.TrimSpace(os.Getenv("IU_TEST_PG_DSN")); dsn != "" {
		basePGDSN = dsn
		return m.Run()
	}

	pgCtr, err := tcpostgres.Run(
		ctx,
		"postgres:16.4-alpine",
		tcpostgres.WithDatabase("infinite_universe"),
		tcpostgres.WithUsername("iu_admin"),
		tcpostgres.WithPassword("iu_dev_secret_2026"),
		testcontainers.WithWaitStrategy(
			wait.ForLog("database system is ready to accept connections").
				WithOccurrence(2).
				WithStartupTimeout(60*time.Second),
		),
	)
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to start postgres:16.4-alpine testcontainer: %v\n", err)
		return 1
	}
	defer func() {
		termCtx, termCancel := context.WithTimeout(context.Background(), 30*time.Second)
		defer termCancel()
		if err := pgCtr.Terminate(termCtx); err != nil {
			fmt.Fprintf(os.Stderr, "failed to terminate postgres testcontainer: %v\n", err)
		}
	}()

	dsn, err := pgCtr.ConnectionString(ctx, "sslmode=disable")
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to get testcontainer connection string: %v\n", err)
		return 1
	}
	basePGDSN = dsn

	return m.Run()
}

func migrationsDir(t *testing.T) string {
	t.Helper()
	_, file, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatal("failed to resolve caller path")
	}
	return filepath.Clean(filepath.Join(filepath.Dir(file), "..", "..", "..", "migrations"))
}

func readMigrationFile(t *testing.T, name string) string {
	t.Helper()
	path := filepath.Join(migrationsDir(t), name)
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read migration %s: %v", path, err)
	}
	return string(raw)
}

func replaceDBInDSN(t *testing.T, dsn, dbName string) string {
	t.Helper()
	u, err := url.Parse(dsn)
	if err != nil {
		t.Fatalf("parse DSN: %v", err)
	}
	u.Path = "/" + dbName
	return u.String()
}

func openIsolatedPG(t *testing.T) *pgxpool.Pool {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	adminPool, err := postgres.NewPool(ctx, basePGDSN)
	if err != nil {
		t.Fatalf("connect admin postgres: %v", err)
	}

	dbName := fmt.Sprintf("iu_test_%d_%d", time.Now().UnixNano(), dbSeq.Add(1))
	if _, err := adminPool.Exec(ctx, fmt.Sprintf(`CREATE DATABASE %s`, dbName)); err != nil {
		adminPool.Close()
		t.Fatalf("create isolated test database %s: %v", dbName, err)
	}

	testDSN := replaceDBInDSN(t, basePGDSN, dbName)
	testPool, err := postgres.NewPool(ctx, testDSN)
	if err != nil {
		_, _ = adminPool.Exec(context.Background(), fmt.Sprintf(`DROP DATABASE IF EXISTS %s WITH (FORCE)`, dbName))
		adminPool.Close()
		t.Fatalf("connect isolated database %s: %v", dbName, err)
	}

	t.Cleanup(func() {
		testPool.Close()
		dropCtx, dropCancel := context.WithTimeout(context.Background(), 15*time.Second)
		defer dropCancel()
		_, _ = adminPool.Exec(dropCtx, fmt.Sprintf(`DROP DATABASE IF EXISTS %s WITH (FORCE)`, dbName))
		adminPool.Close()
	})

	if os.Getenv("IU_TEST_PG_DSN") == "" {
		var versionNum int
		if err := testPool.QueryRow(ctx, `SELECT current_setting('server_version_num')::int`).Scan(&versionNum); err != nil {
			t.Fatalf("query server_version_num: %v", err)
		}
		if versionNum < 160000 || versionNum >= 170000 {
			t.Fatalf("expected PostgreSQL 16.x, got server_version_num=%d", versionNum)
		}
	}

	return testPool
}

func applyUpMigration(t *testing.T, pool *pgxpool.Pool) {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	sql := readMigrationFile(t, "000001_init_core_schema.up.sql")
	if _, err := pool.Exec(ctx, sql); err != nil {
		t.Fatalf("apply up migration: %v", err)
	}
}

func seedAccountAndCharacter(t *testing.T, pool *pgxpool.Pool, suffix string) (uuid.UUID, uuid.UUID) {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	var accountID uuid.UUID
	err := pool.QueryRow(
		ctx,
		`INSERT INTO accounts (username, email, password_hash)
		 VALUES ($1, $2, $3)
		 RETURNING id`,
		"user_"+suffix,
		"user_"+suffix+"@example.com",
		"$argon2id$v=19$m=65536,t=3,p=4$dGVzdHNhbHQ$dGVzdGhhc2g",
	).Scan(&accountID)
	if err != nil {
		t.Fatalf("insert account: %v", err)
	}

	var charID uuid.UUID
	err = pool.QueryRow(
		ctx,
		`INSERT INTO characters (account_id, character_name, race_id)
		 VALUES ($1, $2, 'race.zenith_warrior')
		 RETURNING id`,
		accountID,
		"char_"+suffix,
	).Scan(&charID)
	if err != nil {
		t.Fatalf("insert character: %v", err)
	}

	return accountID, charID
}

func TestCoreSchemaMigrationRoundTrip(t *testing.T) {
	pool := openIsolatedPG(t)
	ctx := context.Background()

	expectedTables := []string{
		"accounts",
		"boss_kill_history",
		"boss_pity_tokens",
		"character_attributes",
		"character_equipment",
		"character_potential",
		"character_quests",
		"character_save_snapshots",
		"character_skills",
		"character_transformations",
		"character_wallets",
		"characters",
		"currency_ledger",
		"guild_members",
		"guilds",
		"inventory_items",
		"item_definitions",
		"loot_history",
		"trade_transactions",
	}

	upSQL := readMigrationFile(t, "000001_init_core_schema.up.sql")
	downSQL := readMigrationFile(t, "000001_init_core_schema.down.sql")

	// 1. Up
	if _, err := pool.Exec(ctx, upSQL); err != nil {
		t.Fatalf("first up migration failed: %v", err)
	}

	listTables := func() []string {
		rows, err := pool.Query(ctx, `
			SELECT table_name
			FROM information_schema.tables
			WHERE table_schema = 'public' AND table_type = 'BASE TABLE'
			ORDER BY table_name
		`)
		if err != nil {
			t.Fatalf("query public tables: %v", err)
		}
		defer rows.Close()

		var tables []string
		for rows.Next() {
			var name string
			if err := rows.Scan(&name); err != nil {
				t.Fatalf("scan table name: %v", err)
			}
			tables = append(tables, name)
		}
		return tables
	}

	tables := listTables()
	if len(tables) != 19 {
		t.Fatalf("expected 19 tables, got %d: %v", len(tables), tables)
	}
	sort.Strings(expectedTables)
	for i := range expectedTables {
		if tables[i] != expectedTables[i] {
			t.Fatalf("table mismatch at %d: got %q, want %q", i, tables[i], expectedTables[i])
		}
	}

	var fnCount int
	err := pool.QueryRow(ctx, `
		SELECT count(*)
		FROM pg_proc p
		JOIN pg_namespace n ON n.oid = p.pronamespace
		WHERE n.nspname = 'public'
		  AND p.proname IN ('fn_touch_updated_at', 'fn_prevent_ledger_mutation')
	`).Scan(&fnCount)
	if err != nil {
		t.Fatalf("count functions: %v", err)
	}
	if fnCount != 2 {
		t.Fatalf("expected 2 trigger functions, got %d", fnCount)
	}

	var trigCount int
	err = pool.QueryRow(ctx, `
		SELECT count(DISTINCT trigger_name)
		FROM information_schema.triggers
		WHERE trigger_schema = 'public'
	`).Scan(&trigCount)
	if err != nil {
		t.Fatalf("count triggers: %v", err)
	}
	if trigCount != 12 {
		t.Fatalf("expected 12 distinct triggers, got %d", trigCount)
	}

	var idxCount int
	err = pool.QueryRow(ctx, `
		SELECT count(*)
		FROM pg_indexes
		WHERE schemaname = 'public'
		  AND indexname LIKE 'idx_%'
	`).Scan(&idxCount)
	if err != nil {
		t.Fatalf("count explicit indexes: %v", err)
	}
	if idxCount != 18 {
		t.Fatalf("expected 18 explicit idx_* indexes, got %d", idxCount)
	}

	// 2. Down (without CASCADE)
	if _, err := pool.Exec(ctx, downSQL); err != nil {
		t.Fatalf("down migration failed: %v", err)
	}

	if leftover := listTables(); len(leftover) != 0 {
		t.Fatalf("expected 0 tables after down, got %v", leftover)
	}

	err = pool.QueryRow(ctx, `
		SELECT count(*)
		FROM pg_proc p
		JOIN pg_namespace n ON n.oid = p.pronamespace
		WHERE n.nspname = 'public'
		  AND p.proname IN ('fn_touch_updated_at', 'fn_prevent_ledger_mutation')
	`).Scan(&fnCount)
	if err != nil {
		t.Fatalf("count leftover functions: %v", err)
	}
	if fnCount != 0 {
		t.Fatalf("expected 0 custom functions after down, got %d", fnCount)
	}

	// 3. Re-Up (idempotent round-trip)
	if _, err := pool.Exec(ctx, upSQL); err != nil {
		t.Fatalf("second up migration failed: %v", err)
	}
	if recreated := listTables(); len(recreated) != 19 {
		t.Fatalf("expected 19 tables after second up, got %d", len(recreated))
	}
}

func TestCharactersFencingEpochDefaultAndCheck(t *testing.T) {
	pool := openIsolatedPG(t)
	applyUpMigration(t, pool)
	ctx := context.Background()

	accountID, charID := seedAccountAndCharacter(t, pool, "epoch1")

	var epoch int64
	if err := pool.QueryRow(ctx, `SELECT fencing_epoch FROM characters WHERE id = $1`, charID).Scan(&epoch); err != nil {
		t.Fatalf("select fencing_epoch: %v", err)
	}
	if epoch != 0 {
		t.Fatalf("expected default characters.fencing_epoch = 0, got %d", epoch)
	}

	// Negative fencing_epoch on INSERT must be rejected with SQLSTATE 23514.
	_, err := pool.Exec(ctx, `
		INSERT INTO characters (account_id, character_name, race_id, fencing_epoch)
		VALUES ($1, 'char_neg_epoch', 'race.zenith_warrior', -1)
	`, accountID)
	var pgErr *pgconn.PgError
	if !errors.As(err, &pgErr) || pgErr.Code != "23514" {
		t.Fatalf("expected SQLSTATE 23514 for negative fencing_epoch insert, got %v", err)
	}

	// Negative fencing_epoch on UPDATE must also be rejected with SQLSTATE 23514.
	_, err = pool.Exec(ctx, `UPDATE characters SET fencing_epoch = -1 WHERE id = $1`, charID)
	if !errors.As(err, &pgErr) || pgErr.Code != "23514" {
		t.Fatalf("expected SQLSTATE 23514 for negative fencing_epoch update, got %v", err)
	}

	// Acquiring first lease transitions epoch 0 -> 1.
	if _, err := pool.Exec(ctx, `UPDATE characters SET fencing_epoch = 1 WHERE id = $1`, charID); err != nil {
		t.Fatalf("expected epoch 0 -> 1 update to succeed: %v", err)
	}

	// currency_ledger must still require fencing_epoch >= 1.
	_, err = pool.Exec(ctx, `
		INSERT INTO currency_ledger (
			idempotency_key, character_id, currency_type, delta_amount, balance_after, reason_code, fencing_epoch, server_id
		) VALUES ('idem-zero-epoch', $1, 'currency.standard_coin', 10, 10, 'MOB_DROP', 0, 'zone-test-1')
	`, charID)
	if !errors.As(err, &pgErr) || pgErr.Code != "23514" {
		t.Fatalf("expected SQLSTATE 23514 for currency_ledger fencing_epoch=0, got %v", err)
	}
}

func TestCharacterWalletsNegativeCoinRejected(t *testing.T) {
	pool := openIsolatedPG(t)
	applyUpMigration(t, pool)
	ctx := context.Background()

	_, charID := seedAccountAndCharacter(t, pool, "wallet1")
	_, charID2 := seedAccountAndCharacter(t, pool, "wallet2")

	if _, err := pool.Exec(ctx, `
		INSERT INTO character_wallets (character_id, standard_coin) VALUES ($1, 100)
	`, charID); err != nil {
		t.Fatalf("insert wallet: %v", err)
	}

	_, err := pool.Exec(ctx, `
		UPDATE character_wallets SET standard_coin = standard_coin - 150 WHERE character_id = $1
	`, charID)
	var pgErr *pgconn.PgError
	if !errors.As(err, &pgErr) || pgErr.Code != "23514" {
		t.Fatalf("expected SQLSTATE 23514 on negative balance update, got %v", err)
	}

	var balance int64
	if err := pool.QueryRow(ctx, `SELECT standard_coin FROM character_wallets WHERE character_id = $1`, charID).Scan(&balance); err != nil {
		t.Fatalf("query balance: %v", err)
	}
	if balance != 100 {
		t.Fatalf("expected balance to remain 100 after rejected update, got %d", balance)
	}

	_, err = pool.Exec(ctx, `
		INSERT INTO character_wallets (character_id, standard_coin) VALUES ($1, -1)
	`, charID2)
	if !errors.As(err, &pgErr) || pgErr.Code != "23514" {
		t.Fatalf("expected SQLSTATE 23514 on negative balance insert, got %v", err)
	}

	if _, err := pool.Exec(ctx, `
		UPDATE character_wallets SET standard_coin = standard_coin - 100 WHERE character_id = $1
	`, charID); err != nil {
		t.Fatalf("expected exact drain to 0 to succeed: %v", err)
	}
	if err := pool.QueryRow(ctx, `SELECT standard_coin FROM character_wallets WHERE character_id = $1`, charID).Scan(&balance); err != nil {
		t.Fatalf("query drained balance: %v", err)
	}
	if balance != 0 {
		t.Fatalf("expected balance 0, got %d", balance)
	}
}

func TestCurrencyLedgerImmutabilityTrigger(t *testing.T) {
	pool := openIsolatedPG(t)
	applyUpMigration(t, pool)
	ctx := context.Background()

	_, charID := seedAccountAndCharacter(t, pool, "ledger1")

	var ledgerID int64
	err := pool.QueryRow(ctx, `
		INSERT INTO currency_ledger (
			idempotency_key, character_id, currency_type, delta_amount, balance_after, reason_code, fencing_epoch, server_id
		) VALUES ('idem-ledger-1', $1, 'currency.standard_coin', 100, 100, 'MOB_DROP', 1, 'zone-test-1')
		RETURNING ledger_id
	`, charID).Scan(&ledgerID)
	if err != nil {
		t.Fatalf("insert currency_ledger: %v", err)
	}

	_, err = pool.Exec(ctx, `UPDATE currency_ledger SET delta_amount = 999 WHERE ledger_id = $1`, ledgerID)
	var pgErr *pgconn.PgError
	if !errors.As(err, &pgErr) || pgErr.Code != "P0001" || !strings.Contains(pgErr.Message, "IMMUTABLE_LEDGER_VIOLATION") {
		t.Fatalf("expected P0001 IMMUTABLE_LEDGER_VIOLATION on UPDATE, got %v", err)
	}

	_, err = pool.Exec(ctx, `DELETE FROM currency_ledger WHERE ledger_id = $1`, ledgerID)
	if !errors.As(err, &pgErr) || pgErr.Code != "P0001" || !strings.Contains(pgErr.Message, "IMMUTABLE_LEDGER_VIOLATION") {
		t.Fatalf("expected P0001 IMMUTABLE_LEDGER_VIOLATION on DELETE, got %v", err)
	}
}

type txObservation struct {
	txName   string
	status   string
	duration time.Duration
}

type spyTxObserver struct {
	mu   sync.Mutex
	seen []txObservation
}

func (s *spyTxObserver) ObserveTxDuration(txName, status string, duration time.Duration) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.seen = append(s.seen, txObservation{txName: txName, status: status, duration: duration})
}

func (s *spyTxObserver) last() txObservation {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.seen[len(s.seen)-1]
}

func (s *spyTxObserver) count() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.seen)
}

func TestTxManagerWithinTxLifecycle(t *testing.T) {
	pool := openIsolatedPG(t)
	applyUpMigration(t, pool)
	ctx := context.Background()

	_, charID := seedAccountAndCharacter(t, pool, "txmgr1")
	if _, err := pool.Exec(ctx, `INSERT INTO character_wallets (character_id, standard_coin) VALUES ($1, 100)`, charID); err != nil {
		t.Fatalf("seed wallet: %v", err)
	}

	observer := &spyTxObserver{}
	txMgr := postgres.NewTxManager(pool, observer)

	readBalance := func() int64 {
		var bal int64
		if err := txMgr.Conn(ctx).QueryRow(ctx, `SELECT standard_coin FROM character_wallets WHERE character_id = $1`, charID).Scan(&bal); err != nil {
			t.Fatalf("read balance: %v", err)
		}
		return bal
	}

	// 1. Commit
	err := txMgr.WithinTx(ctx, "tx_commit", func(txCtx context.Context) error {
		_, err := txMgr.Conn(txCtx).Exec(txCtx, `UPDATE character_wallets SET standard_coin = 250 WHERE character_id = $1`, charID)
		return err
	})
	if err != nil {
		t.Fatalf("tx_commit failed: %v", err)
	}
	if bal := readBalance(); bal != 250 {
		t.Fatalf("expected balance 250 after commit, got %d", bal)
	}
	if last := observer.last(); last.txName != "tx_commit" || last.status != "committed" {
		t.Fatalf("unexpected observer entry for commit: %+v", last)
	}

	// 2. Error rollback
	errBoom := errors.New("boom")
	err = txMgr.WithinTx(ctx, "tx_err", func(txCtx context.Context) error {
		if _, err := txMgr.Conn(txCtx).Exec(txCtx, `UPDATE character_wallets SET standard_coin = 999 WHERE character_id = $1`, charID); err != nil {
			return err
		}
		return errBoom
	})
	if !errors.Is(err, errBoom) {
		t.Fatalf("expected errBoom, got %v", err)
	}
	if bal := readBalance(); bal != 250 {
		t.Fatalf("expected balance 250 after error rollback, got %d", bal)
	}
	if last := observer.last(); last.txName != "tx_err" || last.status != "rolled_back" {
		t.Fatalf("unexpected observer entry for error rollback: %+v", last)
	}

	// 3. Panic recovery rollback
	var recovered any
	func() {
		defer func() {
			recovered = recover()
		}()
		_ = txMgr.WithinTx(ctx, "tx_panic", func(txCtx context.Context) error {
			_, err := txMgr.Conn(txCtx).Exec(txCtx, `UPDATE character_wallets SET standard_coin = 777 WHERE character_id = $1`, charID)
			if err != nil {
				return err
			}
			panic("unexpected panic inside tx")
		})
	}()
	if recovered == nil {
		t.Fatal("expected panic to be re-raised after rollback")
	}
	if bal := readBalance(); bal != 250 {
		t.Fatalf("expected balance 250 after panic rollback, got %d", bal)
	}
	if last := observer.last(); last.txName != "tx_panic" || last.status != "panic_rollback" {
		t.Fatalf("unexpected observer entry for panic rollback: %+v", last)
	}

	// 4. Nested WithinTx join
	beforeCount := observer.count()
	errOuter := errors.New("outer abort")
	err = txMgr.WithinTx(ctx, "tx_outer", func(outerCtx context.Context) error {
		if err := txMgr.WithinTx(outerCtx, "tx_inner", func(innerCtx context.Context) error {
			_, err := txMgr.Conn(innerCtx).Exec(innerCtx, `UPDATE character_wallets SET standard_coin = 500 WHERE character_id = $1`, charID)
			return err
		}); err != nil {
			return err
		}
		return errOuter
	})
	if !errors.Is(err, errOuter) {
		t.Fatalf("expected errOuter, got %v", err)
	}
	if bal := readBalance(); bal != 250 {
		t.Fatalf("expected nested inner update to roll back with outer tx, got %d", bal)
	}
	if observer.count() != beforeCount+1 {
		t.Fatalf("expected nested WithinTx to emit 1 outer observation, got %d", observer.count()-beforeCount)
	}
	if last := observer.last(); last.txName != "tx_outer" || last.status != "rolled_back" {
		t.Fatalf("unexpected observer entry for outer tx: %+v", last)
	}

	// 5. Cancelled caller context mid-transaction still rolls back cleanly
	cancelCtx, cancelTx := context.WithCancel(ctx)
	err = txMgr.WithinTx(cancelCtx, "tx_ctx_cancel", func(txCtx context.Context) error {
		if _, err := txMgr.Conn(txCtx).Exec(txCtx, `UPDATE character_wallets SET standard_coin = 888 WHERE character_id = $1`, charID); err != nil {
			return err
		}
		cancelTx()
		return context.Canceled
	})
	if !errors.Is(err, context.Canceled) {
		t.Fatalf("expected context.Canceled, got %v", err)
	}
	if strings.Contains(err.Error(), "rollback failed") {
		t.Fatalf("expected clean detached rollback without rollback failure, got %v", err)
	}
	if bal := readBalance(); bal != 250 {
		t.Fatalf("expected balance 250 after cancelled-ctx rollback, got %d", bal)
	}
	if last := observer.last(); last.txName != "tx_ctx_cancel" || last.status != "rolled_back" {
		t.Fatalf("unexpected observer entry for cancelled-ctx rollback: %+v", last)
	}
}
