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
	void StartSprintInput();
	void StopSprintInput();
	void ToggleRotationModeInput();

	// ========================================================================
	// Accessors
	// ========================================================================

	UFUNCTION(BlueprintPure, Category = "IU|Character")
	UIU_MovementComponent* GetIUMovementComponent() const { return IUMovementComponent; }

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera")
	FVector CameraSocketOffset = FVector(0.0f, 45.0f, 65.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float LookSensitivity = 1.0f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Movement")
	TObjectPtr<UIU_MovementComponent> IUMovementComponent;

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
	float DesiredCameraArmLength = 360.0f;
	void UpdatePlaceholderVisibility();
};
