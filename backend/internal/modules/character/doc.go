// Package character owns character state, attributes, save snapshots, and fencing-epoch leases.
//
// Exclusive PostgreSQL ownership: characters, character_attributes, character_save_snapshots.
// Redis keys: char:lease:*, char:epoch:*.
package character
