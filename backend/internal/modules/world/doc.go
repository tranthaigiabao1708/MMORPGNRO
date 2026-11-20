// Package world owns zone registry, channel sharding, and 7-step zone handoffs via cmd/coordinator.
//
// Exclusive PostgreSQL ownership: none (Redis + NATS JetStream IU_WORLD_STREAM).
// Redis keys: zone:registry:*, zone:instances:*, zone:channels:*, slot:resv:*, handoff:char:*.
package world
