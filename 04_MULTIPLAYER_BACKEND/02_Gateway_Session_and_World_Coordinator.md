# 02. Edge Gateway, Session Security & World Coordinator

**Phân hệ:** `04_MULTIPLAYER_BACKEND` · **Tài liệu:** `02_Gateway_Session_and_World_Coordinator.md`  
**Modules đích:** `cmd/gateway/`, `internal/modules/auth/`, `internal/modules/worldcoord/`

---

## 1. Luồng Xác Thực & Chống Đăng Nhập Kép (`Authentication & Single Active Session`)

1. **Đăng nhập (`POST /api/v1/auth/login`):**
   - Kiểm tra mật khẩu bằng `Argon2id` (`time=2, memory=64MB, threads=2`).
   - Phát hành `Access JWT` (`EdDSA / Ed25519` ký bất đối xứng để `gateway` và `gamesvc` xác thực không cần gọi chéo, thời hạn `15 phút`, chứa `sub = account_id`, `sid = session_id`) và `Refresh Token` ngẫu nhiên `256-bit` lưu bản băm `SHA-256` vào bảng `player_sessions`.
2. **Chống Đăng Nhập Trùng Tài Khoản (`Duplicate Login Kick`):**
   - Khi cùng một `account_id` đăng nhập ở máy tính mới và bấm `EnterWorld`:
     - `worldcoord` tra cứu xem tài khoản đó có nhân vật nào đang giữ khóa `ziu:lease:char:{char_id}` trên Redis hay không.
     - Nếu đang online trên máy cũ: gửi lệnh thu hồi phiên ngay lập tức (khiến lần `RenewCharacterLeases` kế tiếp hoặc luồng đẩy sự kiện gRPC báo cho Dedicated Server lưu trạng thái nhân vật cũ, ngắt kết nối máy cũ, rồi mới cấp vé cho máy mới sau `1.5 giây` an toàn!).

---

## 2. Bộ Điều Phối Thế Giới & Thuật Toán Chọn Kênh (`World Coordinator Routing`)

Mỗi process UE5 Dedicated Server khi khởi chạy sẽ gửi `ReportServerHeartbeat` mỗi `5 giây` về `worldcoord`. Bộ điều phối chọn máy chủ đích cho người chơi theo thứ tự ưu tiên sau:

1. **Ưu tiên Đồng đội (`Party Affinity`):** Nếu người chơi đang nằm trong một `Party` và có thành viên tổ đội đang ở `Zone X - Channel k`, ưu tiên đưa vào đúng `Channel k` miễn là số người hiện tại `< HardCap (100)`.
2. **Điểm Ngọt Cộng Đồng (`Social Density Sweet-Spot`):** Thay vì rải đều 20 người ra 4 kênh vắng vẻ (mỗi kênh 5 người làm mất cảm giác MMORPG!), bộ điều phối dồn người chơi mới vào kênh đầu tiên đang có lượng người `< SoftCap (80)` và thời gian xử lý frame `server_frame_ms < 26.0 ms`.
3. **Tự động Mở Kênh Mới (`Auto-Scale Trigger`):** Khi mọi kênh hiện có của `zone_cragspire` đều vượt ngưỡng `75% SoftCap` (`60 người`), `worldcoord` gọi Agones Allocator API khởi động sẵn thêm 1 instance ấm (`Warm Spare`) sang trạng thái `Allocated` trước khi kênh cũ chạm trần.

---

## 3. Kịch Bản `Redis Lua` Nguyên Tử Cho `Lease Fencing`

Để tuyệt đối không xảy ra lỗi tranh chấp thời gian (*Time-of-Check to Time-of-Use race condition*) giữa hai Dedicated Server khi nhận bàn giao nhân vật, các thao tác trên khóa phiên Redis bắt buộc thực thi bằng **Lua Script nguyên tử**:

### 3.1. Kịch bản Chiếm Quyền Sở Hữu Bằng Vé Một Lần (`claim_character_lease.lua`)

```lua
-- KEYS[1]: ziu:ticket:transfer:{ticket_id}
-- KEYS[2]: ziu:lease:char:{char_id}
-- ARGV[1]: claiming_server_id
-- ARGV[2]: expected_zone_id
-- ARGV[3]: lease_ttl_seconds (45)

local ticket_raw = redis.call("GETDEL", KEYS[1])
if not ticket_raw then
    return { err = "ERR_INVALID_OR_EXPIRED_TICKET" }
end

local ticket = cjson.decode(ticket_raw)
if ticket.target_zone_id ~= ARGV[2] then
    return { err = "ERR_WRONG_TARGET_ZONE" }
end

-- Nâng epoch đơn điệu thêm 1 ngay lúc Dedicated Server đích nhận quyền
local new_epoch = tonumber(ticket.prepared_epoch) + 1

redis.call("HSET", KEYS[2],
    "server_id", ARGV[1],
    "epoch", tostring(new_epoch),
    "zone_id", ARGV[2]
)
redis.call("EXPIRE", KEYS[2], tonumber(ARGV[3]))

return { tostring(new_epoch), ticket.character_id }
```

### 3.2. Kịch bản Gia Hạn Khóa Phiên (`renew_lease.lua` — Gọi mỗi 15 giây)

```lua
-- KEYS[1]: ziu:lease:char:{char_id}
-- ARGV[1]: server_id
-- ARGV[2]: current_epoch
-- ARGV[3]: lease_ttl_seconds (45)

local owner = redis.call("HGET", KEYS[1], "server_id")
local epoch = redis.call("HGET", KEYS[1], "epoch")

if owner ~= ARGV[1] or epoch ~= ARGV[2] then
    return 0 -- Mất quyền sở hữu! Server phải lập tức kick Pawn nhân vật này mà KHÔNG được ghi đè DB!
end

redis.call("EXPIRE", KEYS[1], tonumber(ARGV[3]))
return 1
```

---

## 4. Tổng Hợp Chỉ Số Nhân Vật Tại Backend (`Server-Authoritative Stat Compiler`)

Mỗi khi nhân vật đăng nhập, lên cấp, cộng điểm thuộc tính hoặc thay đổi 1 trong 8 ô trang bị, hàm `character.CompileCharacterStats(charID)` trên Go Backend tính toán lại bảng chỉ số chuẩn:
1. Lấy chỉ số cơ bản theo Chủng tộc (`VALORAN / TERRAN / SYLVANAR`) và `Level`.
2. Cộng quy đổi từ 5 cột bảng `character_attributes` (`VIT, SPI, END, STR, FOR`).
3. Duyệt 8 món đang nằm trong bảng `character_equipment`, nhân hệ số cường hóa $\left(1 + 0.045 \times \text{EnhanceLv} + 0.01 \times \max(0, \text{EnhanceLv} - 10)^2\right)$ lên chỉ số gốc của trang bị, cộng toàn bộ mảng `rolled_affixes` và đếm số lượng từng `set_id` để kích hoạt mốc `2 / 4 / 6 món`.
4. Cộng chỉ số từ bảng `character_potential` và tính điểm tổng hợp **`combat_resonance`**.
5. Đẩy bản đồ `recomputed_attributes` qua luồng `SubscribeCharacterStateUpdates` sang Dedicated Server để `UZIUAbilitySystemComponent` cập nhật trực tiếp `BaseValue` của các `AttributeSet` trong vòng `< 20 ms`!
