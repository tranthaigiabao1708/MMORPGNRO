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
 * Dedicated Free 3D Flight controller component (Milestone 2).
 * - Activated EXCLUSIVELY by double-Space (pressing Space while airborne / Jumping).
 * - Holding Space ascends with 2-stage physical momentum: slow anti-gravity gather in the first ~1.0s,
 *   then rapidly accelerating up to terminal FlightSpeed / VerticalFlightSpeed (1.5x baseline).
 * - Holding Left Shift descends (-Z) and auto-lands on walkable surfaces.
 * - Holding Left Ctrl applies 2.0x Flight Dash speed multiplier.
 */
UCLASS(ClassGroup = (InfiniteUniverse), meta = (BlueprintSpawnableComponent))
class INFINITEUNIVERSE_API UIU_FlightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIU_FlightComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ========================================================================
	// Tunable Flight Speed & Physics Parameters
	// ========================================================================

	/** Max normal 3D planar flight speed (cm/s) — 1.5x of previous 1050 baseline = 1575 cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "200.0", ClampMax = "8000.0"))
	float FlightSpeed = 1575.0f;

	/** Base planar acceleration when flying (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "400.0", ClampMax = "20000.0"))
	float FlightAcceleration = 3200.0f;

	/** Aerodynamic braking deceleration when releasing inputs -> smooth Hover (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "400.0", ClampMax = "20000.0"))
	float FlightDeceleration = 3000.0f;

	/** Max normal vertical flight speed (cm/s) — 1.5x of previous 850 baseline = 1275 cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "200.0", ClampMax = "6000.0"))
	float VerticalFlightSpeed = 1275.0f;

	/** Speed & thrust multiplier when holding Left Ctrl in flight (x2.0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "1.1", ClampMax = "5.0"))
	float FlightDashMultiplier = 2.0f;

	// ========================================================================
	// 2-Stage Physical Ascent Curve Parameters
	// ========================================================================

	/** Duration (seconds) of the initial slow levitation phase right after double-Space activation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|PhysicsCurve", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float InitialSlowAscentDuration = 1.0f;

	/** Speed ratio at t = 0s when ascent begins (0.18 = gentle 18% initial upward drift). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|PhysicsCurve", meta = (ClampMin = "0.05", ClampMax = "0.6"))
	float InitialAscentStartRatio = 0.18f;

	/** Speed ratio reached at the end of the 1.0s slow phase before surging to 100%. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|PhysicsCurve", meta = (ClampMin = "0.15", ClampMax = "0.8"))
	float SlowPhaseEndSpeedRatio = 0.38f;

	/** Duration (seconds) after the 1.0s slow phase to surge from SlowPhaseEndSpeedRatio up to 100% terminal speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|PhysicsCurve", meta = (ClampMin = "0.2", ClampMax = "4.0"))
	float AscentSurgeRampDuration = 1.10f;

	/** Trace distance below capsule (cm) to smoothly transition from descending flight into Landing/Grounded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Transition", meta = (ClampMin = "2.0", ClampMax = "100.0"))
	float AutoLandFloorTraceDistance = 16.0f;

	/** Max forward pitch tilt (degrees) applied to visual mesh during high-speed flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Visuals", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float MaxVisualPitchLeanDegrees = 30.0f;

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

	/** Cancels flight if currently flying (does NOT enter flight from ground; flight requires airborne Space). */
	UFUNCTION(BlueprintCallable, Category = "Flight")
	void ToggleFlightMode();

	/** Enters flight mode (intended to be called when airborne on 2nd Space press). */
	UFUNCTION(BlueprintCallable, Category = "Flight")
	bool EnterFlight();

	UFUNCTION(BlueprintCallable, Category = "Flight")
	void ExitFlight(bool bEnteredViaLandingTouchdown = false);

	// ========================================================================
	// State & Physics Queries
	// ========================================================================

	UFUNCTION(BlueprintPure, Category = "Flight")
	bool IsFlying() const;

	UFUNCTION(BlueprintPure, Category = "Flight")
	bool IsHovering() const;

	UFUNCTION(BlueprintPure, Category = "Flight")
	bool IsFlightDashing() const;

	bool IsAscendHeld() const { return bAscendHeld; }
	bool IsDescendHeld() const { return bDescendHeld; }
	bool WantsFlightDash() const { return bWantsFlightDash; }

	/** Synchronizes compressed SavedMove flight flags on Server Authority and Client Replay (Step 2). */
	void ApplyNetworkSavedFlightFlags(bool bNetWantsFlight, bool bNetAscendHeld, bool bNetDescendHeld, bool bNetFlightDash);

	UFUNCTION(BlueprintPure, Category = "Flight")
	float GetVerticalFlightInput() const;

	UFUNCTION(BlueprintPure, Category = "Flight")
	float GetEffectiveMaxFlightSpeed() const;

	UFUNCTION(BlueprintPure, Category = "Flight")
	float GetEffectiveFlightAcceleration() const;

	/** Evaluates the non-linear [0..1] ascent speed envelope at current hold time. */
	UFUNCTION(BlueprintPure, Category = "Flight|PhysicsCurve")
	float EvaluateAscentSpeedCurveFactor() const;

	/** Advances internal flight & ascent timers cleanly during physics simulation. */
	void StepFlightPhysicsTimers(float DeltaTime);

	/** Computes desired 3D world-space velocity vector with physical ascent curve & Ctrl x2 cap. */
	FVector ComputeDesiredFlightVelocity() const;

	UFUNCTION(BlueprintPure, Category = "Flight|Visuals")
	FRotator GetVisualFlightLeanRotator() const { return CurrentVisualLean; }

	UPROPERTY(BlueprintAssignable, Category = "Flight|Events")
	FIU_OnFlightStateChangedSignature OnFlightStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Flight|Events")
	FIU_OnFlightDashChangedSignature OnFlightDashChanged;

private:
	bool IsHoveringInputIdle() const;
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

	UPROPERTY(Replicated)
	bool bReplicatedFlightDashing = false;

	/** Elapsed seconds since entering flight via double-Space. */
	float TimeSinceFlightEntered = 0.0f;

	/** Elapsed seconds continuously holding Space to ascend. */
	float ContinuousAscentHoldTime = 0.0f;

	/** True during the initial uninterrupted Space hold right after double-Space activation. */
	bool bInInitialActivationAscent = false;

	FRotator CurrentVisualLean = FRotator::ZeroRotator;
};
