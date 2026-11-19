# 03. GAS Combat & Ascension Architecture

**Phân hệ:** `03_UNREAL_GAME_CLIENT` · **Tài liệu:** `03_GAS_Combat_and_Ascension_Architecture.md`  
**Module đích:** `Source/ZIUCombatGAS/`

---

## 1. Kiến Trúc Ba Tập Thuộc Tính (`Modular AttributeSets`)

Để tránh việc mỗi lần hồi `1 điểm Chi` lại kích hoạt kiểm tra mạng trên toàn bộ 20 chỉ số không liên quan, hệ thống tách `UAttributeSet` thành **3 lớp chuyên biệt** gắn trên `UZIUAbilitySystemComponent`:

### 1.1. `UZIUVitalAttributeSet` (Tần suất thay đổi cao trong giao tranh)
- `Health`, `MaxHealth` *(Replicated tới mọi người chơi trong vùng nhìn thấy để vẽ thanh máu)*.
- `Chi`, `MaxChi` *(Replicated `COND_OwnerOnly` hoặc `PartyOnly` để tiết kiệm băng thông!)*.
- `Stamina`, `MaxStamina` *(Replicated `COND_OwnerOnly`)*.
- `StaggerGauge`, `MaxStaggerGauge` *(Dùng cho Boss & Elite Monster — Replicated tới mọi người)*.
- **Meta Attributes (Chỉ tồn tại trên Server trong 1 tick tính toán, KHÔNG Replicate):** `IncomingDamage`, `IncomingStagger`, `IncomingHealing`.

### 1.2. `UZIUCombatAttributeSet` (Chỉ đổi khi Mặc đồ, Lên cấp hoặc Bật Buff/Ascension)
- `PhysicalAttack`, `ChiAttack`, `Defense`, `AllResistance`.
- `CritRate`, `CritDamage`, `GuardPenetration`.
- `AttackSpeedMultiplier`, `MoveSpeedMultiplier`, `ChiRecoveryRate`, `StaminaRecoveryRate`, `CooldownReduction`.

### 1.3. `UZIUPotentialAttributeSet` (Chỉ đổi khi Hoàn thành Khảo hạch — `COND_OwnerOnly`)
- `AscensionEfficiency`, `ChiControl`, `EnergyCapacity`, `FlightEfficiency`, `AstralResistance`.

---

## 2. Cây Phân Cấp `GameplayTags` Chuẩn Toàn Dự Án (`DefaultGameplayTags.ini`)

AI Coding Agent bắt buộc đăng ký các thẻ trong `ZIU_GameplayTags.h` (Native Gameplay Tags) theo đúng sơ đồ tên sau:

```text
State
├── Movement
│   ├── Grounded
│   ├── Hovering
│   ├── Flying.Cruise
│   ├── Flying.Boost
│   └── Flying.Pursuit
├── Combat
│   ├── InCombat                     # Tự động gia hạn 8s mỗi khi gây/nhận sát thương
│   ├── Guarding                     # Đang giữ phím V
│   ├── PerfectGuardWindow           # 0.15s đầu khi nhấn V
│   ├── Invulnerable.Dash            # 0.15s i-frame của Burst Dash
│   ├── SuperArmor                   # Không bị khựng đòn nhỏ
│   └── Window
│       ├── ComboCancel              # Cho phép nối đòn kế tiếp
│       ├── DodgeCancel              # Cho phép hủy hoạt ảnh sang lướt né
│       └── CanAerialPursuit         # Mở 0.6s sau khi đánh trúng Uppercut Launch
├── Ascension
│   ├── Active                       # Thẻ cha kiểm tra chung đang ở trạng thái biến hình
│   ├── Valoran.SolarSurge
│   ├── Valoran.SolarSovereign
│   ├── Terran.Overlimit
│   └── Sylvanar.VerdantAegis
└── Debuff
    ├── Hitstun.Light
    ├── Hitstun.Airborne
    ├── Knockdown
    ├── Stunned
    └── ChiExhausted                 # Khóa Boost trong 2.5s khi cạn sạch Chi

Ability
├── Melee.LightCombo
├── Melee.HeavyCharge
├── Melee.UppercutLaunch
├── Melee.AerialPursuit
├── Skill.ChiVolley
├── Skill.SolarLance
├── Skill.AstralNova
├── Skill.CometFang
├── Skill.AstralBlink
├── Ultimate.NovaCollapse
└── Ascension.Toggle

DamageType
├── Physical
├── Chi
└── TrueAstral

GameplayCue
├── Combat.Hit.Light
├── Combat.Hit.Heavy
├── Combat.Beam.SolarLance
└── Ascension.Aura.SolarSurge
```

---

## 3. Đặc Tả Bốn Lớp `UGameplayAbility` Cốt Lõi (Dùng Ngay Cho Vertical Slice)

### 3.1. `UGA_ZIUMeleeCombo` (Chuỗi Đánh Cận Chiến Mặt Đất & Trên Không)
- **Net Execution Policy:** `LocalPredicted`.
- **Cơ chế chọn nhánh tự động:** Khi kích hoạt, kiểm tra `ASC->HasMatchingGameplayTag(State.Movement.Grounded)` để chọn `GroundComboMontageArray` hay `AirComboMontageArray`.
- **Xử lý nối đòn không trễ:** Sử dụng `UAbilityTask_PlayMontageAndWait` kết hợp `UAbilityTask_WaitGameplayEvent`:
  - Nếu người chơi bấm `LMB` trong lúc thẻ `State.Combat.Window.ComboCancel` đang bật, ghi nhận cờ `bQueuedNextComboSection = true` và chuyển mượt sang Section kế tiếp ngay tại ranh giới cửa sổ.
  - **Kiểm tra va chạm chuẩn Server (`Server-Only Hit Sweep`):** Trong cửa sổ `UAnimNotifyState_ZIUServerHitbox`, chỉ Dedicated Server (hoặc Listen Host) thực hiện `CapsuleTraceMulti` từ vị trí khung xương nắm đấm/bàn chân frame trước tới frame hiện tại (kèm tập `HitActorsThisSwing` để một cú đấm không gây sát thương 2 lần lên cùng 1 mục tiêu!).

### 3.2. `UGA_ZIUAerialPursuit` (Bay Truy Kích Sau Hất Tung)
- **Activation Required Tags:** `State.Combat.Window.CanAerialPursuit`.
- **Luồng thực thi:**
  1. Đọc mục tiêu vừa trúng đòn hất tung từ `UZIUTargetingComponent::GetLastLaunchedTarget()`.
  2. Gọi `UZIUFlightMovementComponent::StartAerialPursuit(Target, 0.50f)` đồng bộ trên cả Client dự đoán và Server.
  3. Kích hoạt `UMotionWarpingComponent::AddOrUpdateWarpTargetFromLocationAndRotation` ở `15cm` cuối hành trình để khớp vị trí hoàn hảo trước mặt kẻ địch trên không, mở ngay cửa sổ sang `Air LMB 1`.

### 3.3. `UGA_ZIUChanneledBeam` (Mẫu Kỹ Năng Chưởng Lực — `Solar Lance`)
- **Luồng 2 pha (`Charge Phase` → `Fire Phase`):**
  1. **Pha Tụ Lực (`0.45s` tối thiểu, giữ phím tối đa `1.2s`):** Nhân vật chuyển về `CMOVE_Hover` nếu đang bay, phát `GameplayCue.Combat.Beam.SolarLance` giai đoạn tụ sáng tại bàn tay. Mỗi `0.25s` giữ thêm tăng hệ số khuếch đại chưởng thêm `+10%` (tối đa `+35%`).
  2. **Pha Phóng Tia (`0.90s`, chia `6 ticks` sát thương mỗi `0.15s`):**
     - Trên Server, mỗi `0.15s` thực hiện phép quét hình trụ (`SweepMultiByChannel` bán kính `90cm`, chiều dài `4,500cm`) có áp dụng bù trễ `Bounded Lag Compensation <= 150ms` (xem tài liệu `02_Network_Protocol`).
     - Cho phép người chơi xoay góc hướng tia chậm với tốc độ góc giới hạn `15 độ/giây` để rượt theo mục tiêu đang bay ngang.

### 3.4. `UGA_ZIUAscensionToggle` (Cơ Chế Biến Hình & Duy Trì Hào Quang)
- **Activation Check:** Kiểm tra `Chi >= MinActivationChi (50)` và chưa bật thẻ `State.Ascension.Active`.
- **Khi bật Hóa Cảnh:**
  1. Chạy Montage bùng khí `0.65s` (cấp `State.Combat.SuperArmor` trong lúc biến hình và đẩy lùi nhẹ quái nhỏ trong bán kính `300cm`).
  2. Áp dụng **Infinite GameplayEffect `UGE_Ascension_Buff`** (cộng `% PhysicalAttack`, `% ChiAttack`, `% MoveSpeedMultiplier` và gắn thẻ `State.Ascension.Valoran.SolarSurge`).
  3. Áp dụng **Periodic GameplayEffect `UGE_Ascension_ChiDrain`** (chu kỳ `0.5s`, trừ lượng `Chi = 0.5 * BaseDrainPerSec * (1 - 0.45 * AscensionEfficiency / 100)`).
  4. Lắng nghe delegate `OnChiAttributeChanged`: ngay khi `NewChi <= 0.f` (hoặc khi người chơi giữ phím `C` `1.2s` để tắt chủ động), tự động gỡ `UGE_Ascension_Buff` & `UGE_Ascension_ChiDrain` và thu hồi `GameplayCue` hào quang!

---

## 4. Khai Báo Lớp Tính Toán Sát Thương Server (`UZIUDamageExecCalc`)

Toàn bộ công thức toán học từ `01_GDD/03_Action_Combat_System.md` được đóng gói duy nhất tại một lớp C++ này:

```cpp
// Source/ZIUCombatGAS/Public/Executions/ZIUDamageExecCalc.h
UCLASS()
class ZIUCOMBATGAS_API UZIUDamageExecCalc : public UGameplayEffectExecutionCalculation
{
    GENERATED_BODY()
public:
    UZIUDamageExecCalc();

    virtual void Execute_Implementation(
        const FGameplayEffectCustomExecutionParameters& ExecutionParams,
        FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
```

**Thứ tự xử lý bên trong `Execute_Implementation` (Chỉ chạy trên Server):**
1. Đọc `SetByCaller` magnitudes từ `FGameplayEffectSpec`: `Data.SkillMultiplier` ($K_{\text{skill}}$), `Data.FlatBonus`, `Data.StaggerPower` và thẻ `DamageType.*`.
2. Kiểm tra trạng thái phòng thủ của `TargetASC`:
   - Nếu có `State.Combat.Invulnerable.Dash` → trả về `0` sát thương, phát sự kiện `Evaded`.
   - Nếu có `State.Combat.PerfectGuardWindow` → trả về `0` sát thương, cộng `+15 Chi` cho Target, phát sự kiện `PerfectGuarded`.
   - Nếu có `State.Combat.Guarding` và đòn đánh không mang cờ `GuardBreaker` → trừ `Stamina` của Target; nếu đủ `Stamina` thì giảm `70%` sát thương nhận vào, nếu vỡ `Stamina` thì gắn `State.Debuff.Stunned (1.2s)`.
3. Kiểm tra chế độ bản đồ hiện tại qua `AZIUGameState::IsBalancedArenaPvP()`:
   - Nếu `true`: nén `PhysicalAttack / ChiAttack / Defense / MaxHP` qua hàm `ApplySoftTemplateCompression()`.
4. Quay xác suất `CritRate`, tính giảm trừ giáp $M_{\text{def}}$, nhân hệ số chênh lệch cấp độ $\Delta_{\text{level}}$ (trong PvE) và áp trần chống sốc sát thương `PvP Burst Cap` (trong Open-World PvP).
5. Xuất kết quả vào thuộc tính trung gian `VitalSet.IncomingDamage` và `VitalSet.IncomingStagger`, từ đó `UZIUVitalAttributeSet::PostGameplayEffectExecute` trừ thẳng vào `Health`, ghi điểm đóng góp vào `AZIUBossCharacter::RecordDamageContribution()` và phát sự kiện hạ gục nếu `Health <= 0`!
