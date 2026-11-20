-- ============================================================================
-- DRAGON BALL: INFINITE UNIVERSE - POSTGRESQL 16 AUTHORITATIVE SCHEMA (19 TABLES)
-- Charset: UTF8 | Timezone: UTC
-- ============================================================================

BEGIN;

CREATE EXTENSION IF NOT EXISTS "pgcrypto";
CREATE EXTENSION IF NOT EXISTS "citext";

-- Hàm tiện ích tự động cập nhật cột updated_at
CREATE OR REPLACE FUNCTION fn_touch_updated_at()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- ============================================================================
-- 1. BẢNG TÀI KHOẢN NGƯỜI DÙNG (accounts)
-- ============================================================================
CREATE TABLE accounts (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    username            CITEXT NOT NULL UNIQUE CHECK (char_length(username) BETWEEN 3 AND 32),
    email               CITEXT NOT NULL UNIQUE CHECK (position('@' IN email) > 1),
    password_hash       TEXT NOT NULL,                          -- Argon2id PHC string
    account_status      VARCHAR(24) NOT NULL DEFAULT 'ACTIVE'
                        CHECK (account_status IN ('ACTIVE', 'SUSPENDED', 'BANNED', 'PENDING_VERIFY')),
    ban_until           TIMESTAMPTZ,
    ban_reason          TEXT,
    max_character_slots SMALLINT NOT NULL DEFAULT 4 CHECK (max_character_slots BETWEEN 1 AND 12),
    last_login_at       TIMESTAMPTZ,
    last_login_ip       INET,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TRIGGER trg_accounts_updated_at
BEFORE UPDATE ON accounts
FOR EACH ROW EXECUTE FUNCTION fn_touch_updated_at();

-- ============================================================================
-- 2. BẢNG NHÂN VẬT TRUNG TÂM & RÀO CHẮN CHỐNG DUPE (characters)
-- ============================================================================
CREATE TABLE characters (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    account_id          UUID NOT NULL REFERENCES accounts(id) ON DELETE RESTRICT,
    character_name      CITEXT NOT NULL UNIQUE CHECK (char_length(character_name) BETWEEN 3 AND 20),
    race_id             VARCHAR(48) NOT NULL
                        CHECK (race_id IN (
                            'race.zenith_warrior',
                            'race.terran_adept',
                            'race.mystic_elder',
                            'race.frost_archon',
                            'race.chaos_morph',
                            'race.cyber_construct'
                        )),
    appearance_data     JSONB NOT NULL DEFAULT '{}'::jsonb,     -- Kiểu tóc, màu mắt, chiều cao, màu hào quang gốc
    level               INT NOT NULL DEFAULT 1 CHECK (level BETWEEN 1 AND 200),
    current_exp         BIGINT NOT NULL DEFAULT 0 CHECK (current_exp >= 0),
    planet_id           VARCHAR(48) NOT NULL DEFAULT 'planet.p01_terra',
    zone_id             VARCHAR(64) NOT NULL DEFAULT 'zone.p01_west_hub',
    last_channel_id     INT NOT NULL DEFAULT 1 CHECK (last_channel_id >= 1),
    pos_x               DOUBLE PRECISION NOT NULL DEFAULT 0.0,
    pos_y               DOUBLE PRECISION NOT NULL DEFAULT 0.0,
    pos_z               DOUBLE PRECISION NOT NULL DEFAULT 120.0,
    rot_yaw             REAL NOT NULL DEFAULT 0.0,
    safe_respawn_zone   VARCHAR(64) NOT NULL DEFAULT 'zone.p01_west_hub',
    safe_respawn_x      DOUBLE PRECISION NOT NULL DEFAULT 0.0,
    safe_respawn_y      DOUBLE PRECISION NOT NULL DEFAULT 0.0,
    safe_respawn_z      DOUBLE PRECISION NOT NULL DEFAULT 120.0,
    is_alive            BOOLEAN NOT NULL DEFAULT TRUE,
    active_form_id      VARCHAR(64),                            -- NULL nếu ở trạng thái thường, hoặc 'form.zenith_awaken_01'
    playtime_seconds    BIGINT NOT NULL DEFAULT 0 CHECK (playtime_seconds >= 0),
    fencing_epoch       BIGINT NOT NULL DEFAULT 0 CHECK (fencing_epoch >= 0),
    schema_version      INT NOT NULL DEFAULT 1 CHECK (schema_version >= 1),
    deleted_at          TIMESTAMPTZ,
    last_saved_at       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_characters_account_id ON characters(account_id) WHERE deleted_at IS NULL;
CREATE INDEX idx_characters_zone_level ON characters(zone_id, level) WHERE deleted_at IS NULL;

CREATE TRIGGER trg_characters_updated_at
BEFORE UPDATE ON characters
FOR EACH ROW EXECUTE FUNCTION fn_touch_updated_at();

-- ============================================================================
-- 3. BẢNG CHỈ SỐ CƠ BẢN & CHIẾN ĐẤU (character_attributes)
-- Khớp 100% UIUAttributeSet & Canonical Dictionary (Mục 4.3 README.md)
-- ============================================================================
CREATE TABLE character_attributes (
    character_id            UUID PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,
    -- Nhóm Base Attributes
    hp                      DOUBLE PRECISION NOT NULL DEFAULT 500.0 CHECK (hp >= 0.0),
    max_hp                  DOUBLE PRECISION NOT NULL DEFAULT 500.0 CHECK (max_hp > 0.0),
    energy                  DOUBLE PRECISION NOT NULL DEFAULT 300.0 CHECK (energy >= 0.0),
    max_energy              DOUBLE PRECISION NOT NULL DEFAULT 300.0 CHECK (max_energy > 0.0),
    stamina                 DOUBLE PRECISION NOT NULL DEFAULT 100.0 CHECK (stamina >= 0.0),
    max_stamina             DOUBLE PRECISION NOT NULL DEFAULT 100.0 CHECK (max_stamina > 0.0),
    phys_atk                DOUBLE PRECISION NOT NULL DEFAULT 45.0 CHECK (phys_atk >= 0.0),
    ki_atk                  DOUBLE PRECISION NOT NULL DEFAULT 45.0 CHECK (ki_atk >= 0.0),
    defense                 DOUBLE PRECISION NOT NULL DEFAULT 20.0 CHECK (defense >= 0.0),
    -- Nhóm Combat Attributes (Hệ số tỷ lệ thập phân: ví dụ 0.05 = 5%)
    phys_dmg_pct            REAL NOT NULL DEFAULT 0.0,
    ki_dmg_pct              REAL NOT NULL DEFAULT 0.0,
    crit_rate               REAL NOT NULL DEFAULT 0.05 CHECK (crit_rate BETWEEN 0.0 AND 1.0),
    crit_dmg                REAL NOT NULL DEFAULT 1.50 CHECK (crit_dmg >= 1.0),
    atk_speed               REAL NOT NULL DEFAULT 1.00 CHECK (atk_speed > 0.1),
    move_speed              REAL NOT NULL DEFAULT 1.00 CHECK (move_speed > 0.1),
    energy_regen            REAL NOT NULL DEFAULT 8.00 CHECK (energy_regen >= 0.0),
    cdr                     REAL NOT NULL DEFAULT 0.00 CHECK (cdr BETWEEN 0.0 AND 0.80),
    -- Điểm tiềm năng lên cấp chưa cộng
    unspent_stat_points     INT NOT NULL DEFAULT 0 CHECK (unspent_stat_points >= 0),
    updated_at              TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT chk_hp_within_max CHECK (hp <= max_hp * 2.0),
    CONSTRAINT chk_energy_within_max CHECK (energy <= max_energy * 2.0)
);

CREATE TRIGGER trg_character_attributes_updated_at
BEFORE UPDATE ON character_attributes
FOR EACH ROW EXECUTE FUNCTION fn_touch_updated_at();

-- ============================================================================
-- 4. BẢNG TU LUYỆN TIỀM NĂNG NỘI TẠI (character_potential)
-- Khớp 5 cột pot.* trong README.md
-- ============================================================================
CREATE TABLE character_potential (
    character_id            UUID PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,
    potential_tier          SMALLINT NOT NULL DEFAULT 1 CHECK (potential_tier BETWEEN 1 AND 20),
    potential_exp           BIGINT NOT NULL DEFAULT 0 CHECK (potential_exp >= 0),
    form_efficiency         REAL NOT NULL DEFAULT 0.0 CHECK (form_efficiency BETWEEN 0.0 AND 0.90),
    energy_control          REAL NOT NULL DEFAULT 0.0 CHECK (energy_control BETWEEN 0.0 AND 0.80),
    energy_capacity         DOUBLE PRECISION NOT NULL DEFAULT 0.0 CHECK (energy_capacity >= 0.0),
    flight_efficiency       REAL NOT NULL DEFAULT 0.0 CHECK (flight_efficiency BETWEEN 0.0 AND 0.90),
    all_resist              REAL NOT NULL DEFAULT 0.0 CHECK (all_resist BETWEEN 0.0 AND 0.85),
    unspent_potential_pts   INT NOT NULL DEFAULT 0 CHECK (unspent_potential_pts >= 0),
    updated_at              TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TRIGGER trg_character_potential_updated_at
BEFORE UPDATE ON character_potential
FOR EACH ROW EXECUTE FUNCTION fn_touch_updated_at();

-- ============================================================================
-- 5. BẢNG KỸ NĂNG NHÂN VẬT (character_skills)
-- ============================================================================
CREATE TABLE character_skills (
    character_id            UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    skill_id                VARCHAR(64) NOT NULL,               -- Ví dụ: 'skill.melee_basic_combo', 'skill.beam_charged_01'
    skill_level             SMALLINT NOT NULL DEFAULT 1 CHECK (skill_level BETWEEN 1 AND 20),
    mastery_exp             BIGINT NOT NULL DEFAULT 0 CHECK (mastery_exp >= 0),
    quickbar_slot           SMALLINT CHECK (quickbar_slot IS NULL OR quickbar_slot BETWEEN 1 AND 8),
    unlocked_at             TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at              TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (character_id, skill_id),
    CONSTRAINT uq_character_quickbar_slot UNIQUE (character_id, quickbar_slot)
);

CREATE TRIGGER trg_character_skills_updated_at
BEFORE UPDATE ON character_skills
FOR EACH ROW EXECUTE FUNCTION fn_touch_updated_at();

-- ============================================================================
-- 6. BẢNG BIẾN HÌNH NHÂN VẬT (character_transformations)
-- ============================================================================
CREATE TABLE character_transformations (
    character_id            UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    form_id                 VARCHAR(64) NOT NULL,               -- Ví dụ: 'form.zenith_awaken_01'
    form_stage              SMALLINT NOT NULL DEFAULT 1 CHECK (form_stage BETWEEN 1 AND 10),
    mastery_level           SMALLINT NOT NULL DEFAULT 1 CHECK (mastery_level BETWEEN 1 AND 100),
    mastery_exp             BIGINT NOT NULL DEFAULT 0 CHECK (mastery_exp >= 0),
    is_equipped_slot        BOOLEAN NOT NULL DEFAULT FALSE,
    unlocked_at             TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at              TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (character_id, form_id)
);

CREATE INDEX idx_char_transforms_equipped ON character_transformations(character_id) WHERE is_equipped_slot = TRUE;

CREATE TRIGGER trg_character_transformations_updated_at
BEFORE UPDATE ON character_transformations
FOR EACH ROW EXECUTE FUNCTION fn_touch_updated_at();

-- ============================================================================
-- 7. BẢNG DANH MỤC ĐỊNH NGHĨA VẬT PHẨM (item_definitions)
-- Đồng bộ tự động từ Data-Driven Bundle khi khởi động gameapi
-- ============================================================================
CREATE TABLE item_definitions (
    item_def_id             VARCHAR(80) PRIMARY KEY,            -- Ví dụ: 'item.eq_body_titan_battle_armor_01'
    item_category           VARCHAR(32) NOT NULL
                            CHECK (item_category IN ('EQUIPMENT', 'CONSUMABLE', 'MATERIAL', 'GEM_CORE', 'QUEST_ITEM', 'TOKEN_BOX')),
    equip_slot              VARCHAR(32)
                            CHECK (equip_slot IS NULL OR equip_slot IN (
                                'slot.head', 'slot.body', 'slot.legs', 'slot.hands',
                                'slot.feet', 'slot.acc_1', 'slot.acc_2', 'slot.special'
                            )),
    base_rarity             VARCHAR(32) NOT NULL
                            CHECK (base_rarity IN (
                                'rarity.common', 'rarity.uncommon', 'rarity.rare',
                                'rarity.epic', 'rarity.legendary', 'rarity.mythic'
                            )),
    req_level               INT NOT NULL DEFAULT 1 CHECK (req_level >= 1),
    allowed_races           TEXT[] NOT NULL DEFAULT '{}',       -- Mảng rỗng = Mọi chủng tộc mặc được
    set_id                  VARCHAR(64),                        -- Khóa bộ trang bị (Set Bonus)
    max_stack               INT NOT NULL DEFAULT 1 CHECK (max_stack >= 1),
    max_enhance_level       SMALLINT NOT NULL DEFAULT 15 CHECK (max_enhance_level BETWEEN 0 AND 25),
    is_tradeable_default    BOOLEAN NOT NULL DEFAULT TRUE,
    npc_sell_price          BIGINT NOT NULL DEFAULT 0 CHECK (npc_sell_price >= 0),
    base_stats              JSONB NOT NULL DEFAULT '{}'::jsonb,
    affix_pool_id           VARCHAR(64),
    content_hash            VARCHAR(64) NOT NULL,
    updated_at              TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT chk_equip_slot_consistency CHECK (
        (item_category = 'EQUIPMENT' AND equip_slot IS NOT NULL AND max_stack = 1)
        OR (item_category <> 'EQUIPMENT' AND equip_slot IS NULL)
    )
);

-- ============================================================================
-- 8. BẢNG THỰC THỂ VẬT PHẨM TRONG TÚI ĐỒ / KHO (inventory_items)
-- ============================================================================
CREATE TABLE inventory_items (
    id                      UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    character_id            UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    item_def_id             VARCHAR(80) NOT NULL REFERENCES item_definitions(item_def_id) ON DELETE RESTRICT,
    rarity                  VARCHAR(32) NOT NULL
                            CHECK (rarity IN (
                                'rarity.common', 'rarity.uncommon', 'rarity.rare',
                                'rarity.epic', 'rarity.legendary', 'rarity.mythic'
                            )),
    bag_type                VARCHAR(24) NOT NULL DEFAULT 'INVENTORY'
                            CHECK (bag_type IN ('INVENTORY', 'EQUIPPED', 'PERSONAL_VAULT')),
    bag_slot_index          INT CHECK (bag_slot_index IS NULL OR bag_slot_index >= 0),
    stack_count             INT NOT NULL DEFAULT 1 CHECK (stack_count >= 1),
    enhance_level           SMALLINT NOT NULL DEFAULT 0 CHECK (enhance_level BETWEEN 0 AND 25),
    affix_data              JSONB NOT NULL DEFAULT '[]'::jsonb, -- Danh sách dòng ngẫu nhiên đã quay
    socket_data             JSONB NOT NULL DEFAULT '[]'::jsonb, -- Danh sách lõi ngọc khảm nạm
    is_bound                BOOLEAN NOT NULL DEFAULT FALSE,     -- Khóa nhân vật (Soulbound)
    is_player_locked        BOOLEAN NOT NULL DEFAULT FALSE,     -- Khóa chống bán/phân rã nhầm bởi người chơi
    lock_reason             VARCHAR(48)
                            CHECK (lock_reason IS NULL OR lock_reason IN (
                                'IN_TRADE_ESCROW', 'ENHANCING', 'ADMIN_HOLD', 'QUEST_RESERVED'
                            )),
    origin_source           VARCHAR(64) NOT NULL DEFAULT 'LOOT_DROP',
    acquired_at             TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at              TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_inv_items_char_bag ON inventory_items(character_id, bag_type, bag_slot_index);
CREATE INDEX idx_inv_items_char_def ON inventory_items(character_id, item_def_id);
CREATE INDEX idx_inv_items_affix_gin ON inventory_items USING GIN (affix_data jsonb_path_ops);
CREATE INDEX idx_inv_items_tradeable ON inventory_items(character_id)
    WHERE is_bound = FALSE AND lock_reason IS NULL AND bag_type = 'INVENTORY';

CREATE TRIGGER trg_inventory_items_updated_at
BEFORE UPDATE ON inventory_items
FOR EACH ROW EXECUTE FUNCTION fn_touch_updated_at();

-- ============================================================================
-- 9. BẢNG 8 Ô TRANG BỊ ĐANG MẶC TRÊN NGƯỜI (character_equipment)
-- Ràng buộc cứng UNIQUE(character_id, slot_id) và UNIQUE(item_instance_id)
-- ============================================================================
CREATE TABLE character_equipment (
    character_id            UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    slot_id                 VARCHAR(32) NOT NULL
                            CHECK (slot_id IN (
                                'slot.head',
                                'slot.body',
                                'slot.legs',
                                'slot.hands',
                                'slot.feet',
                                'slot.acc_1',
                                'slot.acc_2',
                                'slot.special'
                            )),
    item_instance_id        UUID NOT NULL UNIQUE REFERENCES inventory_items(id) ON DELETE RESTRICT,
    equipped_at             TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT pk_character_equipment PRIMARY KEY (character_id, slot_id)
);

-- ============================================================================
-- 10. BẢNG VÍ TIỀN TỆ HIỆN HÀNH (character_wallets)
-- ============================================================================
CREATE TABLE character_wallets (
    character_id            UUID PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,
    standard_coin           BIGINT NOT NULL DEFAULT 0 CHECK (standard_coin >= 0),
    mastery_point           BIGINT NOT NULL DEFAULT 0 CHECK (mastery_point >= 0),
    lifetime_coin_earned    BIGINT NOT NULL DEFAULT 0 CHECK (lifetime_coin_earned >= 0),
    lifetime_coin_spent     BIGINT NOT NULL DEFAULT 0 CHECK (lifetime_coin_spent >= 0),
    updated_at              TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TRIGGER trg_character_wallets_updated_at
BEFORE UPDATE ON character_wallets
FOR EACH ROW EXECUTE FUNCTION fn_touch_updated_at();

-- ============================================================================
-- 11. BẢNG SỔ CÁI TIỀN TỆ BẤT BIẾN APPEND-ONLY (currency_ledger)
-- Cấm tuyệt đối UPDATE và DELETE bằng Trigger cơ sở dữ liệu
-- ============================================================================
CREATE TABLE currency_ledger (
    ledger_id               BIGSERIAL PRIMARY KEY,
    idempotency_key         VARCHAR(128) NOT NULL UNIQUE,
    character_id            UUID NOT NULL REFERENCES characters(id) ON DELETE RESTRICT,
    currency_type           VARCHAR(48) NOT NULL
                            CHECK (currency_type IN ('currency.standard_coin', 'currency.mastery_point')),
    delta_amount            BIGINT NOT NULL CHECK (delta_amount <> 0),
    balance_after           BIGINT NOT NULL CHECK (balance_after >= 0),
    reason_code             VARCHAR(64) NOT NULL,               -- 'MOB_DROP', 'ENHANCE_COST', 'PLAYER_TRADE', 'TRADE_TAX', 'QUEST_REWARD'
    reference_type          VARCHAR(48),                        -- 'TRADE', 'ITEM', 'BOSS_KILL', 'QUEST'
    reference_id            VARCHAR(128),
    fencing_epoch           BIGINT NOT NULL CHECK (fencing_epoch >= 1),
    server_id               VARCHAR(64) NOT NULL,
    created_at              TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_currency_ledger_char_time ON currency_ledger(character_id, created_at DESC);
CREATE INDEX idx_currency_ledger_ref ON currency_ledger(reference_type, reference_id) WHERE reference_id IS NOT NULL;

CREATE OR REPLACE FUNCTION fn_prevent_ledger_mutation()
RETURNS TRIGGER AS $$
BEGIN
    RAISE EXCEPTION 'IMMUTABLE_LEDGER_VIOLATION: currency_ledger is strictly append-only (% prohibited)', TG_OP;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_currency_ledger_immutable
BEFORE UPDATE OR DELETE ON currency_ledger
FOR EACH ROW EXECUTE FUNCTION fn_prevent_ledger_mutation();

-- ============================================================================
-- 12. BẢNG TIẾN ĐỘ NHIỆM VỤ NHÂN VẬT (character_quests)
-- ============================================================================
CREATE TABLE character_quests (
    character_id            UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    quest_id                VARCHAR(80) NOT NULL,
    quest_state             VARCHAR(24) NOT NULL DEFAULT 'IN_PROGRESS'
                            CHECK (quest_state IN ('IN_PROGRESS', 'READY_TO_TURN_IN', 'COMPLETED', 'ABANDONED')),
    objectives_progress     JSONB NOT NULL DEFAULT '{}'::jsonb, -- Ví dụ: {"kill_mob.highland_wolf_01": {"current": 7, "target": 10}}
    repeat_count            INT NOT NULL DEFAULT 0 CHECK (repeat_count >= 0),
    accepted_at             TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    completed_at            TIMESTAMPTZ,
    updated_at              TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (character_id, quest_id)
);

CREATE INDEX idx_char_quests_active ON character_quests(character_id, quest_state)
    WHERE quest_state IN ('IN_PROGRESS', 'READY_TO_TURN_IN');

CREATE TRIGGER trg_character_quests_updated_at
BEFORE UPDATE ON character_quests
FOR EACH ROW EXECUTE FUNCTION fn_touch_updated_at();

-- ============================================================================
-- 13. BẢNG LỊCH SỬ RƠI ĐỒ KIỂM TOÁN (loot_history)
-- ============================================================================
CREATE TABLE loot_history (
    loot_event_id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    idempotency_key         VARCHAR(128) NOT NULL UNIQUE,
    character_id            UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    item_instance_id        UUID REFERENCES inventory_items(id) ON DELETE SET NULL,
    item_def_id             VARCHAR(80) NOT NULL REFERENCES item_definitions(item_def_id) ON DELETE RESTRICT,
    rarity                  VARCHAR(32) NOT NULL
                            CHECK (rarity IN (
                                'rarity.common', 'rarity.uncommon', 'rarity.rare',
                                'rarity.epic', 'rarity.legendary', 'rarity.mythic'
                            )),
    quantity                INT NOT NULL DEFAULT 1 CHECK (quantity >= 1),
    source_entity_id        VARCHAR(80) NOT NULL,               -- 'boss.highland_titan_01', 'mob.crimson_enforcer_elite'
    zone_id                 VARCHAR(64) NOT NULL,
    channel_id              INT NOT NULL DEFAULT 1,
    was_pity_guaranteed     BOOLEAN NOT NULL DEFAULT FALSE,
    rolled_affixes_snapshot JSONB NOT NULL DEFAULT '[]'::jsonb,
    created_at              TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_loot_history_char_time ON loot_history(character_id, created_at DESC);
CREATE INDEX idx_loot_history_rare_drops ON loot_history(rarity, created_at DESC)
    WHERE rarity IN ('rarity.epic', 'rarity.legendary', 'rarity.mythic');

-- ============================================================================
-- 14. BẢNG LỊCH SỬ TIÊU DIỆT BOSS KHU VỰC & THẾ GIỚI (boss_kill_history)
-- ============================================================================
CREATE TABLE boss_kill_history (
    kill_id                 UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    idempotency_key         VARCHAR(128) NOT NULL UNIQUE,
    boss_id                 VARCHAR(80) NOT NULL,               -- Ví dụ: 'boss.highland_titan_01'
    zone_id                 VARCHAR(64) NOT NULL,
    channel_id              INT NOT NULL DEFAULT 1,
    instance_id             VARCHAR(80),
    fight_duration_ms       INT NOT NULL CHECK (fight_duration_ms >= 0),
    total_damage_dealt      BIGINT NOT NULL CHECK (total_damage_dealt > 0),
    top_killer_char_id      UUID REFERENCES characters(id) ON DELETE SET NULL,
    participants            JSONB NOT NULL DEFAULT '[]'::jsonb, -- [{"character_id": "...", "damage": 42500, "share_pct": 0.32, "rank": 1}]
    spawned_at              TIMESTAMPTZ NOT NULL,
    killed_at               TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_boss_kills_boss_time ON boss_kill_history(boss_id, killed_at DESC);
CREATE INDEX idx_boss_kills_participants_gin ON boss_kill_history USING GIN (participants jsonb_path_ops);

-- ============================================================================
-- 15. BẢNG ĐIỂM BẢO HIỂM SĂN BOSS (boss_pity_tokens)
-- Khớp cột token_id + amount trong README.md (Mục 4.3)
-- ============================================================================
CREATE TABLE boss_pity_tokens (
    character_id            UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    token_id                VARCHAR(80) NOT NULL,               -- Ví dụ: 'pity.boss_highland_titan_01'
    amount                  INT NOT NULL DEFAULT 0 CHECK (amount >= 0),
    consecutive_dry_kills   INT NOT NULL DEFAULT 0 CHECK (consecutive_dry_kills >= 0),
    lifetime_earned         INT NOT NULL DEFAULT 0 CHECK (lifetime_earned >= 0),
    lifetime_redeemed       INT NOT NULL DEFAULT 0 CHECK (lifetime_redeemed >= 0),
    updated_at              TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (character_id, token_id)
);

CREATE TRIGGER trg_boss_pity_tokens_updated_at
BEFORE UPDATE ON boss_pity_tokens
FOR EACH ROW EXECUTE FUNCTION fn_touch_updated_at();

-- ============================================================================
-- 16. BẢNG GIAO DỊCH NGUYÊN TỬ GIỮA HAI NGƯỜI CHƠI (trade_transactions)
-- ============================================================================
CREATE TABLE trade_transactions (
    trade_id                UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    idempotency_key         VARCHAR(128) NOT NULL UNIQUE,
    initiator_char_id       UUID NOT NULL REFERENCES characters(id) ON DELETE RESTRICT,
    recipient_char_id       UUID NOT NULL REFERENCES characters(id) ON DELETE RESTRICT,
    trade_state             VARCHAR(24) NOT NULL
                            CHECK (trade_state IN ('SETTLED', 'CANCELLED', 'ABORTED_CONFLICT')),
    initiator_coin_given    BIGINT NOT NULL DEFAULT 0 CHECK (initiator_coin_given >= 0),
    recipient_coin_given    BIGINT NOT NULL DEFAULT 0 CHECK (recipient_coin_given >= 0),
    tax_coin_burned         BIGINT NOT NULL DEFAULT 0 CHECK (tax_coin_burned >= 0),
    initiator_items_given   JSONB NOT NULL DEFAULT '[]'::jsonb, -- Danh sách snapshot UUID + thông số vật phẩm bên A trao bên B
    recipient_items_given   JSONB NOT NULL DEFAULT '[]'::jsonb, -- Danh sách snapshot UUID + thông số vật phẩm bên B trao bên A
    zone_id                 VARCHAR(64) NOT NULL,
    initiator_epoch         BIGINT NOT NULL,
    recipient_epoch         BIGINT NOT NULL,
    settled_at              TIMESTAMPTZ,
    created_at              TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT chk_no_self_trade CHECK (initiator_char_id <> recipient_char_id)
);

CREATE INDEX idx_trades_initiator_time ON trade_transactions(initiator_char_id, created_at DESC);
CREATE INDEX idx_trades_recipient_time ON trade_transactions(recipient_char_id, created_at DESC);

-- ============================================================================
-- 17. BẢNG BANG HỘI / CHIẾN ĐOÀN (guilds)
-- ============================================================================
CREATE TABLE guilds (
    guild_id                UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    guild_name              CITEXT NOT NULL UNIQUE CHECK (char_length(guild_name) BETWEEN 3 AND 24),
    guild_tag               CITEXT NOT NULL UNIQUE CHECK (char_length(guild_tag) BETWEEN 2 AND 5),
    leader_char_id          UUID NOT NULL UNIQUE REFERENCES characters(id) ON DELETE RESTRICT,
    guild_level             SMALLINT NOT NULL DEFAULT 1 CHECK (guild_level BETWEEN 1 AND 50),
    guild_exp               BIGINT NOT NULL DEFAULT 0 CHECK (guild_exp >= 0),
    member_cap              SMALLINT NOT NULL DEFAULT 30 CHECK (member_cap BETWEEN 10 AND 150),
    treasury_standard_coin  BIGINT NOT NULL DEFAULT 0 CHECK (treasury_standard_coin >= 0),
    motd_announcement       VARCHAR(500) NOT NULL DEFAULT '',
    emblem_data             JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at              TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at              TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TRIGGER trg_guilds_updated_at
BEFORE UPDATE ON guilds
FOR EACH ROW EXECUTE FUNCTION fn_touch_updated_at();

-- ============================================================================
-- 18. BẢNG THÀNH VIÊN BANG HỘI (guild_members)
-- Mỗi nhân vật chỉ thuộc tối đa 1 Guild tại một thời điểm (UNIQUE character_id)
-- ============================================================================
CREATE TABLE guild_members (
    guild_id                UUID NOT NULL REFERENCES guilds(guild_id) ON DELETE CASCADE,
    character_id            UUID NOT NULL UNIQUE REFERENCES characters(id) ON DELETE CASCADE,
    member_role             VARCHAR(24) NOT NULL DEFAULT 'MEMBER'
                            CHECK (member_role IN ('LEADER', 'VICE_LEADER', 'ELITE', 'MEMBER')),
    weekly_contribution     BIGINT NOT NULL DEFAULT 0 CHECK (weekly_contribution >= 0),
    total_contribution      BIGINT NOT NULL DEFAULT 0 CHECK (total_contribution >= 0),
    joined_at               TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (guild_id, character_id)
);

CREATE INDEX idx_guild_members_role ON guild_members(guild_id, member_role);

-- ============================================================================
-- 19. BẢNG BẢN CHỤP LƯU TRỮ NHÂN VẬT XOAY VÒNG (character_save_snapshots)
-- Dùng cho phục hồi sau sự cố máy chủ sập và kiểm toán CSKH
-- ============================================================================
CREATE TABLE character_save_snapshots (
    snapshot_id             BIGSERIAL PRIMARY KEY,
    character_id            UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    fencing_epoch           BIGINT NOT NULL CHECK (fencing_epoch >= 1),
    snapshot_reason         VARCHAR(32) NOT NULL
                            CHECK (snapshot_reason IN ('PERIODIC_60S', 'ZONE_TRANSFER', 'LOGOUT', 'LEVEL_UP', 'ADMIN_BACKUP')),
    zone_id                 VARCHAR(64) NOT NULL,
    pos_x                   DOUBLE PRECISION NOT NULL,
    pos_y                   DOUBLE PRECISION NOT NULL,
    pos_z                   DOUBLE PRECISION NOT NULL,
    state_payload           JSONB NOT NULL,                     -- Bản chụp nén Attributes, Cooldowns, Active Buffs
    payload_sha256          CHAR(64) NOT NULL,
    server_id               VARCHAR(64) NOT NULL,
    created_at              TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_char_snapshots_latest ON character_save_snapshots(character_id, created_at DESC);

COMMIT;
