# 04. API, gRPC & Event Bus Contracts

**Phân hệ:** `02_TECHNICAL_ARCHITECTURE` · **Tài liệu:** `04_API_and_Event_Contracts.md`  
**Trạng thái:** Hợp đồng giao tiếp chuẩn giữa UE5 Client, UE5 Dedicated Server và Go Backend

---

## 1. Phân Luồng Giao Thức Truyền Thông (`Transport Selection Rules`)

1. **UE5 Client ↔ UE5 Dedicated Server:** **UDP (`UE NetDriver / Iris`)** — dùng độc quyền cho di chuyển, bay 3D, kích hoạt kỹ năng GAS, đồng bộ máu/khí và vị trí thực thể.
2. **UE5 Dedicated Server ↔ Go Backend (`ziu-gamesvc`):** **Internal mTLS gRPC (Protobuf v3)** — tốc độ tuần tự hóa nhị phân cực nhanh, kiểu dữ liệu chặt chẽ và hỗ trợ `Server-Streaming` để Backend đẩy ngược thông báo trang bị mới sang Dedicated Server tức thì.
3. **UE5 Client ↔ Go Edge Gateway (`ziu-gateway`):**
   - **HTTPS REST (JSON):** Đăng nhập, lấy danh sách nhân vật, tạo nhân vật, yêu cầu vào thế giới (`EnterWorld`), thao tác Shop/Pity Exchange/Enhance ngoài giao tranh.
   - **WSS (WebSocket JSON Envelope):** Kênh bất đồng bộ liên vùng cho Chat đa kênh, lời mời/trạng thái Party, thông báo Bang hội, thông báo xuất hiện *World Boss* và kết quả nhận thưởng Loot.
4. **Go Modules / Dedicated Server Event Ingestion ↔ Background Workers:** **NATS JetStream (`ZIU_EVENTS`)** — đảm bảo không mất phần thưởng ngay cả khi hàng chục Boss bị hạ cùng một giây.

---

## 2. Hợp Đồng Nội Bộ `gRPC / Protobuf v3` (`pkg/proto/ziu/internal/v1/world_backend.proto`)

```protobuf
syntax = "proto3";

package ziu.internal.v1;

option go_package = "github.com/zenith-mmo/ziu-backend/pkg/proto/internalv1;internalv1";

// ============================================================================
// 1. WORLD COORDINATOR & LEASE FENCING SERVICE
// ============================================================================
service WorldCoordinatorService {
  // Dedicated Server gửi nhịp tim mỗi 5 giây báo cáo tải CPU & số người chơi
  rpc ReportServerHeartbeat(ServerHeartbeatRequest) returns (ServerHeartbeatResponse);

  // Khi UE5 Client kết nối vào Dedicated Server kèm ticket, Server gọi hàm này để chiếm quyền sở hữu
  rpc ClaimCharacterLease(ClaimCharacterLeaseRequest) returns (ClaimCharacterLeaseResponse);

  // Gia hạn khóa sở hữu mỗi 15 giây cho toàn bộ nhân vật đang online trên Zone Server đó
  rpc RenewCharacterLeases(RenewCharacterLeasesRequest) returns (RenewCharacterLeasesResponse);

  // Khởi tạo bàn giao nhân vật sang Zone khác hoặc vào Boss Instance
  rpc PrepareZoneTransfer(PrepareZoneTransferRequest) returns (PrepareZoneTransferResponse);

  // Giải phóng quyền sở hữu ngay khi người chơi thoát game sạch sẽ
  rpc ReleaseCharacterLease(ReleaseCharacterLeaseRequest) returns (ReleaseCharacterLeaseResponse);
}

message ServerHeartbeatRequest {
  string server_instance_id = 1;
  string zone_id = 2;
  int32 channel_index = 3;
  string public_udp_endpoint = 4; // VD: "203.0.113.10:7777"
  int32 current_players = 5;
  int32 max_players = 6;
  float server_frame_ms = 7;
}

message ServerHeartbeatResponse {
  bool drain_requested = 1; // True nếu node sắp bảo trì, không nhận thêm người mới
}

message CharacterRuntimeSnapshot {
  string character_id = 1;
  uint64 lease_epoch = 2;
  string zone_id = 3;
  double pos_x = 4;
  double pos_y = 5;
  double pos_z = 6;
  float rot_yaw = 7;
  int64 current_hp = 8;
  int64 current_chi = 9;
  int64 pending_exp_delta = 10;
  bytes runtime_buffs_json = 11;
}

message ClaimCharacterLeaseRequest {
  string server_instance_id = 1;
  string connect_ticket = 2;
}

message ClaimCharacterLeaseResponse {
  uint64 assigned_lease_epoch = 1;
  CharacterFullLoadout character = 2;
}

message CharacterFullLoadout {
  string character_id = 1;
  string account_id = 2;
  string character_name = 3;
  string race = 4;
  int32 level = 5;
  int64 exp = 6;
  string guild_id = 7;
  string party_id = 8;
  map<string, double> computed_attributes = 9; // Tổng hợp sẵn Core + Equip + Potential từ Backend
  repeated string unlocked_skill_ids = 10;
  repeated string unlocked_ascension_ids = 11;
  string active_ascension_id = 12;
  bytes visual_equipment_json = 13;            // Phục vụ đồng bộ ngoại hình / Transmog cho người khác
  CharacterRuntimeSnapshot last_snapshot = 14;
}

message RenewCharacterLeasesRequest {
  string server_instance_id = 1;
  map<string, uint64> character_epochs = 2;    // map<character_id, lease_epoch>
}

message RenewCharacterLeasesResponse {
  repeated string evicted_character_ids = 1;   // Server bắt buộc kick ngay các ID bị mất lease!
}

message PrepareZoneTransferRequest {
  string source_server_id = 1;
  string target_zone_id = 2;
  string preferred_party_id = 3;
  CharacterRuntimeSnapshot final_snapshot = 4;
}

message PrepareZoneTransferResponse {
  string target_udp_endpoint = 1;
  string transfer_ticket = 2;
}

message ReleaseCharacterLeaseRequest {
  string server_instance_id = 1;
  CharacterRuntimeSnapshot final_snapshot = 2;
}

message ReleaseCharacterLeaseResponse {
  bool persisted = 1;
}

// ============================================================================
// 2. CHARACTER PERSISTENCE & LIVE SYNC STREAM
// ============================================================================
service CharacterPersistenceService {
  // Lưu theo lô mỗi 60 giây cho toàn bộ người chơi trên Zone Server
  rpc BatchCheckpointSave(BatchCheckpointSaveRequest) returns (BatchCheckpointSaveResponse);

  // Luồng đẩy ngược từ Go Backend về Dedicated Server khi người chơi thay đồ/cường hóa/lên cấp
  rpc SubscribeCharacterStateUpdates(ServerSubscriptionRequest) returns (stream CharacterServerUpdateEvent);
}

message BatchCheckpointSaveRequest {
  string server_instance_id = 1;
  repeated CharacterRuntimeSnapshot snapshots = 2;
}

message BatchCheckpointSaveResponse {
  repeated string rejected_character_ids = 1;  // Các nhân vật có lease_epoch không còn hợp lệ
}

message ServerSubscriptionRequest {
  string server_instance_id = 1;
}

message CharacterServerUpdateEvent {
  string character_id = 1;
  string update_type = 2;                      // EQUIP_CHANGED, STAT_ALLOCATED, ASCENSION_UNLOCKED, PARTY_CHANGED
  map<string, double> recomputed_attributes = 3;
  bytes payload_json = 4;
}
```

---

## 3. Hợp Đồng Client Gateway (`HTTPS REST` & `WSS`)

### 3.1. Các Endpoint REST Trọng Tâm (`/api/v1/...`)

| Phương thức & Đường dẫn | Xác thực | Chức năng & Quy tắc Idempotency |
| :--- | :---: | :--- |
| `POST /api/v1/auth/login` | Public | Trả về `access_token` (JWT `15m`), `refresh_token` (`7d`) và thông tin tài khoản. |
| `POST /api/v1/auth/refresh` | Refresh Token | Cấp mới cặp token theo cơ chế xoay vòng (`Token Rotation`). |
| `GET /api/v1/characters` | Bearer JWT | Lấy danh sách nhân vật của tài khoản kèm ngoại hình hiển thị ở màn hình chọn nhân vật. |
| `POST /api/v1/characters` | Bearer JWT | Tạo nhân vật mới (`race`, `character_name`, `appearance_data`), khởi tạo đồng bộ 5 bảng liên quan. |
| `POST /api/v1/world/enter` | Bearer JWT | Chọn `character_id` → `WorldCoordinator` cấp `udp_endpoint` + `connect_ticket` để UE5 chạy `ClientTravel`. |
| `POST /api/v1/inventory/equip` | Bearer JWT | Mặc hoặc tháo trang bị (cập nhật DB rồi bắn sự kiện qua gRPC stream về Dedicated Server cập nhật GAS). |
| `POST /api/v1/crafting/enhance` | Bearer JWT + Header `Idempotency-Key` | Thực hiện 1 lượt cường hóa trang bị an toàn, trả về kết quả thành công/bảo hiểm và số dư `Kora` mới. |
| `POST /api/v1/crafting/reforge` | Bearer JWT + Header `Idempotency-Key` | Khóa tối đa 1 dòng phụ và quay mới các dòng còn lại. |
| `POST /api/v1/pity-shop/redeem` | Bearer JWT + Header `Idempotency-Key` | Đổi `Boss Core Tokens` lấy trang bị mục tiêu trong giao dịch nguyên tử. |

### 3.2. Cấu Trúc Khung Bản Tin WebSocket (`wss://<gateway>/ws/v1`)

Mọi gói tin qua WebSocket tuân thủ khung JSON đồng nhất:

```json
{
  "op": "loot.granted",
  "seq": 10842,
  "ts": "2026-11-19T10:45:12.410Z",
  "data": {
    "source_type": "REGIONAL_BOSS",
    "source_name": "Vanguard Colossus Kael",
    "kora_gained": 1850,
    "exp_gained": 4200,
    "pity_tokens": {
      "boss_pool_id": "boss_cragspire_kael",
      "added": 2,
      "new_balance": 10
    },
    "items": [
      {
        "instance_id": "b7e21c10-91a4-4f12-8c9d-33f21a904e11",
        "item_def_id": "eq_val_vanguard_gauntlets_t1",
        "rarity": "EPIC",
        "rolled_affixes": [
          { "affix_id": "pct_chi_atk", "tier": 3, "value": 9.5 },
          { "affix_id": "crit_rate", "tier": 2, "value": 4.8 },
          { "affix_id": "ascension_eff", "tier": 3, "value": 11.0 }
        ]
      }
    ]
  }
}
```

---

## 4. Hợp Đồng Sự Kiện `NATS JetStream` (`Stream: ZIU_EVENTS`)

Mọi bản tin đẩy vào NATS JetStream **bắt buộc đính kèm Header `Nats-Msg-Id` bằng `event_idempotency_key`** (cửa sổ khử trùng lặp của JetStream bật ở mức `10 phút`) kết hợp ràng buộc `UNIQUE` trong PostgreSQL để đảm bảo **Exactly-Once Business Effect**:

### Mẫu Sự kiện Hạ Boss (`Subject: combat.boss.defeated.v1`)

```json
{
  "event_id": "kill_zone_cragspire_ch1_kael_1795059912",
  "occurred_at": "2026-11-19T10:45:12.105Z",
  "server_instance_id": "ds-cragspire-ch1-01",
  "zone_id": "zone_cragspire",
  "boss_def_id": "boss_cragspire_kael",
  "boss_level": 15,
  "combat_duration_sec": 142.6,
  "participants": [
    {
      "character_id": "550e8400-e29b-41d4-a716-446655440000",
      "character_level": 13,
      "party_id": "881a9200-b12c-49a1-9910-112233445566",
      "damage_ratio": 0.34,
      "stagger_ratio": 0.40,
      "objective_ratio": 0.50,
      "contribution_score": 0.376,
      "present_at_death": true
    }
  ]
}
```

### Bảng Danh Mục Các Subjects Chuẩn Trên `ZIU_EVENTS`

| Subject | Producer | Consumer Module | Hành động xử lý |
| :--- | :--- | :--- | :--- |
| `combat.mob.killed.v1` | UE5 Zone Server *(gộp lô 1s cho quái thường)* | `loot`, `progression`, `quest` | Cộng EXP, `Kora`, cập nhật tiến độ nhiệm vụ diệt quái và quay vật phẩm rơi. |
| `combat.boss.defeated.v1` | UE5 Zone / Instance Server | `loot`, `progression`, `social` | Tính bậc đóng góp, phát `Personal Loot`, cộng `Boss Pity Token`, phát thông báo vinh danh nếu rơi đồ `Legendary/Mythic`. |
| `economy.trade.settled.v1` | `economy` Module | `social`, `analytics` | Gửi biên lai qua WebSocket/Mail cho 2 bên và chạy bộ dò giao dịch bất thường (RMT detector). |
| `progression.level.up.v1` | `progression` Module | `character`, `worldcoord` | Mở khóa nội dung theo mốc cấp và đồng bộ chỉ số mới sang Dedicated Server. |
