-- ============================================================================
-- INFINITE UNIVERSE - POSTGRESQL 16 SCHEMA ROLLBACK (19 TABLES)
-- Reverse topological drop order (no CASCADE required).
-- Extensions (pgcrypto, citext) are preserved for database-level idempotency.
-- ============================================================================

BEGIN;

DROP TABLE IF EXISTS character_save_snapshots;
DROP TABLE IF EXISTS guild_members;
DROP TABLE IF EXISTS guilds;
DROP TABLE IF EXISTS trade_transactions;
DROP TABLE IF EXISTS boss_pity_tokens;
DROP TABLE IF EXISTS boss_kill_history;
DROP TABLE IF EXISTS loot_history;
DROP TABLE IF EXISTS character_quests;
DROP TABLE IF EXISTS currency_ledger;
DROP TABLE IF EXISTS character_wallets;
DROP TABLE IF EXISTS character_equipment;
DROP TABLE IF EXISTS inventory_items;
DROP TABLE IF EXISTS item_definitions;
DROP TABLE IF EXISTS character_transformations;
DROP TABLE IF EXISTS character_skills;
DROP TABLE IF EXISTS character_potential;
DROP TABLE IF EXISTS character_attributes;
DROP TABLE IF EXISTS characters;
DROP TABLE IF EXISTS accounts;

DROP FUNCTION IF EXISTS fn_prevent_ledger_mutation();
DROP FUNCTION IF EXISTS fn_touch_updated_at();

COMMIT;
