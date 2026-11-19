# 01. Go Modular Monolith Backend Structure

**Phân hệ:** `04_MULTIPLAYER_BACKEND` · **Tài liệu:** `01_Go_Modular_Monolith_Structure.md`  
**Ngôn ngữ & Thư viện chuẩn:** Go 1.23+ · `jackc/pgx/v5` + `sqlc` · `redis/go-redis/v9` · `nats-io/nats.go` · `google.golang.org/grpc` · `chi/v5` · `coder/websocket`

---

## 1. Cấu Trúc Thư Mục Chuẩn (`backend/` Repository Layout)

Toàn bộ mã nguồn Go Backend đặt trong thư mục `backend/` với ranh giới nghiêm ngặt giữa các phân hệ nghiệp vụ (`internal/modules/...`) để vận hành nhẹ nhàng như một Monolith trong giai đoạn Vertical Slice/MVP nhưng sẵn sàng tách thành Microservices bất cứ lúc nào:

```text
backend/
├── go.mod                                 # module github.com/zenith-mmo/ziu-backend
├── cmd/
│   ├── gateway/main.go                    # Binary 1: Edge Gateway (HTTPS REST + WSS cho Game Client)
│   ├── gamesvc/main.go                    # Binary 2: Core Modular Monolith (gRPC nội bộ + NATS Consumers)
│   └── migrate/main.go                    # Công cụ chạy SQL Migrations & nạp dữ liệu cấu hình tĩnh
├── migrations/
│   ├── 000001_init_schema.up.sql          # Lược đồ 19 bảng chuẩn từ 02_TECHNICAL_ARCHITECTURE/03
│   └── 000001_init_schema.down.sql
├── gamedata/                              # Dữ liệu JSON tĩnh xuất chung với UE5 (Items, Skills, LootTables, Bosses)
├── pkg/
│   ├── proto/internalv1/                  # Mã Go sinh tự động từ world_backend.proto
│   ├── telemetry/                         # OpenTelemetry, Prometheus Metrics & slog JSON Logger
│   └── dbutil/                            # Trình bao đóng pgx.Tx (WithTx, OrderedUUIDLock)
└── internal/
    ├── platform/
    │   ├── postgres/                      # Khởi tạo pgxpool
    │   ├── redis/                         # Khởi tạo Redis Client & nạp sẵn kịch bản Lua SHA1
    │   └── natsbus/                       # Khởi tạo NATS JetStream Stream ZIU_EVENTS
    └── modules/                           # 12 Phân hệ Nghiệp vụ Độc lập (Domain Modules)
        ├── auth/                          # Đăng ký, Argon2id Login, JWT + Refresh Token Rotation
        ├── account/                       # Trạng thái tài khoản, khóa vi phạm
        ├── character/                     # CRUD nhân vật, cộng điểm thuộc tính, tổng hợp chỉ số
        ├── progression/                   # Công thức EXP/Level, Training Trials, mở khóa Skill & Ascension
        ├── inventory/                     # Túi đồ, Mặc/Tháo 8 ô trang bị, Transmog
        ├── economy/                       # Ví Kora/MasteryMarks, Sổ cái Ledger, Cường hóa, Reforge, Giao dịch
        ├── loot/                          # CSPRNG Roll, Affix Generator, Boss Pity Counter & Pity Shop
        ├── quest/                         # Nhận/Trả nhiệm vụ và cập nhật mục tiêu
        ├── party/                         # Tổ đội 5 người liên kênh, đồng bộ Channel Leader
        ├── guild/                         # Bang hội, phân quyền thành viên, Kho bang
        ├── social/                        # Bạn bè, Chat đa kênh qua Redis PubSub, Hòm thư (Mailbox)
        └── worldcoord/                    # Đăng ký Zone Server, Cân bằng tải Channel, Lease Fencing
```

---

## 2. Bốn Quy Tắc Kiến Trúc Của Modular Monolith (`Module Boundary Rules`)

AI Coding Agent khi viết code trong `internal/modules/` bắt buộc tuân thủ 4 luật sau (kiểm tra tự động bằng linter kiến trúc `depguard` / `go-arch-lint`):

1. **Sở hữu Bảng Độc quyền (`Exclusive Table Ownership`):**
   - Mỗi bảng trong PostgreSQL chỉ thuộc quyền ghi (`INSERT / UPDATE / DELETE`) của **duy nhất 1 module** (ví dụ: chỉ `economy` mới được ghi vào `character_wallets` và `wallet_ledger`; `loot` muốn cộng tiền thưởng Boss bắt buộc phải gọi qua `economy.WalletPort.CreditCurrency(ctx, tx, ...)`!).
2. **Giao tiếp Qua Cổng Giao diện (`Interface Ports Only`):**
   - Module `A` không bao giờ import gói triển khai nội bộ `internal/modules/B/repo` của module `B`, mà chỉ gọi qua interface công khai ở cấp `B.Service` (nhận chung `ctx` và tùy chọn `pgx.Tx` khi cần gộp giao dịch ACID trong cùng process `gamesvc`).
3. **Nạp Cấu hình Trò chơi Vào RAM Lúc Khởi động (`Immutable In-Memory GameData Registry`):**
   - Các bảng thông số không đổi trong lúc chạy (như công thức EXP, trọng số `Affix`, bảng rơi đồ `LootTable`, chi phí cường hóa `+1..+15`) được đọc từ thư mục `gamedata/*.json` vào cấu trúc bộ nhớ chỉ đọc lúc khởi động `gamesvc`, kết hợp đồng bộ bảng `item_definitions` trong DB để tra cứu trong `0.001 ms` không tốn truy vấn SQL dư thừa.
4. **Hỗ trợ Tắt Máy Êm Ái (`Graceful Shutdown`):**
   - Khi nhận tín hiệu `SIGTERM`, `gamesvc` ngừng nhận kết nối gRPC mới, xử lý dứt điểm các bản tin NATS JetStream đang dở dang trong tối đa `15 giây`, đóng sạch `pgxpool` rồi mới thoát tiến trình.

---

## 3. Mẫu Trình Bao Giao Dịch `WithSerializableOrLockedTx` Chuẩn Go

```go
// backend/pkg/dbutil/tx.go
package dbutil

import (
	"context"
	"fmt"
	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

// RunInTx đảm bảo mọi thao tác nhiều bảng (VD: Trừ Kora + Tăng cấp đồ + Ghi Ledger)
// hoặc thành công trọn vẹn 100%, hoặc Rollback sạch sẽ khi có bất kỳ lỗi nào.
func RunInTx(ctx context.Context, pool *pgxpool.Pool, fn func(tx pgx.Tx) error) error {
	tx, err := pool.BeginTx(ctx, pgx.TxOptions{IsoLevel: pgx.ReadCommitted})
	if err != nil {
		return fmt.Errorf("begin tx: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if err := fn(tx); err != nil {
		return err
	}
	if err := tx.Commit(ctx); err != nil {
		return fmt.Errorf("commit tx: %w", err)
	}
	return nil
}
```
