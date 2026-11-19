# 06. Multiplayer & Social Systems

**Phân hệ:** `01_GDD` · **Tài liệu:** `06_Multiplayer_and_Social_Systems.md`  
**Trạng thái:** Baseline khóa cho MVP & Vertical Slice

---

## 1. Kiến Trúc Ba Tầng Không Gian Chơi Chung (`Multiplayer World Layers`)

Để cân bằng giữa **cảm giác thế giới mở đông đúc sống động** và **chất lượng chiến đấu chính xác không giật lag**, hoạt động nhiều người chơi được chia làm 3 tầng không gian bổ trợ lẫn nhau:

| Tầng Hoạt Động | Quy Mô Người Chơi | Cơ Chế Vận Hành | Các Hoạt Động Diễn Ra |
| :--- | :---: | :--- | :--- |
| **1. Persistent Open Zone** | `80 Soft / 100 Hard CCU` mỗi Channel | Luôn tồn tại 24/7; người chơi tự do gặp gỡ, thấy nhau bay lượn, đánh quái, giúp đỡ nhau tại chỗ. | Khám phá, nhiệm vụ, farm bãi quái, săn *Elite*, giao thương tại *Neo Haven*, tụ họp săn *World Boss*. |
| **2. Party Instance** | `1 – 5 Người chơi` | Tạo riêng theo yêu cầu khi đội trưởng mở cổng khiêu chiến; giải phóng ngay khi kết thúc. | *Dungeon*, *Hardcore Boss Rematch* (thử thách lại Boss ở độ khó cao không sợ người ngoài can thiệp), *Co-op Gravity Trials*. |
| **3. Competitive / Raid Event Instance** | `2 – 40 Người chơi` | Ghép trận liên kênh (`Matchmaking`) qua `World Coordinator` theo khung giờ hoặc hàng đợi. | *Balanced Arena PvP (1v1, 3v3)*, *Guild Boss*, *Guild War* và *Endgame Raid (10–20 người)*. |

---

## 2. Hệ Thống Tổ Đội 5 Người (`Party System` — Có ngay trong Vertical Slice)

- **Quy mô chuẩn:** Tối đa **5 thành viên** mỗi `Party`.
- **Cơ chế Cộng hưởng Kinh nghiệm (`Party Synergy EXP Bonus`):** Để khuyến khích lập nhóm thay vì mạnh ai nấy đánh lẻ, khi các thành viên đứng trong bán kính **`120m`** quanh nhau (phù hợp với cự ly bay 3D):
  - Tổng `EXP` cơ bản của quái được nhân hệ số thưởng nhóm rồi chia đều cho các thành viên đủ điều kiện cấp độ:
    - `2 người`: Tổng `130%` (`65%`/người — tốc độ diệt quái tăng gấp đôi nên thực nhận nhanh hơn đánh lẻ `~30%`!).
    - `3 người`: Tổng `165%` (`55%`/người).
    - `4 người`: Tổng `200%` (`50%`/người).
    - `5 người`: Tổng `240%` (`48%`/người + cộng thêm `+5%` tỉ lệ rơi nguyên liệu `Astral Shards`).
- **Tiện ích Tổ đội:**
  - **Shared Quest Credit:** Hạ quái mục tiêu tính tiến độ chung cho mọi thành viên trong vùng `120m`.
  - **3D Tactical Ping (`Alt + LMB`):** Đánh dấu cột sáng chiến thuật 3D ngoài không gian (Tập trung hỏa lực / Nguy hiểm rút lui / Mạch tài nguyên).
  - **Leader Resonance Sync:** Nút một chạm trên giao diện `Party UI` (chỉ dùng được khi ngoài giao tranh) giúp thành viên ở khác Channel hoặc ở xa bay dịch chuyển về Trạm Tiền Tiêu gần Đội trưởng nhất và đồng bộ chung Channel.

---

## 3. Hệ Thống Bang Hội (`Guild System` — Mở rộng đầy đủ ở Phase 4 MVP)

Bang hội là mái nhà dài hạn giữ chân cộng đồng:
- **Quy mô thành viên:** Khởi điểm `30 thành viên` (Guild Lv 1), nâng tối đa `80 thành viên` (Guild Lv 10).
- **Tiến trình phát triển Bang hội:**
  - Thành viên hoàn thành *Guild Daily/Weekly Commissions* (săn quái tinh anh, đóng góp tinh thạch, đi Boss cùng thành viên bang) để tăng `Guild EXP` và nhận `Guild Merits`.
  - Mở khóa **Guild Passive Buffs** vừa phải (ví dụ: `+5% EXP quái`, `-5% phí sửa đồ Kora`, `+5 FlightEfficiency` — không cộng chỉ số đè bẹp người chơi tự do trong Arena PvP).
- **Kho Bang Hội (`Guild Vault`):** Phân quyền minh bạch theo chức vụ (`Guild Master`, `Vice Master`, `Vanguard Officer`, `Member`, `Initiate`) kèm nhật ký giao dịch không thể xóa.
- **Hoạt động gắn kết:** Triệu hồi *Guild Boss* hàng tuần trong *Event Instance* riêng của bang và lộ trình mở khóa **Căn Cứ Đảo Bay Bang Hội (`Skyhold Guild Base`)** ở phiên bản `v1.0`.

---

## 4. Thiết Kế Sự Kiện `World Boss` Đa Giai Đoạn (`World Boss Architecture`)

*World Boss* là sự kiện đỉnh cao biến một thế giới mở thành trải nghiệm đáng nhớ. Tuy nhiên, nếu 150 người cùng dồn vào một điểm bắn chưởng, cả FPS Client lẫn CPU Server đều sẽ sụp đổ. Giải pháp thiết kế như sau:

### 4.1. Vòng Đời Một Đợt Xuất Hiện `World Boss`
1. **Cảnh báo Dị thường (`T - 10 phút`):** Bầu trời khu vực (ví dụ *Riftscar Wasteland* hoặc *Titan's Altar*) đổi sang sắc tím rực tinh vân; thông báo toàn máy chủ phát lên kèm đồng hồ đếm ngược trên bản đồ.
2. **Phân luồng Combat Zone (`Dynamic Encounter Sharding`):**
   - Mỗi phiên đấu trường *World Boss* khóa trần mô phỏng chiến đấu trực tiếp ở mức **40 người chơi (`Hard Combat Cap = 40`)** để đảm bảo Server giữ vững `30 Hz Tick` và Client giữ `>= 60 FPS`.
   - Khi người chơi thứ 41 bay vào vành đai đấu trường, `World Coordinator` tự động chuyển mượt họ sang phiên đấu trường song song (`Encounter Instance/Channel #2`) nơi một bản thể tương đương của *World Boss* đang xuất hiện, ưu tiên giữ nguyên các thành viên cùng `Party` ở chung một phiên!
3. **Ba Giai Đoạn Chiến Đấu (`3-Phase Boss Script`):**
   - **Phase 1 (`100% → 65% HP` — Ground & Mid-Range):** Đòn quét diện rộng mặt đất và phóng tia năng lượng thẳng; dạy người chơi nhịp né cơ bản.
   - **Phase 2 (`65% → 30% HP` — Aerial Ascension):** Boss kích hoạt hào quang bay vọt lên độ cao `+120m` giữa đấu trường, triệu hồi các tinh thể hộ vệ lơ lửng cần cả nhóm bay lên phá hủy trong `25 giây` trước khi Boss tung chiêu hủy diệt diện rộng (`Wipe Mechanic`).
   - **Phase 3 (`30% → 0% HP` — Overdrive Frenzy):** Tốc độ ra chiêu tăng `25%`, mặt đất nứt toác phun cột năng lượng buộc người chơi duy trì không chiến liên tục cho tới đòn kết liễu.

### 4.2. Công Thức Tính Điểm Đóng Góp & Chia Thưởng (`Contribution Formula`)

Để người chơi thiên về chống chịu, phá thanh `Stagger` hoặc hỗ trợ tổ đội không bị thiệt thòi so với người thuần dồn sát thương:

$$
\text{ContributionScore}_i = 0.65 \times \frac{\text{DmgDealt}_i}{\text{BossMaxHP}} + 0.20 \times \frac{\text{StaggerDealt}_i}{\text{TotalStagger}} + 0.15 \times \frac{\text{MechanicObjectives}_i}{\text{MaxObjectives}}
$$

- **Ngưỡng nhận thưởng tối thiểu (`Eligibility Floor`):** Đạt $\text{ContributionScore}_i \ge 0.015$ (`1.5%` quy chuẩn) VÀ còn hiện diện trong khu vực khi Boss ngã xuống.
- **Bậc phần thưởng (`Bronze / Silver / Gold / Vanguard Tier`):** Ai đạt ngưỡng tối thiểu cũng nhận chắc chắn `Core/Sovereign Pity Tokens` + `Kora`; đạt bậc `Gold/Vanguard` được cộng thêm lượt quay phụ trên bảng rơi trang bị *Legendary/Mythic* (nhưng chênh lệch không khóa độc quyền về tay 1 bang hội duy nhất).

---

## 5. Hệ Thống Tương Tác Xã Hội, Giao Dịch & Quy Tắc `PvP`

### 5.1. Bộ Công Cụ Giao Tiếp & Thương Mại
- **Chat đa kênh thời gian thực (WebSocket qua `SocialService`):** `Local Spatial` (bán kính 60m kèm bong bóng thoại trên đầu nhân vật), `Zone`, `Party`, `Guild`, `Whisper`, `Trade` và `System Combat Log` (có bộ lọc từ ngữ xấu và giới hạn chống spam mỗi kênh).
- **Direct Player Trading:** Giao dịch trực tiếp 2 bước xác nhận (`Lock Offer` → cả hai bên kiểm tra lại trong `3 giây` khóa sửa đổi → `Confirm Trade`) để chống trò lừa đảo tráo đồ phút chót.
- **Mailbox:** Nhận lại vật phẩm rơi từ Boss mà lúc đó túi đồ bị đầy (`Overflow Auto-Mail` giữ trong 14 ngày), thông báo bán hàng thành công và quà sự kiện.

### 5.2. Ba Chế Độ `PvP` Có Kiểm Soát (Bảo vệ trải nghiệm Tân thủ)

1. **Friendly Duel (Tỷ Thí Giao Hữu — Mọi vùng ngoài khu cấm đô thị):** Gửi lời mời tỷ thí 1v1, cắm cờ giới hạn bán kính `50m`, khi máu chạm `1 HP` tự dừng trận và hồi lại trạng thái ban đầu — không mất đồ, không tốn độ bền.
2. **Balanced Arena PvP (`1v1` & `3v3` Xếp hạng):** Ghép trận theo điểm `MMR`, áp dụng công thức nén chỉ số công bằng (xem tài liệu `03`), thưởng điểm danh vọng đổi ngoại trang chiến thần, hiệu ứng hào quang mùa giải và danh hiệu.
3. **Contested Anomaly Zones & Guild War:** Chỉ một số phân khu đặc biệt được đánh dấu viền đỏ rõ ràng tại vùng cấp cao (như trung tâm vết nứt *Riftscar Wasteland* vào khung giờ sự kiện khai thác mỏ tinh thạch lớn) mới bật chế độ đối kháng mở. Người chơi bước vào nhận cảnh báo đếm ngược `5 giây` rõ ràng — **tuyệt đối không có chuyện người mới đang làm nhiệm vụ cốt truyện ở *Cragspire* hay đứng ở thành phố bị người cấp cao đồ sát**.
