# 03. Production Database Schema & Persistence Strategy

**Phân hệ:** `02_TECHNICAL_ARCHITECTURE` · **Tài liệu:** `03_Database_Schema_and_Persistence.md`  
**Hệ quản trị CSDL:** PostgreSQL 16 (Primary ACID Store) + Redis 7 (Ephemeral Lease & Hot Cache)

---

## 1. Bảy Quy Tắc Toàn Vẹn Dữ Liệu Bắt Buộc (`Persistence Golden Rules`)

1. **PostgreSQL là Nguồn Chân Lý Duy Nhất (Single Source of Truth)** cho mọi dữ liệu lâu dài (Tài khoản, Nhân vật, Trang bị, Tiền tệ, Nhiệm vụ, Bang hội). Redis bị xóa trắng không được phép làm mất bất kỳ món đồ hay đồng `Kora` nào của người chơi.
2. **Mọi vật phẩm trong túi đồ là một Thực thể Độc nhất (`Unique Item Instance UUID`):** Không bao giờ lưu trang bị bằng chuỗi tên đơn thuần; mỗi món trang bị có `id UUID` riêng, khóa ngoại trỏ về `item_definitions(id)`, mảng `rolled_affixes JSONB` riêng và bộ đếm `version BIGINT` cho **Optimistic Locking**.
3. **Kế toán Kép / Sổ cái Bất biến (`Immutable Wallet Ledger`):** Mọi thay đổi số dư `kora` hoặc `mastery_marks` trên bảng `character_wallets` bắt buộc ghi kèm một bản ghi không thể sửa xóa vào `wallet_ledger` trong cùng một SQL Transaction.
4. **Chống Ghi Đè Cũ Bằng `lease_epoch`:** Mọi câu lệnh lưu trạng thái nhân vật định kỳ từ Dedicated Server đều phải kèm điều kiện `WHERE id = $1 AND last_lease_epoch = $2`. Nếu trả về `0 rows affected`, Dedicated Server đó lập tức biết mình đã mất quyền sở hữu và tự hủy phiên nhân vật cục bộ.
5. **Chống Deadlock Khi Giao Dịch Hai Người Chơi:** Mọi giao dịch liên quan đến 2 `character_id` (như `Direct Trade`) bắt buộc sắp xếp UUID theo thứ tự từ điển tăng dần (`char_id_min < char_id_max`) trước khi thực hiện `SELECT ... FOR UPDATE`.
6. **Schema Versioning:** Quản lý 100% thay đổi cấu trúc bảng bằng migration tuần tự (`golang-migrate` hoặc `goose`), đồng thời lưu cột `schema_version` trong snapshot nhân vật.
7. **Nhịp Lưu Trữ Hợp Lý (`Save Cadence`):**
   - **Tức thời (`Immediate ACID Commit`):** Nhặt đồ `Rare+`, nhận thưởng Boss, cường hóa, Reforge, mua bán, giao dịch, lên Level, mở khóa kỹ năng/Hóa Cảnh, chuyển Zone, đăng xuất.
   - **Định kỳ theo Lô (`Batched Checkpoint mỗi 60 giây`):** Vị trí `X/Y/Z`, lượng `EXP` lẻ từ quái thường, lượng `HP/Chi` hiện tại.

---

## 2. Lược Đồ SQL Sản Xuất Hoàn Chỉnh — 19 Bảng (`PostgreSQL 16 DDL`)

Đoạn mã SQL dưới đây được sắp xếp đúng thứ tự phụ thuộc khóa ngoại để có thể thực thi trực tiếp làm migration khởi tạo `000001_init_schema.up.sql`:

```sql
-- Kích hoạt tiện ích sinh UUID v4 chuẩn mật mã
CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- ============================================================================
-- 1. ACCOUNTS & PLAYER SESSIONS
-- ============================================================================
CREATE TABLE accounts (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    username        VARCHAR(32) UNIQUE NOT NULL,
    email           VARCHAR(255) UNIQUE NOT NULL,
    password_hash   TEXT NOT NULL,                      -- Argon2id hash
    account_status  VARCHAR(16) NOT NULL DEFAULT 'ACTIVE'
                    CHECK (account_status IN ('ACTIVE', 'SUSPENDED', 'BANNED')),
    last_login_at   TIMESTAMPTZ,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE player_sessions (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    account_id          UUID NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    refresh_token_hash  CHAR(64) UNIQUE NOT NULL,       -- SHA-256 của Refresh Token
    client_ip           INET,
    hardware_fingerprint VARCHAR(128),
    expires_at          TIMESTAMPTZ NOT NULL,
    revoked_at          TIMESTAMPTZ,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX idx_sessions_account_active ON player_sessions(account_id) WHERE revoked_at IS NULL;

-- ============================================================================
-- 2. GUILDS & CHARACTERS (Xử lý vòng khóa ngoại an toàn bằng ALTER TABLE)
-- ============================================================================
CREATE TABLE guilds (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    guild_name          VARCHAR(32) UNIQUE NOT NULL,
    guild_tag           VARCHAR(6) UNIQUE NOT NULL,
    leader_character_id UUID,                           -- Ràng buộc FK sau khi tạo bảng characters
    guild_level         SMALLINT NOT NULL DEFAULT 1 CHECK (guild_level BETWEEN 1 AND 20),
    guild_exp           BIGINT NOT NULL DEFAULT 0 CHECK (guild_exp >= 0),
    guild_kora_vault    BIGINT NOT NULL DEFAULT 0 CHECK (guild_kora_vault >= 0),
    announcement        VARCHAR(500) NOT NULL DEFAULT '',
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE characters (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    account_id          UUID NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    character_name      VARCHAR(24) UNIQUE NOT NULL,
    race                VARCHAR(16) NOT NULL
                        CHECK (race IN ('VALORAN', 'TERRAN', 'SYLVANAR', 'CRYON', 'AETHERBORN', 'MACHINA')),
    appearance_data     JSONB NOT NULL DEFAULT '{}'::jsonb,
    level               SMALLINT NOT NULL DEFAULT 1 CHECK (level BETWEEN 1 AND 200),
    exp                 BIGINT NOT NULL DEFAULT 0 CHECK (exp >= 0),
    unspent_stat_points INT NOT NULL DEFAULT 0 CHECK (unspent_stat_points >= 0),
    combat_resonance    BIGINT NOT NULL DEFAULT 100,
    current_planet      VARCHAR(32) NOT NULL DEFAULT 'terra_prime',
    current_zone        VARCHAR(32) NOT NULL DEFAULT 'zone_cragspire',
    pos_x               DOUBLE PRECISION NOT NULL DEFAULT 0.0,
    pos_y               DOUBLE PRECISION NOT NULL DEFAULT 0.0,
    pos_z               DOUBLE PRECISION NOT NULL DEFAULT 100.0,
    rot_yaw             REAL NOT NULL DEFAULT 0.0,
    guild_id            UUID REFERENCES guilds(id) ON DELETE SET NULL,
    last_lease_epoch    BIGINT NOT NULL DEFAULT 0,      -- Fencing Token chống ghi đè / nhân đôi!
    schema_version      SMALLINT NOT NULL DEFAULT 1,
    last_played_at      TIMESTAMPTZ,
    deleted_at          TIMESTAMPTZ,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX idx_characters_account ON characters(account_id) WHERE deleted_at IS NULL;
CREATE INDEX idx_characters_zone ON characters(current_zone);

ALTER TABLE guilds
    ADD CONSTRAINT fk_guilds_leader
    FOREIGN KEY (leader_character_id) REFERENCES characters(id) ON DELETE RESTRICT;

CREATE TABLE guild_members (
    guild_id            UUID NOT NULL REFERENCES guilds(id) ON DELETE CASCADE,
    character_id        UUID PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,
    role                VARCHAR(16) NOT NULL DEFAULT 'MEMBER'
                        CHECK (role IN ('MASTER', 'VICE_MASTER', 'OFFICER', 'MEMBER', 'INITIATE')),
    weekly_merits       BIGINT NOT NULL DEFAULT 0,
    total_merits        BIGINT NOT NULL DEFAULT 0,
    joined_at           TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX idx_guild_members_guild ON guild_members(guild_id);

-- ============================================================================
-- 3. ATTRIBUTES, POTENTIAL, SKILLS & ASCENSIONS
-- ============================================================================
CREATE TABLE character_attributes (
    character_id        UUID PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,
    vitality            INT NOT NULL DEFAULT 10 CHECK (vitality >= 1),
    spirit              INT NOT NULL DEFAULT 10 CHECK (spirit >= 1),
    endurance           INT NOT NULL DEFAULT 10 CHECK (endurance >= 1),
    strength            INT NOT NULL DEFAULT 10 CHECK (strength >= 1),
    fortitude           INT NOT NULL DEFAULT 10 CHECK (fortitude >= 1),
    saved_current_hp    BIGINT NOT NULL DEFAULT 500,
    saved_current_chi   BIGINT NOT NULL DEFAULT 100,
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE character_potential (
    character_id            UUID PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,
    ascension_efficiency    SMALLINT NOT NULL DEFAULT 0 CHECK (ascension_efficiency BETWEEN 0 AND 100),
    chi_control             SMALLINT NOT NULL DEFAULT 0 CHECK (chi_control BETWEEN 0 AND 100),
    energy_capacity         SMALLINT NOT NULL DEFAULT 0 CHECK (energy_capacity BETWEEN 0 AND 100),
    flight_efficiency       SMALLINT NOT NULL DEFAULT 0 CHECK (flight_efficiency BETWEEN 0 AND 100),
    astral_resistance       SMALLINT NOT NULL DEFAULT 0 CHECK (astral_resistance BETWEEN 0 AND 100),
    completed_trials        JSONB NOT NULL DEFAULT '[]'::jsonb,
    updated_at              TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE character_skills (
    character_id        UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    skill_id            VARCHAR(64) NOT NULL,
    skill_rank          SMALLINT NOT NULL DEFAULT 1 CHECK (skill_rank BETWEEN 1 AND 10),
    equipped_slot       SMALLINT CHECK (equipped_slot BETWEEN 1 AND 5), -- 1..4 Active, 5 Ultimate, NULL nếu không gắn
    unlocked_at         TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (character_id, skill_id),
    UNIQUE (character_id, equipped_slot)
);

CREATE TABLE character_ascensions (
    character_id        UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    ascension_id        VARCHAR(64) NOT NULL,           -- VD: 'asc_valoran_solar_surge'
    mastery_level       SMALLINT NOT NULL DEFAULT 1 CHECK (mastery_level BETWEEN 1 AND 5),
    is_active_slot      BOOLEAN NOT NULL DEFAULT FALSE, -- Nhánh biến hình đang gắn vào phím C
    unlocked_at         TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (character_id, ascension_id)
);

-- ============================================================================
-- 4. ITEM DEFINITIONS, INVENTORY & EQUIPMENT
-- ============================================================================
CREATE TABLE item_definitions (
    id                  VARCHAR(64) PRIMARY KEY,        -- VD: 'eq_val_vanguard_chest_t1'
    display_name        VARCHAR(128) NOT NULL,
    item_category       VARCHAR(24) NOT NULL
                        CHECK (item_category IN ('EQUIPMENT', 'MATERIAL', 'CONSUMABLE', 'TOKEN', 'QUEST')),
    equip_slot          VARCHAR(16)
                        CHECK (equip_slot IN ('HEAD', 'BODY', 'LEGS', 'HANDS', 'FEET', 'ACC_1', 'ACC_2', 'SPECIAL')),
    rarity              VARCHAR(16) NOT NULL
                        CHECK (rarity IN ('COMMON', 'UNCOMMON', 'RARE', 'EPIC', 'LEGENDARY', 'MYTHIC')),
    required_level      SMALLINT NOT NULL DEFAULT 1,
    item_level          SMALLINT NOT NULL DEFAULT 1,
    set_id              VARCHAR(64),
    default_bind_rule   VARCHAR(16) NOT NULL DEFAULT 'BIND_ON_EQUIP'
                        CHECK (default_bind_rule IN ('UNBOUND', 'BIND_ON_EQUIP', 'BIND_ON_PICKUP')),
    max_stack           INT NOT NULL DEFAULT 1 CHECK (max_stack >= 1),
    vendor_sell_kora    BIGINT NOT NULL DEFAULT 1 CHECK (vendor_sell_kora >= 0),
    base_stats          JSONB NOT NULL DEFAULT '{}'::jsonb,
    affix_pool_config   JSONB NOT NULL DEFAULT '{}'::jsonb
);

CREATE TABLE inventory_items (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    character_id        UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    item_def_id         VARCHAR(64) NOT NULL REFERENCES item_definitions(id),
    container_type      VARCHAR(16) NOT NULL DEFAULT 'BAG'
                        CHECK (container_type IN ('BAG', 'EQUIPPED', 'STASH', 'MAIL_ESCROW', 'TRADE_ESCROW')),
    slot_index          SMALLINT NOT NULL CHECK (slot_index >= 0),
    quantity            INT NOT NULL DEFAULT 1 CHECK (quantity > 0),
    enhance_level       SMALLINT NOT NULL DEFAULT 0 CHECK (enhance_level BETWEEN 0 AND 15),
    artisan_pity_bp     SMALLINT NOT NULL DEFAULT 0 CHECK (artisan_pity_bp BETWEEN 0 AND 10000), -- Basis points (10000 = 100%)
    bind_state          VARCHAR(16) NOT NULL DEFAULT 'UNBOUND'
                        CHECK (bind_state IN ('UNBOUND', 'BIND_ON_EQUIP', 'BOUND')),
    rolled_affixes      JSONB NOT NULL DEFAULT '[]'::jsonb,
    socketed_gems       JSONB NOT NULL DEFAULT '[]'::jsonb,
    transmog_def_id     VARCHAR(64) REFERENCES item_definitions(id),
    version             BIGINT NOT NULL DEFAULT 1,      -- Optimistic Lock Version
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (character_id, container_type, slot_index)
);
CREATE INDEX idx_inventory_char ON inventory_items(character_id);
CREATE INDEX idx_inventory_affixes_gin ON inventory_items USING GIN (rolled_affixes);

CREATE TABLE character_equipment (
    character_id        UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    equip_slot          VARCHAR(16) NOT NULL
                        CHECK (equip_slot IN ('HEAD', 'BODY', 'LEGS', 'HANDS', 'FEET', 'ACC_1', 'ACC_2', 'SPECIAL')),
    inventory_item_id   UUID UNIQUE NOT NULL REFERENCES inventory_items(id) ON DELETE RESTRICT,
    equipped_at         TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (character_id, equip_slot)
);

-- ============================================================================
-- 5. WALLETS, IMMUTABLE LEDGER, TRADES, LOOT & PITY
-- ============================================================================
CREATE TABLE character_wallets (
    character_id        UUID PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,
    kora                BIGINT NOT NULL DEFAULT 0 CHECK (kora >= 0),
    mastery_marks       BIGINT NOT NULL DEFAULT 0 CHECK (mastery_marks >= 0),
    version             BIGINT NOT NULL DEFAULT 1,
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE wallet_ledger (
    id                  BIGSERIAL PRIMARY KEY,
    idempotency_key     VARCHAR(128) UNIQUE NOT NULL,
    character_id        UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    currency_type       VARCHAR(24) NOT NULL CHECK (currency_type IN ('KORA', 'MASTERY_MARKS')),
    delta_amount        BIGINT NOT NULL CHECK (delta_amount <> 0),
    balance_after       BIGINT NOT NULL CHECK (balance_after >= 0),
    reason_code         VARCHAR(32) NOT NULL,           -- MOB_DROP, BOSS_REWARD, ENHANCE_SINK, REFORGE_SINK, TRADE, VENDOR
    reference_id        VARCHAR(128),
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX idx_wallet_ledger_char_time ON wallet_ledger(character_id, created_at DESC);

CREATE TABLE trade_transactions (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    initiator_char_id   UUID NOT NULL REFERENCES characters(id),
    recipient_char_id   UUID NOT NULL REFERENCES characters(id),
    initiator_kora      BIGINT NOT NULL DEFAULT 0 CHECK (initiator_kora >= 0),
    recipient_kora      BIGINT NOT NULL DEFAULT 0 CHECK (recipient_kora >= 0),
    tax_kora_burned     BIGINT NOT NULL DEFAULT 0 CHECK (tax_kora_burned >= 0),
    transferred_items   JSONB NOT NULL DEFAULT '[]'::jsonb,
    trade_status        VARCHAR(16) NOT NULL CHECK (trade_status IN ('COMPLETED', 'CANCELLED', 'ABORTED')),
    completed_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CHECK (initiator_char_id <> recipient_char_id)
);

CREATE TABLE loot_history (
    id                  BIGSERIAL PRIMARY KEY,
    event_idempotency   VARCHAR(128) NOT NULL,
    character_id        UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    source_type         VARCHAR(24) NOT NULL,           -- NORMAL_MOB, ELITE, REGIONAL_BOSS, WORLD_BOSS, PITY_SHOP
    source_def_id       VARCHAR(64) NOT NULL,
    item_instance_id    UUID,
    item_def_id         VARCHAR(64) NOT NULL REFERENCES item_definitions(id),
    quantity            INT NOT NULL DEFAULT 1,
    rng_audit_seed      BIGINT,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (event_idempotency, character_id, item_def_id)
);

CREATE TABLE boss_pity_progress (
    character_id        UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    boss_pool_id        VARCHAR(64) NOT NULL,           -- VD: 'boss_cragspire_kael'
    token_balance       INT NOT NULL DEFAULT 0 CHECK (token_balance >= 0),
    total_kills         INT NOT NULL DEFAULT 0 CHECK (total_kills >= 0),
    kills_since_epic    INT NOT NULL DEFAULT 0 CHECK (kills_since_epic >= 0),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (character_id, boss_pool_id)
);

-- ============================================================================
-- 6. QUESTS & SAVE SNAPSHOTS
-- ============================================================================
CREATE TABLE character_quests (
    character_id        UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    quest_id            VARCHAR(64) NOT NULL,
    quest_state         VARCHAR(16) NOT NULL
                        CHECK (quest_state IN ('ACTIVE', 'READY_TO_TURN_IN', 'COMPLETED')),
    objectives_progress JSONB NOT NULL DEFAULT '{}'::jsonb,
    completed_at        TIMESTAMPTZ,
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (character_id, quest_id)
);

CREATE TABLE character_save_snapshots (
    id                  BIGSERIAL PRIMARY KEY,
    character_id        UUID NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    lease_epoch         BIGINT NOT NULL,
    snapshot_reason     VARCHAR(24) NOT NULL,           -- ZONE_TRANSFER, LOGOUT, PERIODIC_CHECKPOINT, CRASH_RECOVERY
    zone_id             VARCHAR(32) NOT NULL,
    runtime_payload     JSONB NOT NULL,                 -- Cooldowns còn lại, Active Buffs, Party Context
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (character_id, lease_epoch, snapshot_reason)
);
CREATE INDEX idx_snapshots_char_latest ON character_save_snapshots(character_id, lease_epoch DESC);
```

---

## 3. Cấu Trúc Định Danh Khóa Trên Redis 7 (`Redis Keyspace Specification`)

| Mẫu Khóa (`Key Pattern`) | Kiểu Dữ Liệu | TTL | Mục Đích Nghiệp Vụ |
| :--- | :---: | :---: | :--- |
| `ziu:lease:char:{char_uuid}` | `HASH` (`server_id`, `epoch`, `zone_id`) | `45s` *(Gia hạn mỗi 15s)* | **Single-Owner Lease Lock.** Đảm bảo chỉ 1 Dedicated Server có quyền giữ phiên nhân vật. |
| `ziu:ticket:transfer:{token_id}` | `STRING` (JSON Payload) | `20s` *(Xóa ngay bằng `GETDEL`)* | Vé một lần khi kết nối vào Dedicated Server hoặc chuyển Zone. |
| `ziu:zone:nodes:{zone_id}` | `HASH` (`instance_id -> JSON load`) | Cập nhật mỗi `5s` | Danh sách các Channel sống, số lượng CCU hiện tại và địa chỉ `IP:Port` để cân bằng tải. |
| `ziu:party:{party_uuid}` | `HASH` | `2h` sau khi rỗng | Trạng thái tổ đội thời gian thực (Leader, danh sách thành viên, quy tắc chia sẻ). |
| `ziu:ratelimit:gw:{account_id}:{route}` | `STRING` (Counter) | `1s – 60s` | Chống spam API Cường hóa, Giao dịch, Chat tại Edge Gateway. |
