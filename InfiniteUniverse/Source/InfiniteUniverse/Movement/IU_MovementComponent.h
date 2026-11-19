// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_MovementComponent.generated.h"

class UIU_FlightComponent;
class UIU_CombatComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FIU_OnMovementModeChangedSignature,
	EIU_PlayerMovementMode, PreviousMode,
	EIU_PlayerMovementMode, NewMode);

/**
 * Custom CharacterMovementComponent centralizing all tunable ground locomotion parameters (M1)
 * and extensible custom movement physics for Free 3D Flight (M2).
 */
UCLASS(ClassGroup = (InfiniteUniverse), meta = (BlueprintSpawnableComponent))
class INFINITEUNIVERSE_API UIU_MovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	static constexpr uint8 CMOVE_FreeFlight = 1;

	UIU_MovementComponent();

	virtual void BeginPlay() override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual void PhysCustom(float deltaTime, int32 Iterations) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxAcceleration() const override;
	virtual float GetMaxBrakingDeceleration() const override;

	// ========================================================================
	// Tunable Ground Locomotion Parameters (Single Source of Truth)
	// ========================================================================

	/** Slow movement speed while crouching on ground with Left Shift (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Movement|Ground", meta = (ClampMin = "50.0", ClampMax = "1200.0"))
	float CrouchWalkSpeed = 250.0f;

	/** Standard jog/walk speed on ground (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Movement|Ground", meta = (ClampMin = "100.0", ClampMax = "2000.0"))
	float WalkSpeed = 520.0f;

	/** Fast run/sprint speed when holding Left Ctrl on ground (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Movement|Ground", meta = (ClampMin = "200.0", ClampMax = "3000.0"))
	float SprintSpeed = 880.0f;

	/** Capsule half-height when crouched on ground (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Movement|Ground", meta = (ClampMin = "30.0", ClampMax = "85.0"))
	float CrouchedCapsuleHalfHeight = 56.0f;

	/** Ground acceleration (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Movement|Ground", meta = (ClampMin = "500.0", ClampMax = "10000.0"))
	float GroundAcceleration = 2400.0f;

	/** Braking deceleration when releasing movement input on ground (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Movement|Ground", meta = (ClampMin = "500.0", ClampMax = "10000.0"))
	float GroundBrakingDeceleration = 2200.0f;

	/** Initial vertical velocity on Jump (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Movement|Jump", meta = (ClampMin = "200.0", ClampMax = "2000.0"))
	float JumpVelocity = 650.0f;

	/** Directional air control multiplier while jumping/falling [0..1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Movement|Jump", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirControlRatio = 0.50f;

	/** Gravity scale tuned for crisp anime action jumps (avoids floaty descent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Movement|Jump", meta = (ClampMin = "0.5", ClampMax = "4.0"))
	float DefaultGravityScale = 1.55f;

	/** Yaw turn rate (deg/s) when orienting character toward camera or movement direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Movement|Rotation", meta = (ClampMin = "90.0", ClampMax = "1440.0"))
	float MovementRotationRateYaw = 720.0f;

	// ========================================================================
	// Runtime Locomotion API
	// ========================================================================

	/** Re-applies tunable UPROPERTY parameters to the underlying CMC fields. */
	UFUNCTION(BlueprintCallable, Category = "IU|Movement")
	void RefreshMovementSettings();

	/** Enables or disables ground sprinting and updates MaxWalkSpeed accordingly. */
	UFUNCTION(BlueprintCallable, Category = "IU|Movement")
	void SetSprinting(bool bNewSprinting);

	UFUNCTION(BlueprintPure, Category = "IU|Movement")
	bool IsSprinting() const { return bIsSprinting; }

	/** Switches between movement-oriented rotation and camera-facing rotation. */
	UFUNCTION(BlueprintCallable, Category = "IU|Movement")
	void SetCharacterRotationMode(EIU_CharacterRotationMode NewRotationMode);

	UFUNCTION(BlueprintPure, Category = "IU|Movement")
	EIU_CharacterRotationMode GetCharacterRotationMode() const { return CurrentRotationMode; }

	UFUNCTION(BlueprintPure, Category = "IU|Movement")
	EIU_PlayerMovementMode GetIUMovementMode() const { return CurrentIUMovementMode; }

	/** Explicit hook for M2 FlightComponent to transition high-level movement state. */
	void SetIUMovementMode(EIU_PlayerMovementMode NewMode);

	void EnterCustomFreeFlight();
	void ExitCustomFreeFlight(bool bTouchdownOnFloor);
	bool IsInCustomFreeFlight() const;

	UPROPERTY(BlueprintAssignable, Category = "IU|Movement|Events")
	FIU_OnMovementModeChangedSignature OnIUMovementModeChanged;

private:
	void PhysCustom_FreeFlight(float DeltaTime, int32 Iterations);
	bool TryAutoLandOnWalkableFloor();

	UPROPERTY(Transient)
	TObjectPtr<UIU_FlightComponent> CachedFlightComponent;

	UPROPERTY(Transient)
	TObjectPtr<UIU_CombatComponent> CachedCombatComponent;

	bool bIsSprinting = false;

	UPROPERTY(VisibleInstanceOnly, Category = "IU|Movement|State")
	EIU_CharacterRotationMode CurrentRotationMode = EIU_CharacterRotationMode::OrientToCamera;

	UPROPERTY(VisibleInstanceOnly, Category = "IU|Movement|State")
	EIU_PlayerMovementMode CurrentIUMovementMode = EIU_PlayerMovementMode::Grounded;
};

/**
 * Step 2 Client-Side SavedMove packing Sprint, Free 3D Flight state, Ascend/Descend & Flight Dash
 * into UE5's native 60Hz ServerMove bitflags with zero extra RPC bandwidth.
 */
class INFINITEUNIVERSE_API FSavedMove_IUCharacter : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	uint8 bSavedIsSprinting : 1;
	uint8 bSavedInFreeFlight : 1;
	uint8 bSavedAscendHeld : 1;
	uint8 bSavedDescendHeld : 1;

	FSavedMove_IUCharacter();

	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* C) override;
};

class INFINITEUNIVERSE_API FNetworkPredictionData_Client_IUCharacter : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;

	explicit FNetworkPredictionData_Client_IUCharacter(const UCharacterMovementComponent& ClientMovement);
	virtual FSavedMovePtr AllocateNewMove() override;
};
