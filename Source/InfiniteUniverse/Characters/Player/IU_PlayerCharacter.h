// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_PlayerCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UStaticMeshComponent;
class UIU_MovementComponent;
class UIU_FlightComponent;
class UIU_AttributeComponent;
class UIU_DamageComponent;
class UIU_TargetingComponent;
class UIU_CombatComponent;

/**
 * Playable Third-Person Character orchestrator (Milestone 1).
 * Delegates locomotion physics to UIU_MovementComponent and receives semantic intents from AIU_PlayerController.
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_PlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	explicit AIU_PlayerCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	// ========================================================================
	// Semantic Input Receivers (Called by AIU_PlayerController)
	// ========================================================================

	/** Moves character relative to camera yaw on ground (X = Right, Y = Forward). */
	void ApplyMovementInput(const FVector2D& MoveAxis);

	/** Rotates controller yaw/pitch from mouse look input. */
	void ApplyLookInput(const FVector2D& LookAxis);

	/** Zooms third-person SpringArm in/out within configured bounds. */
	UFUNCTION(BlueprintCallable, Category = "IU|Camera")
	void AdjustCameraZoom(float ZoomAxisValue);

	void StartJumpInput();
	void StopJumpInput();
	void StartDescendInput();
	void StopDescendInput();
	void ToggleFlightInput();
	void StartSprintInput();
	void StopSprintInput();
	void ToggleRotationModeInput();
	void MeleeAttackInput();
	void KiBlastInput();
	void DodgeInput();
	void ToggleLockOnInput();
	void SwitchTargetInput(float Direction);

	/** Applies visual-only pitch/roll lean during 3D flight without tilting the collision capsule. */
	void ApplyVisualFlightLean(const FRotator& LeanRotator);

	// ========================================================================
	// Accessors
	// ========================================================================

	UFUNCTION(BlueprintPure, Category = "IU|Character")
	UIU_MovementComponent* GetIUMovementComponent() const { return IUMovementComponent; }

	UFUNCTION(BlueprintPure, Category = "IU|Flight")
	UIU_FlightComponent* GetFlightComponent() const { return FlightComponent; }

	UFUNCTION(BlueprintPure, Category = "IU|Attributes")
	UIU_AttributeComponent* GetAttributeComponent() const { return AttributeComponent; }

	UFUNCTION(BlueprintPure, Category = "IU|Combat")
	UIU_DamageComponent* GetDamageComponent() const { return DamageComponent; }

	UFUNCTION(BlueprintPure, Category = "IU|Combat")
	UIU_TargetingComponent* GetTargetingComponent() const { return TargetingComponent; }

	UFUNCTION(BlueprintPure, Category = "IU|Combat")
	UIU_CombatComponent* GetCombatComponent() const { return CombatComponent; }

	UFUNCTION(BlueprintPure, Category = "IU|Camera")
	USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	UFUNCTION(BlueprintPure, Category = "IU|Camera")
	UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	UFUNCTION(BlueprintPure, Category = "IU|Camera")
	float GetDesiredCameraArmLength() const { return DesiredCameraArmLength; }

	// ========================================================================
	// Tunable Camera Parameters
	// ========================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "100.0", ClampMax = "1000.0"))
	float DefaultCameraArmLength = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "100.0", ClampMax = "600.0"))
	float MinCameraArmLength = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "250.0", ClampMax = "1200.0"))
	float MaxCameraArmLength = 620.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "10.0", ClampMax = "200.0"))
	float CameraZoomStep = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float CameraZoomInterpSpeed = 12.0f;

	/** Pivot offset on the character's central vertical axis (X=0, Y=0 keeps character as pure orbit center). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera")
	FVector CameraTargetOffset = FVector(0.0f, 0.0f, 55.0f);

	/** Camera socket offset (Y=0 ensures camera looks straight along character axis without shoulder skew). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera")
	FVector CameraSocketOffset = FVector(0.0f, 0.0f, 20.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float LookSensitivity = 1.0f;

	/** Default camera field-of-view in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "60.0", ClampMax = "120.0"))
	float DefaultCameraFOV = 90.0f;

	/** Expanded camera field-of-view during Flight Dash for high-speed sensation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "70.0", ClampMax = "130.0"))
	float FlightDashCameraFOV = 102.0f;

	/** Extra camera arm distance added smoothly while Flight Dashing (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "0.0", ClampMax = "300.0"))
	float FlightDashArmLengthOffset = 65.0f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Movement")
	TObjectPtr<UIU_MovementComponent> IUMovementComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Flight")
	TObjectPtr<UIU_FlightComponent> FlightComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Attributes")
	TObjectPtr<UIU_AttributeComponent> AttributeComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Combat")
	TObjectPtr<UIU_DamageComponent> DamageComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Combat")
	TObjectPtr<UIU_TargetingComponent> TargetingComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Combat")
	TObjectPtr<UIU_CombatComponent> CombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** Visible placeholder body + forward visor when no SkeletalMesh is assigned yet (M1-M5). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Placeholder")
	TObjectPtr<UStaticMeshComponent> PlaceholderBodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Placeholder")
	TObjectPtr<UStaticMeshComponent> PlaceholderFacingVisor;

private:
	UFUNCTION()
	void HandleFlightDashChanged(bool bIsDashing);

	UFUNCTION()
	void HandleComboVisualPulse(int32 ComboStep);

	float DesiredCameraArmLength = 360.0f;
	FVector2D LastMoveAxisInput = FVector2D::ZeroVector;
	bool bFlightDashCameraActive = false;
	FTimerHandle StrikePulseResetTimerHandle;
	void ResetStrikeVisualPulse();
	void UpdatePlaceholderVisibility();
};
