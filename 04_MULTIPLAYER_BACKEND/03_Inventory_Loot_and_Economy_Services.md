# 03. Authoritative Inventory, Loot RNG & ACID Economy Services

**Phân hệ:** `04_MULTIPLAYER_BACKEND` · **Tài liệu:** `03_Inventory_Loot_and_Economy_Services.md`  
**Modules đích:** `internal/modules/loot/`, `internal/modules/inventory/`, `internal/modules/economy/`

---

## 1. Đường Ống Quay Thưởng `Personal Loot` & Bảo Hiểm Boss (`Authoritative Loot Pipeline`)

Khi worker của module `loot` nhận bản tin `combat.boss.defeated.v1` từ NATS JetStream:

### 1.1. Quy Trình 6 Bước Cho Từng Người Chơi Đủ Điều Kiện
1. **Kiểm tra Đủ Điều Kiện (`Eligibility Check`):** Bỏ qua người chơi có `present_at_death == false` hoặc `contribution_score < 0.015`. Kiểm tra quy tắc chống kéo cấp (`Anti-Carry`): nếu chênh lệch cấp trong tổ đội vi phạm ngưỡng cho phép, hạ vật phẩm nguyên liệu về trạng thái `BOUND`.
2. **Khởi tạo Bộ Sinh Số Ngẫu Nhiên Có Khả Năng Kiểm Toán (`Seeded Audit RNG`):**
   - Sinh hạt giống `64-bit` từ `crypto/rand` rồi lưu `rng_audit_seed` vào bảng `loot_history` cùng với `event_idempotency`. Nhờ vậy tỷ lệ quay hoàn toàn không thể bị người chơi đoán trước, nhưng đội ngũ kỹ thuật vẫn có thể tái kiểm tra khi cần.
3. **Cộng Chắc Chắn `Boss Pity Token` & Đếm Chuỗi Đen (`Soft/Hard Pity`):**
   - Tăng `boss_pity_progress.token_balance += GuaranteedTokens` (ví dụ `+2` cho *Colossus Kael*) và `kills_since_epic += 1`.
   - Nếu `kills_since_epic >= 8` mà chưa tự quay trúng mảnh `EPIC`, nâng tỉ lệ rơi `EPIC` thêm `+10%` mỗi lần hạ tiếp theo; chạm lần thứ `12` chắc chắn rơi `100%` rồi đặt lại `kills_since_epic = 0`!
4. **Quay Bảng Vật Phẩm & Sinh Dòng Phụ (`Affix Generator`):**
   - Chọn phẩm cấp món đồ theo trọng số bảng rơi của Boss và bậc đóng góp (`Bronze / Silver / Gold / Vanguard`).
   - Dựa vào `rarity`, rút ngẫu nhiên không hoàn lại (`Weighted Reservoir Sampling without Replacement`) đúng số lượng dòng phụ từ các bể `Offensive / Defensive / Utility / Special`, sau đó quay bậc `Tier` và giá trị thực trong dải `[MinVal, MaxVal]` của bậc đó.
5. **Ghi Vào Túi Đồ Hoặc Hòm Thư Tràn (`Bag or Mail Escrow`):**
   - Trong một `pgx.Tx` duy nhất: kiểm tra số ô trống trong túi đồ (`MaxBagSlots = 48` mặc định). Nếu còn ô trống thì chèn vào `container_type = 'BAG'`; nếu đầy túi thì tự động chèn vào `container_type = 'MAIL_ESCROW'` và tạo thư đính kèm để **người chơi không bao giờ mất đồ hiếm vì quên dọn túi**!
6. **Thông báo Thời gian thực:** Đẩy gói JSON `loot.granted` qua Redis PubSub tới `gateway` để bật hiệu ứng nhận thưởng trên màn hình Client.

---

## 2. Giao Dịch Cường Hóa & Tinh Luyện An Toàn (`Enhance & Reforge Transactions`)

```go
// Luồng giả mã chuẩn trong internal/modules/economy/enhance_service.go
func (s *EnhanceService) EnhanceEquipment(ctx context.Context, req EnhanceRequest) (*EnhanceResult, error) {
    // 1. Kiểm tra Idempotency-Key trước để chống gửi đúp gói tin
    if cached, ok := s.idemStore.Get(ctx, req.IdempotencyKey); ok {
        return cached, nil
    }

    var res *EnhanceResult
    err := dbutil.RunInTx(ctx, s.pool, func(tx pgx.Tx) error {
        // 2. Khóa dòng trang bị (SELECT ... FOR UPDATE) & kiểm tra Optimistic Version
        item, err := s.invRepo.LockItemForUpdate(ctx, tx, req.ItemInstanceID, req.CharacterID)
        if err != nil { return err }
        if item.Version != req.ExpectedVersion { return ErrConcurrentModification }
        if item.EnhanceLevel >= 15 { return ErrMaxEnhanceReached }

        cfg := s.gameData.EnhanceTable[item.EnhanceLevel+1]

        // 3. Trừ Kora (kèm ghi vào wallet_ledger) và trừ đá Astral Shards trong túi
        if err := s.walletRepo.DebitKoraWithLedger(ctx, tx, req.CharacterID, cfg.KoraCost,
            "ENHANCE_SINK", req.ItemInstanceID.String(), req.IdempotencyKey); err != nil {
            return err
        }
        if err := s.invRepo.ConsumeMaterial(ctx, tx, req.CharacterID, cfg.ShardDefID, cfg.ShardCount); err != nil {
            return err
        }

        // 4. Nếu món đồ đang ở trạng thái BIND_ON_EQUIP, cường hóa sẽ tự động chuyển sang BOUND
        newBind := item.BindState
        if newBind == "BIND_ON_EQUIP" { newBind = "BOUND" }

        // 5. Kiểm tra Bảo hiểm Artisan's Energy (10000 bp = 100%) hoặc quay CSPRNG
        succeeded := item.ArtisanPityBP >= 10000 || cryptoRollBasisPoints() < cfg.SuccessRateBP
        newLevel := item.EnhanceLevel
        newPity  := item.ArtisanPityBP

        if succeeded {
            newLevel++
            newPity = 0
        } else {
            // Không bao giờ vỡ đồ! Tích lũy điểm bảo hiểm rõ ràng
            newPity = min(10000, item.ArtisanPityBP + cfg.PityGainOnFailBP)
        }

        // 6. Cập nhật trang bị và tăng version + 1
        return s.invRepo.UpdateItemEnhancement(ctx, tx, item.ID, newLevel, newPity, newBind, item.Version+1)
    })
    if err != nil { return nil, err }

    // 7. Nếu món đồ đang mặc trên người, phát sự kiện tính lại chỉ số sang Dedicated Server!
    s.charNotifier.NotifyEquipmentChanged(ctx, req.CharacterID)
    return res, nil
}
```

---

## 3. Giao Dịch Trực Tiếp Hai Người Chơi Chống Deadlock (`Atomic P2P Trade`)

Mọi phiên giao dịch trực tiếp (`Direct Trade`) đều trải qua máy trạng thái tạm trên Redis (`DRAFT` → `BOTH_LOCKED` chờ đủ `3.0 giây` → `BOTH_CONFIRMED`), sau đó mới thực thi duy nhất **1 giao dịch SQL nguyên tử**:

1. **Sắp xếp Khóa Chống Deadlock:**
   - So sánh chuỗi UUID của `InitiatorCharID` và `RecipientCharID`. Luôn gọi `SELECT ... FOR UPDATE` trên bảng `character_wallets` theo thứ tự UUID nhỏ trước, UUID lớn sau!
2. **Xác minh Ràng buộc Chống Gian lận:**
   - Mọi vật phẩm đưa lên bàn giao dịch đều phải đang thuộc sở hữu của đúng người gửi, nằm ở `container_type = 'BAG'`, có `bind_state IN ('UNBOUND', 'BIND_ON_EQUIP')` (tuyệt đối từ chối đồ `BOUND`!) và khớp số `version`.
   - Cả hai bên còn đủ số ô túi trống để nhận số món đồ của đối phương.
3. **Khấu trừ Thuế Chống Lạm phát (`8% Trade Tax`):**
   - Nếu có chuyển `Kora`, bên nhận thực lãnh `92%` số `Kora`, còn `8%` bị đốt vĩnh viễn khỏi lưu thông và ghi rõ vào cột `tax_kora_burned` trên `trade_transactions` cùng 2 dòng tương ứng trong `wallet_ledger`.
4. **Hoán đổi Chủ sở hữu Vật phẩm (`Ownership Swap`):**
   - Cập nhật trực tiếp `character_id` và `slot_index` mới trên bảng `inventory_items` (giữ nguyên `id UUID` gốc của món đồ để chuỗi lịch sử truy vết nguồn gốc món đồ từ lúc rơi ra khỏi Boss vẫn liền mạch 100%!).
