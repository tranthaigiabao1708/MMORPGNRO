// Package social owns chat channels and real-time player presence.
//
// Exclusive PostgreSQL ownership: none (ephemeral Redis + NATS state).
// Redis keys: presence:char:*.
package social
