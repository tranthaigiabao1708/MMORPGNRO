// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_PlayerController.h"
#include "Core/IU_GameState.h"
#include "Core/IU_PlayerState.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Network/IU_BackendSubsystem.h"
#include "UI/IU_HUDManager.h"
#include "UI/IU_LoginCharacterWidget.h"
#include "UI/IU_PotentialUpgradeWidget.h"
#include "UI/IU_InventoryEquipmentWidget.h"
#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"
#include "Templates/Atomic.h"
#include "Engine/GameInstance.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "InfiniteUniverse.h"

AIU_PlayerController::AIU_PlayerController()
{
}

void AIU_PlayerController::ConfigureStandardPlayerMappings(
	UInputMappingContext* TargetIMC,
	UInputAction* InMove,
	UInputAction* InLook,
	UInputAction* InJump,
	UInputAction* InSprint,
	UInputAction* InZoom,
	UInputAction* InToggleRot,
	UInputAction* InDescend,
	UInputAction* InFlightToggle,
	UInputAction* InMeleeAttack,
	UInputAction* InKiBlast,
	UInputAction* InDodge,
	UInputAction* InLockOn,
	UInputAction* InSwitchTarget,
	UInputAction* InToggleMobilityStyle)
{
	if (!TargetIMC)
	{
		return;
	}

	TargetIMC->UnmapAll();

	if (InMove)
	{
		// W -> +Y (Forward along Camera view)
		FEnhancedActionKeyMapping& MapW = TargetIMC->MapKey(InMove, EKeys::W);
		UInputModifierSwizzleAxis* SwizzleW = NewObject<UInputModifierSwizzleAxis>(TargetIMC);
		SwizzleW->Order = EInputAxisSwizzle::YXZ;
		MapW.Modifiers.Add(SwizzleW);

		// S -> -Y (Backward)
		FEnhancedActionKeyMapping& MapS = TargetIMC->MapKey(InMove, EKeys::S);
		UInputModifierSwizzleAxis* SwizzleS = NewObject<UInputModifierSwizzleAxis>(TargetIMC);
		SwizzleS->Order = EInputAxisSwizzle::YXZ;
		UInputModifierNegate* NegateS = NewObject<UInputModifierNegate>(TargetIMC);
		MapS.Modifiers.Add(SwizzleS);
		MapS.Modifiers.Add(NegateS);

		// D -> +X (Right)
		TargetIMC->MapKey(InMove, EKeys::D);

		// A -> -X (Left)
		FEnhancedActionKeyMapping& MapA = TargetIMC->MapKey(InMove, EKeys::A);
		UInputModifierNegate* NegateA = NewObject<UInputModifierNegate>(TargetIMC);
		MapA.Modifiers.Add(NegateA);
	}

	if (InLook)
	{
		// Mouse2D -> +X Yaw (Orbit Left/Right), -Y Pitch (Mouse Up looks Up naturally)
		FEnhancedActionKeyMapping& MapMouse = TargetIMC->MapKey(InLook, EKeys::Mouse2D);
		UInputModifierNegate* NegatePitch = NewObject<UInputModifierNegate>(TargetIMC);
		NegatePitch->bX = false;
		NegatePitch->bY = true;
		NegatePitch->bZ = false;
		MapMouse.Modifiers.Add(NegatePitch);
	}

	if (InJump)
	{
		TargetIMC->MapKey(InJump, EKeys::SpaceBar);
	}
	if (InSprint)
	{
		// Left Ctrl: Fast Sprint on Ground + x2 Speed Boost in Flight
		TargetIMC->MapKey(InSprint, EKeys::LeftControl);
	}
	if (InZoom)
	{
		TargetIMC->MapKey(InZoom, EKeys::MouseWheelAxis);
	}
	if (InToggleRot)
	{
		TargetIMC->MapKey(InToggleRot, EKeys::Tab);
	}
	if (InDescend)
	{
		// Left Shift: Crouch & Slow Walk on Ground + Descend Altitude in Flight
		TargetIMC->MapKey(InDescend, EKeys::LeftShift);
	}
	if (InFlightToggle)
	{
		TargetIMC->MapKey(InFlightToggle, EKeys::F);
	}
	if (InMeleeAttack)
	{
		TargetIMC->MapKey(InMeleeAttack, EKeys::LeftMouseButton);
	}
	if (InKiBlast)
	{
		// Q: Ki Blast Energy Projectile ("Chưởng")
		TargetIMC->MapKey(InKiBlast, EKeys::Q);
	}
	if (InDodge)
	{
		// Right Mouse Button: Contextual Dash / Short Blink / Combat Evade if Enemy Present
		TargetIMC->MapKey(InDodge, EKeys::RightMouseButton);
	}
	if (InLockOn)
	{
		TargetIMC->MapKey(InLockOn, EKeys::MiddleMouseButton);
		TargetIMC->MapKey(InLockOn, EKeys::C);
	}
	if (InSwitchTarget)
	{
		TargetIMC->MapKey(InSwitchTarget, EKeys::E);
	}
	if (InToggleMobilityStyle)
	{
		// V: Switch RMB Archetype between Earthling Dash and Alien InstantBlink
		TargetIMC->MapKey(InToggleMobilityStyle, EKeys::V);
	}
}

void AIU_PlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (PlayerCameraManager)
	{
		PlayerCameraManager->ViewPitchMin = MinCameraPitch;
		PlayerCameraManager->ViewPitchMax = MaxCameraPitch;
	}

	// Fallback soft load if spawned directly from C++ AIU_GameMode without a Blueprint subclass override
	if (!DefaultMappingContext)
	{
		DefaultMappingContext = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Player.IMC_Player"));
	}

	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		ApplyInitialGameplayCameraAngle();

		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
		{
			if (DefaultMappingContext)
			{
				InputSubsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}

		if (UGameInstance* GI = GetGameInstance())
		{
			if (UIU_BackendSubsystem* Backend = GI->GetSubsystem<UIU_BackendSubsystem>())
			{
				Backend->TryTriggerCommandLineAutoAuth(this);
			}
		}

		if (FParse::Param(FCommandLine::Get(), TEXT("ShowLoginUI")) && GetNetMode() != NM_Client)
		{
			HandleToggleAuthPanel();
		}
	}
}

void AIU_PlayerController::ApplyInitialGameplayCameraAngle()
{
	FRotator InitialRot = GetControlRotation();
	InitialRot.Pitch = FMath::Clamp(DefaultStartingCameraPitch, MinCameraPitch, MaxCameraPitch);
	InitialRot.Roll = 0.0f;
	SetControlRotation(InitialRot);
}

void AIU_PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Load default InputAction assets on demand if not overridden in Blueprint
	if (!MoveAction)
	{
		MoveAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
	}
	if (!LookAction)
	{
		LookAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
	}
	if (!JumpAction)
	{
		JumpAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
	}
	if (!DescendAction)
	{
		DescendAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Descend.IA_Descend"));
	}
	if (!FlightToggleAction)
	{
		FlightToggleAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_FlightToggle.IA_FlightToggle"));
	}
	if (!SprintAction)
	{
		SprintAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Sprint.IA_Sprint"));
	}
	if (!CameraZoomAction)
	{
		CameraZoomAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_CameraZoom.IA_CameraZoom"));
	}
	if (!ToggleRotationModeAction)
	{
		ToggleRotationModeAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_ToggleRotationMode.IA_ToggleRotationMode"));
	}
	if (!MeleeAttackAction)
	{
		MeleeAttackAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_MeleeAttack.IA_MeleeAttack"));
	}
	if (!KiBlastAction)
	{
		KiBlastAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_KiBlast.IA_KiBlast"));
	}
	if (!DodgeAction)
	{
		DodgeAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Dodge.IA_Dodge"));
	}
	if (!LockOnAction)
	{
		LockOnAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_LockOn.IA_LockOn"));
	}
	if (!SwitchTargetAction)
	{
		SwitchTargetAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_SwitchTarget.IA_SwitchTarget"));
	}
	if (!ToggleMobilityStyleAction)
	{
		ToggleMobilityStyleAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_ToggleMobilityStyle.IA_ToggleMobilityStyle"));
	}

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (MoveAction)
		{
			EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AIU_PlayerController::HandleMove);
			EnhancedInput->BindAction(MoveAction, ETriggerEvent::Completed, this, &AIU_PlayerController::HandleMoveCompleted);
		}
		if (LookAction)
		{
			EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AIU_PlayerController::HandleLook);
		}
		if (CameraZoomAction)
		{
			EnhancedInput->BindAction(CameraZoomAction, ETriggerEvent::Triggered, this, &AIU_PlayerController::HandleCameraZoom);
		}
		if (JumpAction)
		{
			EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &AIU_PlayerController::HandleJumpStarted);
			EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &AIU_PlayerController::HandleJumpCompleted);
		}
		if (DescendAction)
		{
			EnhancedInput->BindAction(DescendAction, ETriggerEvent::Started, this, &AIU_PlayerController::HandleDescendStarted);
			EnhancedInput->BindAction(DescendAction, ETriggerEvent::Completed, this, &AIU_PlayerController::HandleDescendCompleted);
		}
		if (FlightToggleAction)
		{
			EnhancedInput->BindAction(FlightToggleAction, ETriggerEvent::Started, this, &AIU_PlayerController::HandleFlightToggle);
		}
		if (SprintAction)
		{
			EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &AIU_PlayerController::HandleSprintStarted);
			EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this, &AIU_PlayerController::HandleSprintCompleted);
		}
		if (ToggleRotationModeAction)
		{
			EnhancedInput->BindAction(ToggleRotationModeAction, ETriggerEvent::Started, this, &AIU_PlayerController::HandleToggleRotationMode);
		}
		if (MeleeAttackAction)
		{
			EnhancedInput->BindAction(MeleeAttackAction, ETriggerEvent::Started, this, &AIU_PlayerController::HandleMeleeAttack);
		}
		if (KiBlastAction)
		{
			EnhancedInput->BindAction(KiBlastAction, ETriggerEvent::Started, this, &AIU_PlayerController::HandleKiBlast);
		}
		if (DodgeAction)
		{
			EnhancedInput->BindAction(DodgeAction, ETriggerEvent::Started, this, &AIU_PlayerController::HandleDodge);
		}
		if (LockOnAction)
		{
			EnhancedInput->BindAction(LockOnAction, ETriggerEvent::Started, this, &AIU_PlayerController::HandleLockOn);
		}
		if (SwitchTargetAction)
		{
			EnhancedInput->BindAction(SwitchTargetAction, ETriggerEvent::Started, this, &AIU_PlayerController::HandleSwitchTarget);
		}
		if (ToggleMobilityStyleAction)
		{
			EnhancedInput->BindAction(ToggleMobilityStyleAction, ETriggerEvent::Started, this, &AIU_PlayerController::HandleToggleMobilityStyle);
		}
	}

	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::F3, IE_Pressed, this, &AIU_PlayerController::HandleToggleDebugHUD);
		InputComponent->BindKey(EKeys::F4, IE_Pressed, this, &AIU_PlayerController::HandleToggleGlobalPvP);
		InputComponent->BindKey(EKeys::F5, IE_Pressed, this, &AIU_PlayerController::HandleToggleAuthPanel);
		InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AIU_PlayerController::HandleTogglePotentialPanel);
		InputComponent->BindKey(EKeys::I, IE_Pressed, this, &AIU_PlayerController::HandleToggleInventoryPanel);
		InputComponent->BindKey(EKeys::B, IE_Pressed, this, &AIU_PlayerController::HandleToggleInventoryPanel);
		InputComponent->BindKey(EKeys::One, IE_Pressed, this, &AIU_PlayerController::HandleQuickUpgradeHP);
		InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AIU_PlayerController::HandleQuickUpgradeKi);
		InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AIU_PlayerController::HandleQuickUpgradeAtk);
		InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &AIU_PlayerController::HandleQuickUseElixir);
	}
}

AIU_PlayerCharacter* AIU_PlayerController::GetIUPlayerCharacter() const
{
	return Cast<AIU_PlayerCharacter>(GetPawn());
}

void AIU_PlayerController::HandleMove(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->ApplyMovementInput(Value.Get<FVector2D>());
	}
}

void AIU_PlayerController::HandleMoveCompleted(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->ApplyMovementInput(FVector2D::ZeroVector);
	}
}

void AIU_PlayerController::HandleLook(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->ApplyLookInput(Value.Get<FVector2D>());
	}
}

void AIU_PlayerController::HandleCameraZoom(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->AdjustCameraZoom(Value.Get<float>());
	}
}

void AIU_PlayerController::HandleJumpStarted(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->StartJumpInput();
	}
}

void AIU_PlayerController::HandleJumpCompleted(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->StopJumpInput();
	}
}

void AIU_PlayerController::HandleDescendStarted(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->StartDescendInput();
	}
}

void AIU_PlayerController::HandleDescendCompleted(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->StopDescendInput();
	}
}

void AIU_PlayerController::HandleFlightToggle(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->ToggleFlightInput();
	}
}

void AIU_PlayerController::HandleSprintStarted(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->StartSprintInput();
	}
}

void AIU_PlayerController::HandleSprintCompleted(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->StopSprintInput();
	}
}

void AIU_PlayerController::HandleToggleRotationMode(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->ToggleRotationModeInput();
	}
}

void AIU_PlayerController::HandleMeleeAttack(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->MeleeAttackInput();
	}
}

void AIU_PlayerController::HandleKiBlast(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->KiBlastInput();
	}
}

void AIU_PlayerController::HandleDodge(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->DodgeInput();
	}
}

void AIU_PlayerController::HandleLockOn(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->ToggleLockOnInput();
	}
}

void AIU_PlayerController::HandleSwitchTarget(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		const float Axis = Value.Get<float>();
		IUChar->SwitchTargetInput(FMath::IsNearlyZero(Axis) ? 1.0f : Axis);
	}
}

void AIU_PlayerController::HandleToggleMobilityStyle(const FInputActionValue& Value)
{
	if (AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter())
	{
		IUChar->ToggleMobilityStyleInput();
	}
}

void AIU_PlayerController::HandleToggleDebugHUD()
{
	if (AIU_HUDManager* IUHUD = Cast<AIU_HUDManager>(GetHUD()))
	{
		IUHUD->ToggleDebugTelemetry();
	}
}

void AIU_PlayerController::HandleToggleGlobalPvP()
{
	Server_ToggleGlobalPvPSparring();
}

void AIU_PlayerController::HandleToggleAuthPanel()
{
	if (!IsLocalController())
	{
		return;
	}
	if (!ActiveAuthWidget)
	{
		ActiveAuthWidget = CreateWidget<UIU_LoginCharacterWidget>(this, UIU_LoginCharacterWidget::StaticClass());
		if (ActiveAuthWidget)
		{
			ActiveAuthWidget->AddToViewport(10);
			ActiveAuthWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (!ActiveAuthWidget)
	{
		return;
	}

	const bool bOpening = (ActiveAuthWidget->GetVisibility() != ESlateVisibility::Visible);
	ActiveAuthWidget->SetVisibility(bOpening ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	bShowMouseCursor = bOpening;
	if (bOpening)
	{
		FInputModeGameAndUI UIInput;
		UIInput.SetWidgetToFocus(ActiveAuthWidget->TakeWidget());
		SetInputMode(UIInput);
	}
	else
	{
		FInputModeGameOnly GameInput;
		SetInputMode(GameInput);
	}
}

void AIU_PlayerController::Server_ToggleGlobalPvPSparring_Implementation()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Security Gate (SEC-01): On a Dedicated Server, unprivileged remote clients cannot mutate global PvP rules
	// unless the server operator explicitly enabled -AllowClientPvPToggle on the server command line.
	if (World->GetNetMode() == NM_DedicatedServer && !FParse::Param(FCommandLine::Get(), TEXT("AllowClientPvPToggle")))
	{
		UE_LOG(LogInfiniteUniverse, Warning,
			TEXT("[Security] Rejected unauthorized Server_ToggleGlobalPvPSparring RPC from remote client '%s'."),
			*GetNameSafe(this));
		return;
	}

	if (AIU_GameState* GS = World->GetGameState<AIU_GameState>())
	{
		GS->SetAllowGlobalPvPSparring(!GS->bAllowGlobalPvPSparring);
	}
}

bool AIU_PlayerController::IsPotentialUpgradePanelVisible() const
{
	return ActivePotentialWidget && ActivePotentialWidget->GetVisibility() == ESlateVisibility::Visible;
}

void AIU_PlayerController::SetPotentialUpgradePanelVisible(bool bVisible, bool bUpdateMouseInputMode)
{
	if (!IsLocalController())
	{
		return;
	}
	if (!ActivePotentialWidget && bVisible)
	{
		ActivePotentialWidget = CreateWidget<UIU_PotentialUpgradeWidget>(this, UIU_PotentialUpgradeWidget::StaticClass());
		if (ActivePotentialWidget)
		{
			ActivePotentialWidget->AddToViewport(8);
			ActivePotentialWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (!ActivePotentialWidget)
	{
		return;
	}

	if (bVisible && IsInventoryPanelVisible())
	{
		SetInventoryPanelVisible(false, false);
	}
	if (bVisible)
	{
		ActivePotentialWidget->RefreshBindings();
	}
	ActivePotentialWidget->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	if (bUpdateMouseInputMode)
	{
		bShowMouseCursor = bVisible;
		if (bVisible)
		{
			FInputModeGameAndUI UIInput;
			UIInput.SetWidgetToFocus(ActivePotentialWidget->TakeWidget());
			UIInput.SetHideCursorDuringCapture(false);
			SetInputMode(UIInput);
		}
		else
		{
			FInputModeGameOnly GameInput;
			SetInputMode(GameInput);
		}
	}
}

void AIU_PlayerController::HandleTogglePotentialPanel()
{
	SetPotentialUpgradePanelVisible(!IsPotentialUpgradePanelVisible(), true);
}

void AIU_PlayerController::HandleQuickUpgradeHP()
{
	Server_RequestAllocatePotential(EIU_PotentialStatType::MaxHP);
}

void AIU_PlayerController::HandleQuickUpgradeKi()
{
	Server_RequestAllocatePotential(EIU_PotentialStatType::MaxKi);
}

void AIU_PlayerController::HandleQuickUpgradeAtk()
{
	Server_RequestAllocatePotential(EIU_PotentialStatType::AttackPower);
}

void AIU_PlayerController::Server_RequestAllocatePotential_Implementation(EIU_PotentialStatType StatType)
{
	FString StatCode;
	switch (StatType)
	{
	case EIU_PotentialStatType::MaxHP:
		StatCode = TEXT("hp");
		break;
	case EIU_PotentialStatType::MaxKi:
		StatCode = TEXT("ki");
		break;
	case EIU_PotentialStatType::AttackPower:
		StatCode = TEXT("atk");
		break;
	default:
		UE_LOG(LogInfiniteUniverse, Warning,
			TEXT("[Security] Rejected invalid EIU_PotentialStatType=%d from '%s'."),
			static_cast<int32>(StatType), *GetNameSafe(this));
		return;
	}

	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	APawn* ControlledPawn = GetPawn();
	if (!IUPS || !ControlledPawn)
	{
		return;
	}

	if (IUPS->HasValidBackendLease())
	{
		UGameInstance* GI = GetGameInstance();
		UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
		if (!Backend)
		{
			return;
		}

		TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
		TWeakObjectPtr<AIU_PlayerState> WeakPS(IUPS);
		Backend->AllocateCharacterPotential(
			IUPS->GetPersistedCharacterUUID(),
			IUPS->GetActiveFencingEpoch(),
			StatCode,
			[WeakPC, WeakPS, StatCode](bool bSuccess, const FIU_BackendCharacterSummary& Updated, const FString& ErrMsg)
			{
				if (!bSuccess)
				{
					UE_LOG(LogInfiniteUniverse, Warning,
						TEXT("[Potential] AllocateCharacterPotential('%s') rejected: %s"),
						*StatCode, *ErrMsg);
					return;
				}

				if (AIU_PlayerState* PS = WeakPS.Get())
				{
					PS->ApplyAuthoritativePotentialUpgrade(Updated);
				}

				if (AIU_PlayerController* PC = WeakPC.Get())
				{
					if (APawn* P = PC->GetPawn())
					{
						if (UIU_AttributeComponent* Attr = P->FindComponentByClass<UIU_AttributeComponent>())
						{
							Attr->ApplyPersistedVitals(
								Updated.MaxHP,
								Updated.HP,
								Updated.MaxEnergy,
								Updated.Energy,
								Updated.AttackPowerMultiplier,
								Updated.AttackPower,
								Updated.Defense);
						}
					}
					PC->Server_RequestRefreshInventory();
				}

				UE_LOG(LogInfiniteUniverse, Display,
					TEXT("[Potential] Character '%s' spent %lld TN on '%s' -> HP %.0f/%.0f, Ki %.0f/%.0f, SucDanh %.0f (x%.2f), RemainingTN=%lld"),
					*Updated.CharacterName,
					Updated.PotentialSpent,
					*StatCode,
					Updated.HP,
					Updated.MaxHP,
					Updated.Energy,
					Updated.MaxEnergy,
					Updated.AttackPower,
					Updated.AttackPowerMultiplier,
					Updated.PotentialPoints);
			});
	}
	else if (!GIsAutomationTesting)
	{
		IUPS->TryOfflineAllocatePotential(StatType, ControlledPawn);
	}
}

namespace
{
	static TAtomic<uint64> GServerEconOpSeq{ 0 };
}

bool AIU_PlayerController::IsInventoryPanelVisible() const
{
	return ActiveInventoryWidget && ActiveInventoryWidget->GetVisibility() == ESlateVisibility::Visible;
}

void AIU_PlayerController::SetInventoryPanelVisible(bool bVisible, bool bUpdateMouseInputMode)
{
	if (!IsLocalController())
	{
		return;
	}
	if (bVisible && IsPotentialUpgradePanelVisible())
	{
		SetPotentialUpgradePanelVisible(false, false);
	}
	if (!ActiveInventoryWidget && bVisible)
	{
		ActiveInventoryWidget = CreateWidget<UIU_InventoryEquipmentWidget>(this, UIU_InventoryEquipmentWidget::StaticClass());
		if (ActiveInventoryWidget)
		{
			ActiveInventoryWidget->AddToViewport(8);
			ActiveInventoryWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (!ActiveInventoryWidget)
	{
		return;
	}

	if (bVisible)
	{
		ActiveInventoryWidget->RefreshBindingsAndRebuild();
		Server_RequestRefreshInventory();
	}
	ActiveInventoryWidget->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	if (bUpdateMouseInputMode)
	{
		bShowMouseCursor = bVisible;
		if (bVisible)
		{
			FInputModeGameAndUI UIInput;
			UIInput.SetWidgetToFocus(ActiveInventoryWidget->TakeWidget());
			UIInput.SetHideCursorDuringCapture(false);
			SetInputMode(UIInput);
		}
		else
		{
			FInputModeGameOnly GameInput;
			SetInputMode(GameInput);
		}
	}
}

void AIU_PlayerController::HandleToggleInventoryPanel()
{
	SetInventoryPanelVisible(!IsInventoryPanelVisible(), true);
}

void AIU_PlayerController::HandleQuickUseElixir()
{
	Server_RequestUseConsumable(TEXT(""));
}

bool AIU_PlayerController::CanUseConsumableOnServer(float& OutRemainingSeconds) const
{
	const UWorld* W = GetWorld();
	const double Now = W ? W->GetTimeSeconds() : FPlatformTime::Seconds();
	const double Elapsed = Now - LastConsumableUseServerTime;
	if (Elapsed < static_cast<double>(ConsumableCooldownSeconds))
	{
		OutRemainingSeconds = static_cast<float>(static_cast<double>(ConsumableCooldownSeconds) - Elapsed);
		return false;
	}
	OutRemainingSeconds = 0.0f;
	return true;
}

void AIU_PlayerController::ApplyAuthoritativeInventorySnapshotOnServer(const FIU_InventorySnapshot& Snapshot)
{
	if (AIU_PlayerState* PS = GetPlayerState<AIU_PlayerState>())
	{
		PS->ApplyAuthoritativeEconomySummary(
			Snapshot.StandardCoin,
			Snapshot.ElixirCount,
			Snapshot.CrimsonOreCount,
			Snapshot.EquippedItems.Num());
	}

	if (APawn* P = GetPawn())
	{
		if (UIU_AttributeComponent* Attr = P->FindComponentByClass<UIU_AttributeComponent>())
		{
			Attr->ApplyPersistedVitals(
				Snapshot.MaxHP,
				Snapshot.HP,
				Snapshot.MaxEnergy,
				Snapshot.Energy,
				Snapshot.AttackPowerMultiplier,
				Snapshot.AttackPower,
				Snapshot.Defense);
		}
	}

	Client_ReceiveInventorySnapshot(Snapshot);
}

void AIU_PlayerController::Client_ReceiveInventorySnapshot_Implementation(const FIU_InventorySnapshot& Snapshot)
{
	CachedInventorySnapshot = Snapshot;
	bHasCachedInventorySnapshot = true;

	if (ActiveInventoryWidget)
	{
		ActiveInventoryWidget->ApplyInventorySnapshot(Snapshot);
	}
	if (!Snapshot.ActionResult.IsEmpty())
	{
		if (AIU_HUDManager* IUHUD = Cast<AIU_HUDManager>(GetHUD()))
		{
			const bool bSuccess = !Snapshot.bHasEnhanceOutcome || Snapshot.bEnhanceSucceeded;
			IUHUD->NotifyInventoryToast(Snapshot.ActionResult, bSuccess);
		}
	}
}

void AIU_PlayerController::Client_NotifyLootDropped_Implementation(
	int64 CoinsGranted,
	const TArray<FString>& DroppedItemLabels,
	bool bBagFullSkipped)
{
	if (AIU_HUDManager* IUHUD = Cast<AIU_HUDManager>(GetHUD()))
	{
		IUHUD->NotifyLootPopup(CoinsGranted, DroppedItemLabels, bBagFullSkipped);
	}
}

void AIU_PlayerController::Client_NotifyInventoryFeedback_Implementation(const FString& Message, bool bIsError)
{
	if (ActiveInventoryWidget)
	{
		ActiveInventoryWidget->SetFeedbackMessage(Message, bIsError);
	}
	if (AIU_HUDManager* IUHUD = Cast<AIU_HUDManager>(GetHUD()))
	{
		IUHUD->NotifyInventoryToast(Message, !bIsError);
	}
}

void AIU_PlayerController::Server_RequestRefreshInventory_Implementation()
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease())
	{
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend)
	{
		return;
	}

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->FetchCharacterInventory(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		[WeakPC](bool bSuccess, const FIU_InventorySnapshot& Snap, const FString&)
		{
			if (bSuccess)
			{
				if (AIU_PlayerController* PC = WeakPC.Get())
				{
					PC->ApplyAuthoritativeInventorySnapshotOnServer(Snap);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestEquipItem_Implementation(const FString& ItemInstanceID)
{
	const FString CleanID = ItemInstanceID.TrimStartAndEnd();
	if (CleanID.IsEmpty())
	{
		return;
	}
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease())
	{
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend)
	{
		return;
	}

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->EquipInventoryItem(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		CleanID,
		[WeakPC](bool bSuccess, const FIU_InventorySnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->ApplyAuthoritativeInventorySnapshotOnServer(Snap);
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestUnequipSlot_Implementation(const FString& SlotID)
{
	const FString CleanSlot = SlotID.TrimStartAndEnd();
	if (CleanSlot.IsEmpty())
	{
		return;
	}
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease())
	{
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend)
	{
		return;
	}

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->UnequipEquipmentSlot(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		CleanSlot,
		[WeakPC](bool bSuccess, const FIU_InventorySnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->ApplyAuthoritativeInventorySnapshotOnServer(Snap);
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestUseConsumable_Implementation(const FString& OptionalItemInstanceID)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	APawn* ControlledPawn = GetPawn();
	if (!IUPS || !ControlledPawn)
	{
		return;
	}

	float RemainingCD = 0.0f;
	if (!CanUseConsumableOnServer(RemainingCD))
	{
		Client_NotifyInventoryFeedback(
			FString::Printf(TEXT("Dau Than dang hoi chieu (con %.1fs)!"), RemainingCD),
			true);
		return;
	}

	const double PrevUseTime = LastConsumableUseServerTime;
	const UWorld* W = GetWorld();
	LastConsumableUseServerTime = W ? W->GetTimeSeconds() : FPlatformTime::Seconds();

	if (IUPS->HasValidBackendLease())
	{
		UGameInstance* GI = GetGameInstance();
		UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
		if (!Backend)
		{
			return;
		}

		TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
		Backend->UseInventoryConsumable(
			IUPS->GetPersistedCharacterUUID(),
			IUPS->GetActiveFencingEpoch(),
			OptionalItemInstanceID.TrimStartAndEnd(),
			[WeakPC, PrevUseTime](bool bSuccess, const FIU_InventorySnapshot& Snap, const FString& ErrMsg)
			{
				if (AIU_PlayerController* PC = WeakPC.Get())
				{
					if (bSuccess)
					{
						PC->ApplyAuthoritativeInventorySnapshotOnServer(Snap);
					}
					else
					{
						PC->LastConsumableUseServerTime = PrevUseTime;
						PC->Client_NotifyInventoryFeedback(ErrMsg, true);
					}
				}
			});
	}
	else if (!GIsAutomationTesting)
	{
		if (IUPS->TryOfflineUseElixir(ControlledPawn))
		{
			Client_NotifyInventoryFeedback(TEXT("Da dung Dau Than (+150 HP & +150 Ki)!"), false);
		}
		else
		{
			LastConsumableUseServerTime = PrevUseTime;
			Client_NotifyInventoryFeedback(TEXT("Khong con Dau Than trong tui do!"), true);
		}
	}
}

void AIU_PlayerController::Server_RequestToggleItemLock_Implementation(const FString& ItemInstanceID, bool bLocked)
{
	const FString CleanID = ItemInstanceID.TrimStartAndEnd();
	if (CleanID.IsEmpty())
	{
		return;
	}
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease())
	{
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend)
	{
		return;
	}

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->ToggleInventoryItemLock(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		CleanID,
		bLocked,
		[WeakPC](bool bSuccess, const FIU_InventorySnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->ApplyAuthoritativeInventorySnapshotOnServer(Snap);
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestSellItem_Implementation(const FString& ItemInstanceID, int32 Quantity)
{
	const FString CleanID = ItemInstanceID.TrimStartAndEnd();
	if (CleanID.IsEmpty() || Quantity <= 0)
	{
		return;
	}
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease())
	{
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend)
	{
		return;
	}

	const FString IdemKey = FString::Printf(
		TEXT("srv_sell_%s_ep%lld_seq%llu"),
		*IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		++GServerEconOpSeq);

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->SellInventoryItem(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		CleanID,
		Quantity,
		IdemKey,
		[WeakPC](bool bSuccess, const FIU_InventorySnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->ApplyAuthoritativeInventorySnapshotOnServer(Snap);
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestEnhanceItem_Implementation(const FString& ItemInstanceID)
{
	const FString CleanID = ItemInstanceID.TrimStartAndEnd();
	if (CleanID.IsEmpty())
	{
		return;
	}
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease())
	{
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend)
	{
		return;
	}

	const FString IdemKey = FString::Printf(
		TEXT("srv_enh_%s_ep%lld_seq%llu"),
		*IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		++GServerEconOpSeq);

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->EnhanceInventoryEquipment(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		CleanID,
		IdemKey,
		[WeakPC](bool bSuccess, const FIU_InventorySnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->ApplyAuthoritativeInventorySnapshotOnServer(Snap);
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}
