// Package auth handles JWT Ed25519 verification, account session locks, and one-time join tickets.
//
// Exclusive PostgreSQL ownership: none (reads account credentials via account ports).
// Redis keys: session:account:*, session:ticket:*, refresh_tok:*.
package auth
