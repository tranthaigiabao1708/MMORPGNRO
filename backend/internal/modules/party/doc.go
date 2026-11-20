// Package party owns ephemeral party state, boss contribution sharing, and channel synchronization.
//
// Exclusive PostgreSQL ownership: none (ephemeral Redis state).
// Redis keys: party:state:*, party:char:*.
package party
