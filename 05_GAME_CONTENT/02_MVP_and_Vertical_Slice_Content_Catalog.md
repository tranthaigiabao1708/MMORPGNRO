# 02. MVP & Vertical Slice Concrete Content Catalog

**Phân hệ:** `05_GAME_CONTENT` · **Tài liệu:** `02_MVP_and_Vertical_Slice_Content_Catalog.md`  
**Mục đích:** Bộ thông số thiết kế cụ thể sẵn sàng nạp trực tiếp vào `backend/gamedata/*.json` và `UE5 DataTables`

---

## 1. Bảng Cấu Hình 6 Kỹ Năng Khởi Đầu Cho Vertical Slice (`gamedata/skills_vs.json`)

Tất cả nhân vật trong **Vertical Slice (`Level 1 – 15`)** có thể mở khóa và trang bị trọn bộ 6 kỹ năng chủ động + 1 trạng thái Hóa Cảnh sau:

| `skill_id` | Tên Kỹ Năng | Ô Gắn | Cấp Mở | `Chi` Cost | Cooldown | Loại Sát Thương | Thông Số & Hành Vi Thực Chiến |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| `sk_common_starbreaker_barrage` | **Starbreaker Barrage** | `Slot 1` | `Lv 1` | `18` | `5.0s` | `CHI` | Phóng 6 viên đạn khí tầm `28m`, mỗi viên `0.28x ChiAtk`, vừa bay vừa bắn được. |
| `sk_common_comet_fang` | **Comet Fang** | `Slot 2` | `Lv 3` | `25` | `8.0s` | `PHYSICAL` | Lao đột kích `22m` tới mục tiêu, trúng đích nối 3 đòn đấm tổng `2.10x PhysAtk` + `35 Stagger`. |
| `sk_valoran_solar_lance` | **Solar Lance** | `Slot 3` | `Lv 8` | `35` | `10.0s` | `CHI` | Tụ lực `0.45–1.2s`, phóng chưởng dài `45m` gây `6 ticks × 0.42x ChiAtk` (Tối đa `+35%` khi tụ đầy). |
| `sk_common_aegis_sphere` | **Aegis Sphere** | `Slot 4` | `Lv 10` | `30` | `14.0s` | `UTILITY` | Tạo cầu chắn năng lượng hấp thụ sát thương bằng `22% MaxHP` trong `2.5s`, đẩy lùi nhẹ kẻ áp sát. |
| `sk_common_astral_blink` | **Astral Blink** *(Thay thế linh hoạt Slot 4)* | `Slot 4` | `Lv 11` | `32` | `12.0s` | `UTILITY` | Dịch chuyển tức thời tới cạnh mục tiêu đang khóa (tầm `30m`) và tăng `+15% AttackSpeed` trong `3s`. |
| `sk_valoran_nova_collapse` | **Nova Collapse** | `Ultimate (R)` | `Lv 14` | `90` | `45.0s` | `HYBRID` | Triệu hồi cầu năng lượng ném xuống gây nổ bán kính `14m`, sát thương `4.80x (0.5 Phys + 0.5 Chi)` + `120 Stagger`. |

---

## 2. Danh Mục 12 Loại Quái Vật Chuẩn Của MVP (`Terra Prime Bestiary`)

*(Các mục đánh dấu **[VS]** được triển khai ngay trong Cragspire Highlands Vertical Slice)*

| `mob_def_id` | Tên Quái Vật | Zone Xuất Hiện | Hạng | Level | HP Cơ Bản | Đặc Điểm Chiến Đấu AI (`StateTree`) |
| :--- | :--- | :--- | :---: | :---: | :---: | :--- |
| `mob_crag_crawler` **[VS]** | **Crag Crawler** | `zone_cragspire` | Normal | `5 – 7` | `1,100` | Bò mặt đất theo bầy, lao húc ngắn; bao cát hoàn hảo để tân thủ tập nối `Uppercut Launch`. |
| `mob_crag_ravager` **[VS]** | **Crag Ravager** | `zone_cragspire` | Normal | `8 – 10` | `1,850` | Thú săn mồi 2 chân nhảy cao, biết đỡ đòn nhẹ (dạy người chơi dùng `RMB Guard Break`). |
| `mob_sky_harpy_drone` **[VS]** | **Galewing Drone** | `zone_cragspire` | Normal (Bay) | `9 – 12` | `1,600` | Bay lượn ở độ cao `+180m`, bắn tia laser đơn; buộc người chơi cất cánh không chiến. |
| `mob_apex_ravager` **[VS]** | **Apex Ravager** | `zone_cragspire` | **Elite** | `12` | `11,500` | Có thanh `Stagger (200)`, gầm sóng âm báo đỏ `1.0s` trước khi vồ diện rộng. |
| `mob_canopy_stalker` | **Thornback Stalker** | `zone_emerald_canopy` | Normal | `14 – 18` | `3,600` | Ẩn nấp trên cành đại thụ nhảy bổ xuống, tốc độ đánh nhanh. |
| `mob_crystal_wasp` | **Veridian Hornet** | `zone_emerald_canopy` | Normal (Bay) | `16 – 22` | `3,200` | Bay theo đàn 4 con, bắn kim tinh thể làm chậm tốc độ bay `15%`. |
| `mob_ancient_treant` | **Elder Barkwarden** | `zone_emerald_canopy` | **Elite** | `24` | `28,000` | Dựng khiên rễ cây phản đòn phía trước, phải bay vòng ra sau lưng để phá giáp. |
| `mob_badland_raider` | **Sandstorm Marauder** | `zone_sunken_badlands` | Normal | `24 – 30` | `6,400` | Đạo tặc đeo cánh đẩy cơ khí, vừa lùi vừa xả đạn liên thanh. |
| `mob_iron_juggernaut` | **Scrapyard Juggernaut** | `zone_sunken_badlands` | **Elite** | `34` | `58,000` | Phóng tên lửa khóa mục tiêu buộc người chơi dùng `Burst Dash` cắt đuôi đạn. |
| `mob_rift_hound` | **Voidscar Hound** | `zone_riftscar` | Normal | `35 – 42` | `11,000` | Dịch chuyển ngắn áp sát cắn rút `Chi`. |
| `mob_astral_wraith` | **Riftborn Wraith** | `zone_riftscar` | Normal (Bay) | `40 – 48` | `13,500` | Lơ lửng quanh cột sét dị thường, kháng `30%` sát thương Vật lý (ưu tiên dùng `Chi`). |
| `mob_gravity_archon` | **Anomaly Enforcer** | `zone_riftscar` | **Elite** | `50` | `135,000` | Tạo vùng tăng trọng lực bán kính `15m` kéo người đang bay thấp xuống mặt đất. |

---

## 3. Đặc Tả Chi Tiết Boss Vertical Slice: `Vanguard Colossus Kael`

- **`boss_def_id`:** `boss_cragspire_kael` · **Cấp độ:** `Lv 15` (Khuyến nghị tổ đội `3 – 5 người Lv 12–15`, có giãn máu nhẹ nếu ít người nhưng tối ưu cho 5–15 người vây đánh tại đấu trường `Titan's Altar`).
- **Thông số gốc (Chuẩn 5 người chơi):** `MaxHP = 240,000` · `MaxStaggerGauge = 1,000` · `PhysicalDef = 210` · `ChiDef = 210`.

### Kịch bản 3 Giai Đoạn (`StateTree Boss Encounter Script`)

| Giai đoạn | Ngưỡng Máu | Hành Vi & Chuỗi Chiêu Thức Đặc Trưng | Cách Khắc Chế Của Người Chơi (`Counterplay`) |
| :--- | :---: | :--- | :--- |
| **Phase 1: Ground Bastion** | `100% → 65%` | **1. Seismic Cleave:** Quét búa năng lượng hình quạt `120°` tầm `10m` phía trước (`0.8s` báo đỏ).<br>**2. Meteoric Leap:** Nhảy dậm xuống người chơi đứng xa nhất gây sóng chấn động vòng tròn lan rộng trên sàn đá. | Lướt `Burst Dash` ra sau lưng khi quét búa; nhảy cất cánh `Hover` cách mặt đất `3m` để né hoàn toàn sóng chấn động sàn! |
| **Phase 2: Skyward Ascension** | `65% → 30%` | Kael bùng hào quang vàng đỏ bay vọt lên tâm đấu trường ở độ cao **`+80m`**, bật khiên giảm `80%` sát thương và triệu hồi **3 Tinh Thể Dẫn Truyền (`Solar Pylons`)** bay quanh ở độ cao `+60m` đến `+95m` trong `25 giây`. | Cả tổ đội **bắt buộc bay lên không trung**, chia nhau phá vỡ 3 `Solar Pylons` trước khi hết `25s`. Phá đủ 3 trụ khiến Kael nổ khiên, rơi xuống choáng váng `6.0 giây` nhận `+35%` sát thương! Nếu thất bại, Kael nổ `Solar Cataclysm` gây sát thương cực lớn toàn sân. |
| **Phase 3: Overdrive Fury** | `30% → 0%` | Kael chiến đấu liên hoàn cả trên không lẫn mặt đất: phóng chưởng quét ngang **`Sweeping Solar Beam` (`180°`)** và lao húc 3 lần liên tiếp vào mục tiêu giữ `Aggro` cao nhất. | Bay cao/hạ thấp vượt qua mặt phẳng quét tia chưởng, canh `Perfect Guard` hoặc `Aegis Sphere` đỡ cú húc cuối rồi xả đồng loạt `Ascension + Nova Collapse` kết liễu. |

---

## 4. Danh Mục 15 Trang Bị Mẫu & Bảng Đổi Bảo Hiểm (`gamedata/items_vs.json`)

| `item_def_id` | Tên Trang Bị | Ô Mặc | Phẩm Cấp | `iLv` | `set_id` | Chỉ Số Gốc Chính & Nguồn Nhận |
| :--- | :--- | :---: | :---: | :---: | :--- | :--- |
| `eq_starter_visor_t1` | **Initiate Resonance Visor** | `HEAD` | `COMMON` | `1` | — | `+15 MaxChi` (Trang bị khởi đầu). |
| `eq_starter_tunic_t1` | **Initiate Martial Tunic** | `BODY` | `COMMON` | `1` | — | `+80 MaxHP, +12 Def` (Trang bị khởi đầu). |
| `eq_crag_bracers_u1` | **Ravager Bone Bracers** | `HANDS` | `UNCOMMON` | `8` | — | `+14 PhysAtk, +10 ChiAtk` (Rơi từ *Crag Ravager*). |
| `eq_crag_boots_u1` | **Windstep Boots** | `FEET` | `UNCOMMON` | `9` | — | `+14 Def, +4% FlightSpeed` (Rơi từ *Galewing Drone*). |
| `eq_apex_ring_r1` | **Apex Fang Ring** | `ACC_1` | `RARE` | `12` | — | `+24 PhysAtk, +24 ChiAtk` (Rơi từ *Apex Ravager*). |
| `eq_apex_pendant_r1` | **Highland Gale Pendant** | `ACC_2` | `RARE` | `12` | — | `+1.2 ChiRecovery/s, +4% CDR` (Rơi từ *Apex Ravager*). |
| `eq_vanguard_visor_e1` | **Cragspire Vanguard Visor** | `HEAD` | `EPIC` | `15` | `set_crag_vanguard` | `+45 MaxChi, +3.5% CritRate` (Boss *Kael* `25%` / Đổi `12 Core Tokens`). |
| `eq_vanguard_chest_e1` | **Cragspire Vanguard Cuirass** | `BODY` | `EPIC` | `15` | `set_crag_vanguard` | `+280 MaxHP, +48 Def` (Boss *Kael* / Đổi `12 Core Tokens`). |
| `eq_vanguard_legs_e1` | **Cragspire Vanguard Greaves** | `LEGS` | `EPIC` | `15` | `set_crag_vanguard` | `+210 MaxHP, +36 Def` (Boss *Kael* / Đổi `12 Core Tokens`). |
| `eq_vanguard_hands_e1` | **Cragspire Vanguard Gauntlets** | `HANDS` | `EPIC` | `15` | `set_crag_vanguard` | `+38 PhysAtk, +38 ChiAtk` (Boss *Kael* / Đổi `12 Core Tokens`). |
| `eq_vanguard_feet_e1` | **Cragspire Vanguard Sabatons** | `FEET` | `EPIC` | `15` | `set_crag_vanguard` | `+30 Def, +7% FlightSpeed` (Boss *Kael* / Đổi `12 Core Tokens`). |
| `eq_vanguard_ring_e1` | **Cragspire Vanguard Signet** | `ACC_1` | `EPIC` | `15` | `set_crag_vanguard` | `+42 PhysAtk, +42 ChiAtk` (Boss *Kael* / Đổi `12 Core Tokens`). |
| `eq_vanguard_core_e1` | **Cragspire Resonance Core** | `SPECIAL` | `EPIC` | `15` | `set_crag_vanguard` | `+12 AscensionEfficiency` (Boss *Kael* / Đổi `15 Core Tokens`). |
| `eq_kael_solar_gauntlets_l1` | **Kael's Solar Crusher Gauntlets** | `HANDS` | `LEGENDARY` | `15` | — | `+52 PhysAtk, +52 ChiAtk` + **Nội tại Riêng:** *Đòn `Solar Lance` bắn thêm 2 luồng xoắn phụ gây thêm `25%` sát thương* (Boss *Kael* `2%` / Đổi `40 Core Tokens`). |
| `eq_kael_relic_mantle_l1` | **Heart of the Colossus** | `SPECIAL` | `LEGENDARY` | `15` | — | `+180 MaxHP, +15 AscensionEfficiency` + **Nội tại Riêng:** *Khi bật `Ascension`, nhận lớp khiên bằng `20% MaxHP` trong `8s`* (Boss *Kael* `2%` / Đổi `40 Core Tokens`). |
