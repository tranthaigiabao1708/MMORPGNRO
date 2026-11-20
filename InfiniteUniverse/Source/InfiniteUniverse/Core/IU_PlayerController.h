// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_PlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class AIU_PlayerCharacter;
class UIU_LoginCharacterWidget;
class UIU_PotentialUpgradeWidget;

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

	/** Marks synthetic headless automation test controllers (without ULocalPlayer) as locally controlled in UE 5.8+. */
	void MarkAsLocalTestController() { SetAsLocalPlayerController(); }

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
		UInputAction* InFlightToggle = nullptr,
		UInputAction* InMeleeAttack = nullptr,
		UInputAction* InKiBlast = nullptr,
		UInputAction* InDodge = nullptr,
		UInputAction* InLockOn = nullptr,
		UInputAction* InSwitchTarget = nullptr,
		UInputAction* InToggleMobilityStyle = nullptr);

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> MeleeAttackAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> KiBlastAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> DodgeAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> LockOnAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> SwitchTargetAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> ToggleMobilityStyleAction;

	// ========================================================================
	// Camera Pitch Clamp Limits & Genshin-Style Initial Overlook Angle
	// ========================================================================

	/** Gentle downward overlook pitch (-8.5 deg) on spawn so enemies ahead & character both stay clearly visible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "-35.0", ClampMax = "15.0"))
	float DefaultStartingCameraPitch = -8.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "-89.0", ClampMax = "0.0"))
	float MinCameraPitch = -75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxCameraPitch = 75.0f;

	/** Applies DefaultStartingCameraPitch while preserving current controller yaw. */
	UFUNCTION(BlueprintCallable, Category = "IU|Camera")
	void ApplyInitialGameplayCameraAngle();

	/** Requests Server Authority to toggle global PvP sparring mode (F4 shortcut in Development). */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Multiplayer")
	void Server_ToggleGlobalPvPSparring();

	/** Server-authoritative RPC: spends accumulated Potential Points to upgrade MaxHP (+100), MaxKi (+100), or AttackPower (+10). */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Potential")
	void Server_RequestAllocatePotential(EIU_PotentialStatType StatType);

	/** Shows or hides the NRO Potential & Stat Allocation UMG modal (Key P). */
	UFUNCTION(BlueprintCallable, Category = "IU|UI")
	void SetPotentialUpgradePanelVisible(bool bVisible, bool bUpdateMouseInputMode = true);

	UFUNCTION(BlueprintPure, Category = "IU|UI")
	bool IsPotentialUpgradePanelVisible() const;

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
	void HandleMeleeAttack(const FInputActionValue& Value);
	void HandleKiBlast(const FInputActionValue& Value);
	void HandleDodge(const FInputActionValue& Value);
	void HandleLockOn(const FInputActionValue& Value);
	void HandleSwitchTarget(const FInputActionValue& Value);
	void HandleToggleMobilityStyle(const FInputActionValue& Value);
	void HandleToggleDebugHUD();
	void HandleToggleGlobalPvP();
	void HandleToggleAuthPanel();
	void HandleTogglePotentialPanel();
	void HandleQuickUpgradeHP();
	void HandleQuickUpgradeKi();
	void HandleQuickUpgradeAtk();

	UPROPERTY(Transient)
	TObjectPtr<UIU_LoginCharacterWidget> ActiveAuthWidget;

	UPROPERTY(Transient)
	TObjectPtr<UIU_PotentialUpgradeWidget> ActivePotentialWidget;
};
