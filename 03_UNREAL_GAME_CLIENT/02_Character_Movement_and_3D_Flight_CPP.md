# 02. Character Movement & 3D Flight C++ Specification

**Phân hệ:** `03_UNREAL_GAME_CLIENT` · **Tài liệu:** `02_Character_Movement_and_3D_Flight_CPP.md`  
**Module đích:** `Source/ZIUMovement/`

---

## 1. Tại Sao Không Dùng Chế Độ `MOVE_Flying` Mặc Định Của Unreal?

Chế độ `MOVE_Flying` có sẵn trong `UCharacterMovementComponent` gốc của Unreal chỉ là chế độ bay đơn giản kiểu camera quan sát (thiếu phân biệt giữa *Lơ lửng không tốn khí (`Hover`)*, *Bay hành trình (`Cruise`)*, *Bay siêu thanh bẻ góc có quán tính (`High-Speed Boost`)* và *Bay bám khóa mục tiêu 3D (`Aerial Pursuit`)*, cũng như không nén cờ bay tốc độ cao vào gói tin dự đoán `FSavedMove`).

Do đó, dự án mở rộng trực tiếp `UCharacterMovementComponent` thông qua `MOVE_Custom` kết hợp `CustomMovementMode` chuẩn mạng.

---

## 2. Khai Báo C++ Chuẩn Cho `UZIUFlightMovementComponent`

```cpp
// Source/ZIUMovement/Public/ZIUFlightMovementComponent.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ZIUFlightMovementComponent.generated.h"

UENUM(BlueprintType)
enum EZIUFlightMovementMode : uint8
{
    CMOVE_None          = 0 UMETA(Hidden),
    CMOVE_Hover         = 1 UMETA(DisplayName = "3D Hover"),
    CMOVE_FlightCruise  = 2 UMETA(DisplayName = "3D Flight Cruise"),
    CMOVE_FlightBoost   = 3 UMETA(DisplayName = "High-Speed Flight"),
    CMOVE_AerialPursuit = 4 UMETA(DisplayName = "Target Homing Pursuit")
};

UCLASS(ClassGroup=(ZIU), meta=(BlueprintSpawnableComponent))
class ZIUMOVEMENT_API UZIUFlightMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()

public:
    UZIUFlightMovementComponent();

    // --- Cấu hình Tốc độ & Khí động học (Đọc từ DataAsset / GAS Attributes) ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZIU|Flight")
    float HoverMaxSpeed = 250.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZIU|Flight")
    float HoverBrakingDeceleration = 2800.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZIU|Flight")
    float CruiseMaxSpeed = 1100.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZIU|Flight")
    float CruiseAcceleration = 3200.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZIU|Flight")
    float BoostMaxSpeed = 2400.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZIU|Flight")
    float BoostAcceleration = 5400.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZIU|Flight")
    float BoostTurnInterpSpeed = 6.5f; // Tạo cảm giác vòng cung quán tính mượt khi bay siêu thanh

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ZIU|Flight")
    float PursuitSpeed = 3400.f;

    // --- API Điều khiển từ Enhanced Input / GAS ---
    UFUNCTION(BlueprintCallable, Category = "ZIU|Flight")
    void SetWantsFlightToggle(bool bWantsToggle);

    UFUNCTION(BlueprintCallable, Category = "ZIU|Flight")
    void SetWantsFlightBoost(bool bWantsBoost);

    UFUNCTION(BlueprintCallable, Category = "ZIU|Flight")
    void SetVerticalFlightInput(float AxisValue); // +1.0 (Space) đến -1.0 (LeftCtrl)

    UFUNCTION(BlueprintCallable, Category = "ZIU|Flight")
    void StartAerialPursuit(AActor* TargetActor, float MaxDuration = 0.55f);

    UFUNCTION(BlueprintPure, Category = "ZIU|Flight")
    bool IsInAnyFlightMode() const;

    // --- Overrides chuẩn UCharacterMovementComponent ---
    virtual float GetMaxSpeed() const override;
    virtual float GetMaxAcceleration() const override;
    virtual float GetMaxBrakingDeceleration() const override;
    virtual void UpdateFromCompressedFlags(uint8 Flags) override;
    virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;

protected:
    virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;
    virtual void PhysCustom(float deltaTime, int32 Iterations) override;

    void PhysFlightHoverAndCruise(float DeltaTime, int32 Iterations);
    void PhysFlightBoost(float DeltaTime, int32 Iterations);
    void PhysAerialPursuit(float DeltaTime, int32 Iterations);

private:
    uint8 bWantsFlightToggle : 1;
    uint8 bWantsFlightBoost  : 1;
    float PendingVerticalInput = 0.f;

    TWeakObjectPtr<AActor> PursuitTarget;
    float PursuitRemainingTime = 0.f;
};
```

---

## 3. Cấu Trúc Dự Đoán Mạng `FSavedMove_ZIUFlight`

Sử dụng 2 bit cờ dự phòng hợp chuẩn của Unreal (`FLAG_Custom_0` cho `FlightToggle` và `FLAG_Custom_1` cho `FlightBoost`), còn thành phần vận tốc dọc trục `Z` được đưa trực tiếp vào `Acceleration` vector của `CharacterMovementComponent` trước khi đóng gói `ServerMovePacked`:

```cpp
// Source/ZIUMovement/Public/ZIUSavedMove_Flight.h
class FSavedMove_ZIUFlight : public FSavedMove_Character
{
    typedef FSavedMove_Character Super;
public:
    uint8 bSavedWantsFlightToggle : 1;
    uint8 bSavedWantsFlightBoost  : 1;

    virtual void Clear() override
    {
        Super::Clear();
        bSavedWantsFlightToggle = false;
        bSavedWantsFlightBoost  = false;
    }

    virtual uint8 GetCompressedFlags() const override
    {
        uint8 Result = Super::GetCompressedFlags();
        if (bSavedWantsFlightToggle) Result |= FLAG_Custom_0;
        if (bSavedWantsFlightBoost)  Result |= FLAG_Custom_1;
        return Result;
    }

    virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
    {
        const FSavedMove_ZIUFlight* NewZIUMove = static_cast<const FSavedMove_ZIUFlight*>(NewMove.Get());
        if (bSavedWantsFlightToggle != NewZIUMove->bSavedWantsFlightToggle ||
            bSavedWantsFlightBoost  != NewZIUMove->bSavedWantsFlightBoost)
        {
            return false; // Không gộp gói tin ngay tại frame chuyển trạng thái bay!
        }
        return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
    }

    virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
    virtual void PrepMoveFor(ACharacter* C) override;
};
```

---

## 4. Phương Trình Vật Lý Từng Chế Độ Trong `PhysCustom`

Trong mỗi bước chia nhỏ `Sub-step` của `PhysCustom(float deltaTime, int32 Iterations)`:

### 4.1. `PhysFlightHoverAndCruise` (`CMOVE_Hover` & `CMOVE_FlightCruise`)
1. **Tính Vector Gia tốc Không gian 3D ($\mathbf{A}_{3D}$):**
   - Khi người chơi nhập `WASD`, hướng tiến `Forward` lấy theo **3D Control Rotation của Camera** (ngước camera lên trời bấm `W` là bay chéo lên trời tự nhiên), cộng thêm vector thẳng đứng thế giới `(0, 0, PendingVerticalInput)` rồi chuẩn hóa độ dài `<= 1.0`.
2. **Chuyển đổi mượt `Hover ↔ Cruise`:**
   - Nếu $|\mathbf{A}_{3D}| < 0.05$: đặt `CustomMovementMode = CMOVE_Hover`, áp dụng lực hãm `CalcVelocity(DeltaTime, Friction = 4.0f, bFluid = true, HoverBrakingDeceleration)` để nhân vật dừng êm ái giữa không trung trong `0.25s`, đồng thời kích hoạt sóng nhấp nhô nhẹ ở tầng hình ảnh (`Sinusoidal Mesh Offset ±4cm`, không làm đổi tọa độ Capsule mạng!).
   - Nếu $|\mathbf{A}_{3D}| \ge 0.05$: đặt `CustomMovementMode = CMOVE_FlightCruise`, tăng tốc tới `CruiseMaxSpeed * SpeedMultiplierFromGAS`.
3. **Trượt Va chạm & Tiếp đất Tự động (`SafeMoveUpdatedComponent` + `SlideAlongSurface`):**
   - Nếu bay sát mặt đất trong khi nhấn hạ độ cao (`PendingVerticalInput < -0.2f`) và quét tia xuống dưới thấy mặt sàn hợp lệ (`WalkableFloorZ >= 0.7f`) cách `< 8cm`, tự động gọi `SetMovementMode(MOVE_Walking)` để nhân vật đáp chân chạy tiếp mượt mà không bị khựng.

### 4.2. `PhysFlightBoost` (`CMOVE_FlightBoost` — Bay Siêu Thanh)
1. **Kiểm tra Năng lượng có Thẩm quyền:**
   - Kiểm tra qua `UZIUAbilitySystemComponent`: nếu nhân vật dính thẻ `State.Debuff.ChiExhausted` hoặc `State.Debuff.Stunned`, lập tức hạ về `CMOVE_FlightCruise`.
   - Việc trừ `Chi` khi đang bay `Boost` được thực hiện bởi một `Infinite GameplayEffect` chu kỳ `0.25s` trên Server (tránh gửi RPC mỗi frame!).
2. **Định hướng Quán tính Khí động học (`Banked Steering`):**
   - Thay vì bẻ ngoặt vuông góc `180°` mất tự nhiên ở vận tốc `2,400 cm/s`, hướng vận tốc hiện tại nội suy cầu (`VInterpNormalRotationTo`) về hướng nhìn mới với tốc độ góc `BoostTurnInterpSpeed`. Nếu người chơi kéo ngược hướng `S + Boost`, kích hoạt `Quick Air Brake Turn` tốn nhẹ `10 Stamina` để xoay gắt phản công.

### 4.3. `PhysAerialPursuit` (`CMOVE_AerialPursuit` — Lao Bám Sau Đòn Hất Tung)
- Khi người chơi đánh trúng đòn `Uppercut Launch`, Server gắn thẻ tạm `State.Window.CanPursuit (0.6s)`.
- Khi kích hoạt `StartAerialPursuit(TargetActor)`: nhân vật lao theo đường cong dẫn hướng tới điểm dừng cách trước mặt mục tiêu `115 cm` (`StopStandoffDistance`), tự kết thúc sang `CMOVE_Hover` ngay khi tới tầm đánh để người chơi nối liền chuỗi `Air LMB 1 → 2 → 3 → Slam`!
