// Package loot owns authoritative 2-tier loot rolls, affix generation, boss kill history, and pity tokens.
//
// Exclusive PostgreSQL ownership: loot_history, boss_kill_history, boss_pity_tokens.
// Redis keys: idem:kill:*, ratelimit:kill:*.
package loot
