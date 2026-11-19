// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_FlightComponent.generated.h"

class AIU_PlayerCharacter;
class UIU_MovementComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIU_OnFlightStateChangedSignature, bool, bIsNowFlying);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIU_OnFlightDashChangedSignature, bool, bIsNowDashing);

/**
 * Dedicated Free 3D Flight controller component (Milestone 2: P2-01 to P2-09).
 * Manages Ground <-> Flight <-> Landing state transitions, Space-hold activation,
 * 6-DOF camera-relative input blending, stationary Hover, and Flight Dash.
 */
UCLASS(ClassGroup = (InfiniteUniverse), meta = (BlueprintSpawnableComponent))
class INFINITEUNIVERSE_API UIU_FlightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIU_FlightComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ========================================================================
	// Mandatory Tunable Flight Parameters (Milestone 2 Specification)
	// ========================================================================

	/** Base 3D flight cruise speed (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "200.0", ClampMax = "5000.0"))
	float FlightSpeed = 1050.0f;

	/** Acceleration when applying directional input in flight (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "500.0", ClampMax = "15000.0"))
	float FlightAcceleration = 3400.0f;

	/** Aerodynamic braking deceleration when releasing input -> smooth Hover (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "500.0", ClampMax = "15000.0"))
	float FlightDeceleration = 3000.0f;

	/** Dedicated vertical ascend/descend speed on Space / Ctrl (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "200.0", ClampMax = "4000.0"))
	float VerticalFlightSpeed = 850.0f;

	/** Speed and acceleration multiplier during Flight Dash (Shift). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "1.1", ClampMax = "4.0"))
	float FlightDashMultiplier = 1.85f;

	// ========================================================================
	// Transition & Visual Feel Tuning
	// ========================================================================

	/** Seconds to hold Space before smoothly engaging Free Flight (no teleport). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Transition", meta = (ClampMin = "0.10", ClampMax = "1.0"))
	float SpaceHoldActivationTime = 0.28f;

	/** Gentle initial upward velocity (cm/s) when lifting off from ground (not a position teleport). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Transition", meta = (ClampMin = "0.0", ClampMax = "600.0"))
	float GroundLiftoffUpwardVelocity = 220.0f;

	/** Trace distance below capsule (cm) to smoothly transition from descending flight into Landing/Grounded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Transition", meta = (ClampMin = "2.0", ClampMax = "100.0"))
	float AutoLandFloorTraceDistance = 16.0f;

	/** Max forward pitch tilt (degrees) applied to visual mesh during Flight Dash. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Visuals", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float MaxVisualPitchLeanDegrees = 28.0f;

	/** Max roll bank angle (degrees) applied to visual mesh when strafing in air. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Visuals", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float MaxVisualRollBankDegrees = 18.0f;

	// ========================================================================
	// Flight Control Intents
	// ========================================================================

	void HandleJumpOrAscendStarted();
	void HandleJumpOrAscendCompleted();
	void HandleDescendStarted();
	void HandleDescendCompleted();
	void SetMoveAxisInput(const FVector2D& InMoveAxis);
	void SetFlightDashActive(bool bActive);

	UFUNCTION(BlueprintCallable, Category = "Flight")
	void ToggleFlightMode();

	UFUNCTION(BlueprintCallable, Category = "Flight")
	bool EnterFlight();

	UFUNCTION(BlueprintCallable, Category = "Flight")
	void ExitFlight(bool bEnteredViaLandingTouchdown = false);

	// ========================================================================
	// State Queries (For Movement, Camera & Animation Blueprint)
	// ========================================================================

	UFUNCTION(BlueprintPure, Category = "Flight")
	bool IsFlying() const;

	UFUNCTION(BlueprintPure, Category = "Flight")
	bool IsHovering() const;

	UFUNCTION(BlueprintPure, Category = "Flight")
	bool IsFlightDashing() const { return IsFlying() && bWantsFlightDash && !IsHoveringInputIdle(); }

	UFUNCTION(BlueprintPure, Category = "Flight")
	float GetVerticalFlightInput() const;

	UFUNCTION(BlueprintPure, Category = "Flight")
	float GetEffectiveMaxFlightSpeed() const;

	UFUNCTION(BlueprintPure, Category = "Flight")
	float GetEffectiveFlightAcceleration() const;

	/** Computes desired 3D world-space velocity vector based on Camera orientation and 6-DOF inputs. */
	FVector ComputeDesiredFlightVelocity() const;

	/** Visual-only mesh pitch/roll lean rotator (capsule remains upright). */
	UFUNCTION(BlueprintPure, Category = "Flight|Visuals")
	FRotator GetVisualFlightLeanRotator() const { return CurrentVisualLean; }

	UPROPERTY(BlueprintAssignable, Category = "Flight|Events")
	FIU_OnFlightStateChangedSignature OnFlightStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Flight|Events")
	FIU_OnFlightDashChangedSignature OnFlightDashChanged;

private:
	bool IsHoveringInputIdle() const;
	void OnSpaceHoldTimerExpired();
	void UpdateVisualFlightLean(float DeltaTime);

	UPROPERTY(Transient)
	TObjectPtr<AIU_PlayerCharacter> OwnerCharacter;

	UPROPERTY(Transient)
	TObjectPtr<UIU_MovementComponent> MovementComp;

	FVector2D CachedMoveAxis = FVector2D::ZeroVector;
	bool bAscendHeld = false;
	bool bDescendHeld = false;
	bool bWantsFlightDash = false;
	bool bLastBroadcastDashState = false;

	FRotator CurrentVisualLean = FRotator::ZeroRotator;
	FTimerHandle SpaceHoldTimerHandle;
};
