// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "IU_PlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class AIU_PlayerCharacter;

/**
 * Enhanced Input PlayerController decoupling hardware key bindings from character movement execution.
 * All InputActions and MappingContexts are remappable and editable via Blueprint or Data config.
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AIU_PlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/**
	 * Populates an InputMappingContext in C++ using direct FEnhancedActionKeyMapping& references
	 * so axis swizzle/negate modifiers for W/A/S/D and Mouse2D are guaranteed persisted on the asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "IU|Input")
	static void ConfigureStandardPlayerMappings(
		UInputMappingContext* TargetIMC,
		UInputAction* InMove,
		UInputAction* InLook,
		UInputAction* InJump,
		UInputAction* InSprint,
		UInputAction* InZoom,
		UInputAction* InToggleRot,
		UInputAction* InDescend = nullptr,
		UInputAction* InFlightToggle = nullptr);

	// ========================================================================
	// Remappable Enhanced Input Assets
	// ========================================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> DescendAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> FlightToggleAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> CameraZoomAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> ToggleRotationModeAction;

	// ========================================================================
	// Camera Pitch Clamp Limits
	// ========================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "-89.0", ClampMax = "0.0"))
	float MinCameraPitch = -75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxCameraPitch = 75.0f;

private:
	AIU_PlayerCharacter* GetIUPlayerCharacter() const;

	void HandleMove(const FInputActionValue& Value);
	void HandleMoveCompleted(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleCameraZoom(const FInputActionValue& Value);
	void HandleJumpStarted(const FInputActionValue& Value);
	void HandleJumpCompleted(const FInputActionValue& Value);
	void HandleDescendStarted(const FInputActionValue& Value);
	void HandleDescendCompleted(const FInputActionValue& Value);
	void HandleFlightToggle(const FInputActionValue& Value);
	void HandleSprintStarted(const FInputActionValue& Value);
	void HandleSprintCompleted(const FInputActionValue& Value);
	void HandleToggleRotationMode(const FInputActionValue& Value);
};
