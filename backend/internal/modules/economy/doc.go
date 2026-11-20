// Package economy owns character wallets, immutable currency ledger, enhancement, and 2-phase trades.
//
// Exclusive PostgreSQL ownership: character_wallets, currency_ledger, trade_transactions.
// Redis keys: trade:session:*, trade:active_char:*.
package economy
