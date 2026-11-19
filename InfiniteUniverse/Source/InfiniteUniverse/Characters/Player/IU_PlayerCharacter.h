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
class UIU_SkeletalAnimDirectorComponent;

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

	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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
	void ToggleMobilityStyleInput();
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

	UFUNCTION(BlueprintPure, Category = "IU|Animation")
	UIU_SkeletalAnimDirectorComponent* GetAnimDirectorComponent() const { return AnimDirectorComponent; }

	UFUNCTION(BlueprintPure, Category = "IU|Placeholder")
	UStaticMeshComponent* GetPlaceholderBodyMesh() const { return PlaceholderBodyMesh; }

	/** Restores full HP/Ki, movement, Idle combat state, and spawn transform after defeat. */
	UFUNCTION(BlueprintCallable, Category = "IU|Lifecycle")
	void ResetPlayerAtSpawn();

	UFUNCTION(BlueprintPure, Category = "IU|Camera")
	USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	UFUNCTION(BlueprintPure, Category = "IU|Camera")
	UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	UFUNCTION(BlueprintPure, Category = "IU|Camera")
	float GetDesiredCameraArmLength() const { return DesiredCameraArmLength; }

	/** Returns the exact world-space origin and forward unit vector of the active third-person FollowCamera. */
	UFUNCTION(BlueprintPure, Category = "IU|Camera")
	bool GetCameraAimRay(FVector& OutCameraOrigin, FVector& OutCameraDirection) const;

	// ========================================================================
	// Tunable Camera Parameters (Genshin Impact-Inspired Elevated Framing)
	// ========================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "100.0", ClampMax = "1000.0"))
	float DefaultCameraArmLength = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "100.0", ClampMax = "600.0"))
	float MinCameraArmLength = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "250.0", ClampMax = "1200.0"))
	float MaxCameraArmLength = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "10.0", ClampMax = "200.0"))
	float CameraZoomStep = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float CameraZoomInterpSpeed = 12.0f;

	/** Upper-shoulder/neck orbit pivot on the character's central vertical axis (X=0, Y=0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera")
	FVector CameraTargetOffset = FVector(0.0f, 0.0f, 68.0f);

	/** Elevated Genshin-style socket offset (+52 cm up) placing character in lower half so center aim ray clears head. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera")
	FVector CameraSocketOffset = FVector(0.0f, 0.0f, 52.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float LookSensitivity = 1.0f;

	/** Natural anime action RPG field-of-view (86 deg keeps mid-range enemies crisp and clear to aim at). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "60.0", ClampMax = "120.0"))
	float DefaultCameraFOV = 86.0f;

	/** Expanded camera field-of-view during Flight Dash for high-speed sensation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "70.0", ClampMax = "130.0"))
	float FlightDashCameraFOV = 100.0f;

	/** Extra camera arm distance added smoothly while Flight Dashing (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "0.0", ClampMax = "300.0"))
	float FlightDashArmLengthOffset = 65.0f;

	/** Automatically equips /Game/Characters/Player/Meshes/SK_Player at BeginPlay when no custom mesh is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation")
	bool bAutoLoadDefaultAnimeMesh = true;

	/** Automatically respawns the defeated player at spawn transform so prototype testing never soft-locks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Lifecycle")
	bool bAutoRespawnOnDefeat = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Lifecycle", meta = (ClampMin = "0.2", ClampMax = "30.0"))
	float PlayerRespawnDelaySeconds = 3.0f;

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Animation")
	TObjectPtr<UIU_SkeletalAnimDirectorComponent> AnimDirectorComponent;

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

	UFUNCTION()
	void HandleCombatStateVisuals(EIU_CombatState PreviousState, EIU_CombatState NewState);

	UFUNCTION()
	void HandlePlayerDefeated(AActor* DepletedActor);

	FTransform InitialSpawnTransform;
	bool bHasSavedInitialSpawn = false;
	float DesiredCameraArmLength = 420.0f;
	FVector2D LastMoveAxisInput = FVector2D::ZeroVector;
	bool bFlightDashCameraActive = false;
	FTimerHandle StrikePulseResetTimerHandle;
	FTimerHandle VanishFlickerTimerHandle;
	FTimerHandle PlayerAutoRespawnTimerHandle;
	void ResetStrikeVisualPulse();
	void RestoreVanishVisibility();
	void UpdatePlaceholderVisibility();
};
