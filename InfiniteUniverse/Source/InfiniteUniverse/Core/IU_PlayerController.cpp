// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_PlayerController.h"
#include "Core/IU_GameState.h"
#include "Core/IU_PlayerState.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Network/IU_BackendSubsystem.h"
#include "UI/IU_HUDManager.h"
#include "UI/IU_PlayerHUDWidget.h"
#include "UI/IU_LoginCharacterWidget.h"
#include "UI/IU_PotentialUpgradeWidget.h"
#include "UI/IU_InventoryEquipmentWidget.h"
#include "UI/IU_PlayerTradeWidget.h"
#include "Combat/IU_TargetingComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
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
		// R: Toggle Camera Rotation Mode (Tab is reserved for NRO Character Profile Window!)
		TargetIMC->MapKey(InToggleRot, EKeys::R);
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

	// Ensure Tab is freed from legacy IMC_Player.uasset ToggleRotationMode so Tab opens the NRO Character Profile cleanly!
	if (DefaultMappingContext)
	{
		if (!ToggleRotationModeAction)
		{
			ToggleRotationModeAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_ToggleRotationMode.IA_ToggleRotationMode"));
		}
		if (ToggleRotationModeAction)
		{
			DefaultMappingContext->UnmapKey(ToggleRotationModeAction, EKeys::Tab);
			DefaultMappingContext->UnmapKey(ToggleRotationModeAction, EKeys::R);
			DefaultMappingContext->MapKey(ToggleRotationModeAction, EKeys::R);
		}
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
		InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AIU_PlayerController::HandleToggleCharacterProfileTab);
		InputComponent->BindKey(EKeys::F3, IE_Pressed, this, &AIU_PlayerController::HandleToggleDebugHUD);
		InputComponent->BindKey(EKeys::F4, IE_Pressed, this, &AIU_PlayerController::HandleToggleGlobalPvP);
		InputComponent->BindKey(EKeys::F5, IE_Pressed, this, &AIU_PlayerController::HandleToggleAuthPanel);
		InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AIU_PlayerController::HandleTogglePotentialPanel);
		InputComponent->BindKey(EKeys::I, IE_Pressed, this, &AIU_PlayerController::HandleToggleInventoryPanel);
		InputComponent->BindKey(EKeys::B, IE_Pressed, this, &AIU_PlayerController::HandleToggleInventoryPanel);
		InputComponent->BindKey(EKeys::One, IE_Pressed, this, &AIU_PlayerController::HandleSkillHotbar1);
		InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AIU_PlayerController::HandleSkillHotbar2);
		InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AIU_PlayerController::HandleSkillHotbar3);
		InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &AIU_PlayerController::HandleSkillHotbar4);
		InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &AIU_PlayerController::HandleSkillHotbar5);
		InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &AIU_PlayerController::HandleSkillHotbar6);
		InputComponent->BindKey(EKeys::G, IE_Pressed, this, &AIU_PlayerController::HandleQuickUseElixir);
		InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AIU_PlayerController::HandleChatEnterKey);
		InputComponent->BindKey(EKeys::Y, IE_Pressed, this, &AIU_PlayerController::HandleQuickPartyKey);
		InputComponent->BindKey(EKeys::T, IE_Pressed, this, &AIU_PlayerController::HandleQuickTradeKey);
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
	AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter();
	if (!IUChar)
	{
		return;
	}

	// Step 5 Co-op Utility: Alt + LeftMouseButton places a 6.0s 3D Tactical Party Ping!
	if (IsInputKeyDown(EKeys::LeftAlt) || IsInputKeyDown(EKeys::RightAlt))
	{
		FVector CamOrigin, CamDir;
		IUChar->GetCameraAimRay(CamOrigin, CamDir);
		FVector PingLoc = IUChar->GetActorLocation() + IUChar->GetActorForwardVector() * 450.0f;
		if (UWorld* W = GetWorld())
		{
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(IUTacticalPingTrace), false, IUChar);
			if (W->LineTraceSingleByChannel(Hit, CamOrigin + CamDir * 80.0f, CamOrigin + CamDir * 9000.0f, ECC_Visibility, Params))
			{
				PingLoc = Hit.ImpactPoint;
			}
		}
		Server_RequestPlaceTacticalPing(PingLoc);
		return;
	}

	IUChar->MeleeAttackInput();
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

void AIU_PlayerController::OpenLoginCharacterModal()
{
	HandleToggleAuthPanel();
}

void AIU_PlayerController::HandleToggleCharacterProfileTab()
{
	SetInventoryPanelVisible(!IsInventoryPanelVisible(), true);
}

void AIU_PlayerController::HandleTogglePotentialPanel()
{
	if (IsInventoryPanelVisible() && ActiveInventoryWidget && ActiveInventoryWidget->GetActiveProfileTab() == EIU_CharacterProfileTab::KyNang)
	{
		SetInventoryPanelVisible(false, true);
	}
	else
	{
		OpenCharacterProfileOnTab(EIU_CharacterProfileTab::KyNang, true);
	}
}

void AIU_PlayerController::ExecuteHubSkillSlot(int32 SlotNumber)
{
	const int32 ClampedSlot = FMath::Clamp(SlotNumber, 1, 6);
	if (AIU_HUDManager* IUHUD = Cast<AIU_HUDManager>(GetHUD()))
	{
		if (UIU_PlayerHUDWidget* HUDWidget = IUHUD->GetPlayerHUDWidget())
		{
			HUDWidget->SetActiveSkillSlot(ClampedSlot);
		}
	}

	AIU_PlayerCharacter* IUChar = GetIUPlayerCharacter();
	if (!IUChar)
	{
		return;
	}

	switch (ClampedSlot)
	{
	case 1:
		IUChar->MeleeAttackInput();
		break;
	case 2:
		IUChar->KiBlastInput();
		break;
	case 3:
		IUChar->DodgeInput();
		break;
	case 4:
		IUChar->ToggleMobilityStyleInput();
		break;
	case 5:
		IUChar->ToggleLockOnInput();
		break;
	case 6:
		IUChar->ToggleFlightInput();
		break;
	default:
		break;
	}
}

void AIU_PlayerController::HandleSkillHotbar1() { ExecuteHubSkillSlot(1); }
void AIU_PlayerController::HandleSkillHotbar2() { ExecuteHubSkillSlot(2); }
void AIU_PlayerController::HandleSkillHotbar3() { ExecuteHubSkillSlot(3); }
void AIU_PlayerController::HandleSkillHotbar4() { ExecuteHubSkillSlot(4); }
void AIU_PlayerController::HandleSkillHotbar5() { ExecuteHubSkillSlot(5); }
void AIU_PlayerController::HandleSkillHotbar6() { ExecuteHubSkillSlot(6); }

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

void AIU_PlayerController::OpenCharacterProfileOnTab(EIU_CharacterProfileTab TargetTab, bool bUpdateMouseInputMode)
{
	SetInventoryPanelVisible(true, bUpdateMouseInputMode);
	if (ActiveInventoryWidget)
	{
		ActiveInventoryWidget->SetActiveProfileTab(TargetTab);
	}
}

void AIU_PlayerController::HandleToggleInventoryPanel()
{
	if (IsInventoryPanelVisible() && ActiveInventoryWidget && ActiveInventoryWidget->GetActiveProfileTab() == EIU_CharacterProfileTab::HanhTrang)
	{
		SetInventoryPanelVisible(false, true);
	}
	else
	{
		OpenCharacterProfileOnTab(EIU_CharacterProfileTab::HanhTrang, true);
	}
}

void AIU_PlayerController::TriggerQuickUseElixirFromHUD()
{
	if (AIU_HUDManager* IUHUD = Cast<AIU_HUDManager>(GetHUD()))
	{
		if (UIU_PlayerHUDWidget* HUDWidget = IUHUD->GetPlayerHUDWidget())
		{
			if (HUDWidget->GetDisplayedElixirCount() > 0)
			{
				HUDWidget->NotifyElixirCooldownStarted(ConsumableCooldownSeconds);
			}
		}
	}
	Server_RequestUseConsumable(TEXT(""));
}

void AIU_PlayerController::HandleQuickUseElixir()
{
	TriggerQuickUseElixirFromHUD();
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
	if (ActiveTradeWidget)
	{
		ActiveTradeWidget->RefreshFromCachedInventory();
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

// ============================================================================
// Step 5 Party, Guild, Multi-Channel Chat, Tactical Ping & P2P Trade Implementation
// ============================================================================

bool AIU_PlayerController::IsPlayerTradePanelVisible() const
{
	return ActiveTradeWidget && ActiveTradeWidget->GetVisibility() == ESlateVisibility::Visible;
}

void AIU_PlayerController::SetPlayerTradePanelVisible(bool bVisible, bool bUpdateMouseInputMode)
{
	if (!IsLocalController())
	{
		return;
	}
	if (bVisible && IsInventoryPanelVisible())
	{
		SetInventoryPanelVisible(false, false);
	}
	if (!ActiveTradeWidget && bVisible)
	{
		ActiveTradeWidget = CreateWidget<UIU_PlayerTradeWidget>(this, UIU_PlayerTradeWidget::StaticClass());
		if (ActiveTradeWidget)
		{
			ActiveTradeWidget->AddToViewport(12);
			ActiveTradeWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (!ActiveTradeWidget)
	{
		return;
	}

	if (bVisible)
	{
		Server_RequestRefreshInventory();
		if (bHasCachedTradeSnapshot)
		{
			ActiveTradeWidget->ApplyTradeSnapshot(CachedTradeSnapshot);
		}
	}
	ActiveTradeWidget->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	if (bUpdateMouseInputMode)
	{
		bShowMouseCursor = bVisible;
		if (bVisible)
		{
			FInputModeGameAndUI UIInput;
			UIInput.SetWidgetToFocus(ActiveTradeWidget->TakeWidget());
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

AIU_PlayerCharacter* AIU_PlayerController::FindClosestOtherPlayerCharacter(float MaxDistanceCm) const
{
	const AIU_PlayerCharacter* SelfChar = GetIUPlayerCharacter();
	const UWorld* W = GetWorld();
	if (!SelfChar || !W)
	{
		return nullptr;
	}

	if (const UIU_TargetingComponent* TC = SelfChar->GetTargetingComponent())
	{
		if (AIU_PlayerCharacter* LockedP = Cast<AIU_PlayerCharacter>(TC->GetLockedTarget()))
		{
			if (IsValid(LockedP) && LockedP != SelfChar &&
				FVector::Dist(SelfChar->GetActorLocation(), LockedP->GetActorLocation()) <= MaxDistanceCm)
			{
				return LockedP;
			}
		}
	}

	AIU_PlayerCharacter* Best = nullptr;
	float BestDist = MaxDistanceCm;
	for (TActorIterator<AIU_PlayerCharacter> It(W); It; ++It)
	{
		AIU_PlayerCharacter* Cand = *It;
		if (!IsValid(Cand) || Cand == SelfChar)
		{
			continue;
		}
		const float D = FVector::Dist(SelfChar->GetActorLocation(), Cand->GetActorLocation());
		if (D <= BestDist)
		{
			BestDist = D;
			Best = Cand;
		}
	}
	return Best;
}

void AIU_PlayerController::HandleChatEnterKey()
{
	if (AIU_HUDManager* IUHUD = Cast<AIU_HUDManager>(GetHUD()))
	{
		if (UIU_PlayerHUDWidget* HUDWidget = IUHUD->GetPlayerHUDWidget())
		{
			HUDWidget->FocusOrSubmitChatBar();
		}
	}
}

void AIU_PlayerController::HandleQuickPartyKey()
{
	if (bHasCachedPartySnapshot && CachedPartySnapshot.PendingInvites.Num() > 0)
	{
		Server_RequestPartyRespond(CachedPartySnapshot.PendingInvites[0].InviteID, true);
		return;
	}
	FString TargetUUID;
	if (const AIU_PlayerCharacter* Ally = FindClosestOtherPlayerCharacter(2000.0f))
	{
		if (const AIU_PlayerState* AllyPS = Ally->GetPlayerState<AIU_PlayerState>())
		{
			TargetUUID = AllyPS->GetPersistedCharacterUUID();
		}
	}
	Server_RequestPartyInvite(TargetUUID);
}

void AIU_PlayerController::HandleQuickTradeKey()
{
	const AIU_PlayerState* MyPS = GetPlayerState<AIU_PlayerState>();
	const FString MyUUID = MyPS ? MyPS->GetPersistedCharacterUUID() : TEXT("");

	if (bHasCachedTradeSnapshot && CachedTradeSnapshot.bHasActiveTrade)
	{
		if (CachedTradeSnapshot.State == TEXT("PENDING_INVITE") && CachedTradeSnapshot.RecipientCharID == MyUUID)
		{
			Server_RequestTradeRespond(CachedTradeSnapshot.TradeID, true);
			return;
		}
		SetPlayerTradePanelVisible(!IsPlayerTradePanelVisible(), true);
		return;
	}

	FString TargetUUID;
	if (const AIU_PlayerCharacter* Ally = FindClosestOtherPlayerCharacter(600.0f))
	{
		if (const AIU_PlayerState* AllyPS = Ally->GetPlayerState<AIU_PlayerState>())
		{
			TargetUUID = AllyPS->GetPersistedCharacterUUID();
		}
	}
	Server_RequestTradeWithCharacter(TargetUUID);
}

void AIU_PlayerController::SyncAllWorldControllersPartyStateOnServer()
{
	UWorld* W = GetWorld();
	if (!W)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
	{
		if (AIU_PlayerController* OtherPC = Cast<AIU_PlayerController>(It->Get()))
		{
			OtherPC->Server_RequestRefreshParty();
		}
	}
}

void AIU_PlayerController::SyncPartnerTradeStateOnServer(const FString& CharIDA, const FString& CharIDB, bool bRefreshInventories)
{
	UWorld* W = GetWorld();
	if (!W)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
	{
		AIU_PlayerController* OtherPC = Cast<AIU_PlayerController>(It->Get());
		if (!OtherPC) { continue; }
		const AIU_PlayerState* PS = OtherPC->GetPlayerState<AIU_PlayerState>();
		if (!PS) { continue; }
		const FString CID = PS->GetPersistedCharacterUUID();
		if (!CID.IsEmpty() && (CID == CharIDA || CID == CharIDB))
		{
			OtherPC->Server_RequestRefreshTrade();
			if (bRefreshInventories)
			{
				OtherPC->Server_RequestRefreshInventory();
			}
		}
	}
}

void AIU_PlayerController::Client_ReceivePartySnapshot_Implementation(const FIU_PartySnapshot& Snapshot)
{
	CachedPartySnapshot = Snapshot;
	bHasCachedPartySnapshot = true;

	if (AIU_HUDManager* IUHUD = Cast<AIU_HUDManager>(GetHUD()))
	{
		if (UIU_PlayerHUDWidget* HUDWidget = IUHUD->GetPlayerHUDWidget())
		{
			HUDWidget->ApplyPartySnapshot(Snapshot);
		}
		if (!Snapshot.ActionMessage.IsEmpty())
		{
			IUHUD->NotifyInventoryToast(Snapshot.ActionMessage, true);
		}
	}
}

void AIU_PlayerController::Server_RequestRefreshParty_Implementation()
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

	float HP = 200.0f, MaxHP = 200.0f, Ki = 200.0f, MaxKi = 200.0f;
	if (const APawn* P = GetPawn())
	{
		if (const UIU_AttributeComponent* Attr = P->FindComponentByClass<UIU_AttributeComponent>())
		{
			HP = Attr->GetHP();
			MaxHP = Attr->GetMaxHP();
			Ki = Attr->GetKi();
			MaxKi = Attr->GetMaxKi();
		}
	}

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	TWeakObjectPtr<AIU_PlayerState> WeakPS(IUPS);
	const FString MyCharID = IUPS->GetPersistedCharacterUUID();

	Backend->FetchPartyState(
		MyCharID,
		IUPS->GetPersistedCharacterName(),
		IUPS->GetActiveFencingEpoch(),
		IUPS->GetCharacterLevel(),
		HP,
		MaxHP,
		Ki,
		MaxKi,
		[WeakPC, WeakPS, MyCharID](bool bSuccess, const FIU_PartySnapshot& Snap, const FString&)
		{
			if (!bSuccess) { return; }
			if (AIU_PlayerState* PS = WeakPS.Get())
			{
				PS->ApplyAuthoritativePartyIdentity(
					Snap.bInParty ? Snap.PartyID : TEXT(""),
					Snap.bInParty && Snap.LeaderCharID == MyCharID);
			}
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				PC->Client_ReceivePartySnapshot(Snap);
			}
		});
}

void AIU_PlayerController::Server_RequestPartyInvite_Implementation(const FString& TargetCharUUID)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease())
	{
		return;
	}
	FString ResolvedTargetID = TargetCharUUID.TrimStartAndEnd();
	FString ResolvedTargetName;
	if (ResolvedTargetID.IsEmpty())
	{
		if (const AIU_PlayerCharacter* Ally = FindClosestOtherPlayerCharacter(2500.0f))
		{
			if (const AIU_PlayerState* AllyPS = Ally->GetPlayerState<AIU_PlayerState>())
			{
				ResolvedTargetID = AllyPS->GetPersistedCharacterUUID();
				ResolvedTargetName = AllyPS->GetPersistedCharacterName();
			}
		}
	}
	if (ResolvedTargetID.IsEmpty())
	{
		Client_NotifyInventoryFeedback(TEXT("Khong tim thay nguoi choi nao o gan de moi vao Party!"), true);
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->InviteToParty(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetPersistedCharacterName(),
		IUPS->GetCharacterLevel(),
		IUPS->GetActiveFencingEpoch(),
		ResolvedTargetID,
		ResolvedTargetName,
		[WeakPC](bool bSuccess, const FIU_PartySnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->Client_ReceivePartySnapshot(Snap);
					PC->SyncAllWorldControllersPartyStateOnServer();
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestPartyRespond_Implementation(const FString& InviteID, bool bAccept)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->RespondPartyInvite(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetPersistedCharacterName(),
		IUPS->GetCharacterLevel(),
		IUPS->GetActiveFencingEpoch(),
		InviteID.TrimStartAndEnd(),
		bAccept,
		[WeakPC](bool bSuccess, const FIU_PartySnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->Client_ReceivePartySnapshot(Snap);
					PC->SyncAllWorldControllersPartyStateOnServer();
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestPartyLeave_Implementation()
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->LeaveParty(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		[WeakPC](bool bSuccess, const FIU_PartySnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->Client_ReceivePartySnapshot(Snap);
					PC->SyncAllWorldControllersPartyStateOnServer();
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestPartyKick_Implementation(const FString& TargetCharUUID)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->KickPartyMember(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		TargetCharUUID.TrimStartAndEnd(),
		[WeakPC](bool bSuccess, const FIU_PartySnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->Client_ReceivePartySnapshot(Snap);
					PC->SyncAllWorldControllersPartyStateOnServer();
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestPartyPromote_Implementation(const FString& TargetCharUUID)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->PromotePartyLeader(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		TargetCharUUID.TrimStartAndEnd(),
		[WeakPC](bool bSuccess, const FIU_PartySnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->Client_ReceivePartySnapshot(Snap);
					PC->SyncAllWorldControllersPartyStateOnServer();
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestPlaceTacticalPing_Implementation(FVector WorldLocation)
{
	if (WorldLocation.ContainsNaN())
	{
		return;
	}
	UWorld* W = GetWorld();
	AIU_GameState* GS = W ? W->GetGameState<AIU_GameState>() : nullptr;
	const AIU_PlayerState* PS = GetPlayerState<AIU_PlayerState>();
	if (!GS || !PS)
	{
		return;
	}
	FString PingPartyKey = PS->GetPartyID();
	if (PingPartyKey.IsEmpty())
	{
		PingPartyKey = FString::Printf(TEXT("SOLO:%s"), *PS->GetPersistedCharacterUUID());
	}
	const FString Sender = PS->GetPersistedCharacterName().IsEmpty() ? TEXT("Warrior") : PS->GetPersistedCharacterName();
	GS->BroadcastPartyTacticalPing(PingPartyKey, Sender, WorldLocation, 6.0f);
}

void AIU_PlayerController::Client_ReceiveGuildSnapshot_Implementation(const FIU_GuildSnapshot& Snapshot)
{
	CachedGuildSnapshot = Snapshot;
	bHasCachedGuildSnapshot = true;

	if (ActiveInventoryWidget)
	{
		ActiveInventoryWidget->ApplyGuildSnapshot(Snapshot);
	}
	if (!Snapshot.ActionMessage.IsEmpty())
	{
		if (AIU_HUDManager* IUHUD = Cast<AIU_HUDManager>(GetHUD()))
		{
			IUHUD->NotifyInventoryToast(Snapshot.ActionMessage, true);
		}
	}
}

void AIU_PlayerController::Server_RequestRefreshGuild_Implementation()
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	TWeakObjectPtr<AIU_PlayerState> WeakPS(IUPS);
	Backend->FetchGuildSnapshot(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		[WeakPC, WeakPS](bool bSuccess, const FIU_GuildSnapshot& Snap, const FString&)
		{
			if (!bSuccess) { return; }
			if (AIU_PlayerState* PS = WeakPS.Get())
			{
				PS->ApplyAuthoritativeGuildIdentity(
					Snap.bInGuild ? Snap.GuildID : TEXT(""),
					Snap.bInGuild ? Snap.GuildName : TEXT(""),
					Snap.bInGuild ? Snap.GuildTag : TEXT(""),
					Snap.bInGuild ? Snap.MyRole : TEXT(""));
			}
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				PC->Client_ReceiveGuildSnapshot(Snap);
			}
		});
}

void AIU_PlayerController::Server_RequestCreateGuild_Implementation(const FString& GuildName, const FString& GuildTag, const FString& MOTD)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	const FString IdemKey = FString::Printf(
		TEXT("srv_gcreate_%s_ep%lld_seq%llu"),
		*IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		++GServerEconOpSeq);

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	TWeakObjectPtr<AIU_PlayerState> WeakPS(IUPS);
	Backend->CreateGuild(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		GuildName.TrimStartAndEnd(),
		GuildTag.TrimStartAndEnd().ToUpper(),
		MOTD.TrimStartAndEnd(),
		IdemKey,
		[WeakPC, WeakPS](bool bSuccess, const FIU_GuildSnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					if (AIU_PlayerState* PS = WeakPS.Get())
					{
						PS->ApplyAuthoritativeGuildIdentity(Snap.GuildID, Snap.GuildName, Snap.GuildTag, Snap.MyRole);
					}
					PC->Client_ReceiveGuildSnapshot(Snap);
					PC->Server_RequestRefreshInventory();
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestJoinGuild_Implementation(const FString& GuildID)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	TWeakObjectPtr<AIU_PlayerState> WeakPS(IUPS);
	Backend->JoinGuild(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		GuildID.TrimStartAndEnd(),
		[WeakPC, WeakPS](bool bSuccess, const FIU_GuildSnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					if (AIU_PlayerState* PS = WeakPS.Get())
					{
						PS->ApplyAuthoritativeGuildIdentity(Snap.GuildID, Snap.GuildName, Snap.GuildTag, Snap.MyRole);
					}
					PC->Client_ReceiveGuildSnapshot(Snap);
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestLeaveGuild_Implementation()
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	TWeakObjectPtr<AIU_PlayerState> WeakPS(IUPS);
	Backend->LeaveGuild(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		[WeakPC, WeakPS](bool bSuccess, const FIU_GuildSnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					if (AIU_PlayerState* PS = WeakPS.Get())
					{
						PS->ApplyAuthoritativeGuildIdentity(TEXT(""), TEXT(""), TEXT(""), TEXT(""));
					}
					PC->Client_ReceiveGuildSnapshot(Snap);
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestDonateGuildCoins_Implementation(int64 AmountCoin)
{
	if (AmountCoin <= 0) { return; }
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	const FString IdemKey = FString::Printf(
		TEXT("srv_gdonate_%s_ep%lld_seq%llu"),
		*IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		++GServerEconOpSeq);

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->DonateGuildCoins(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		AmountCoin,
		IdemKey,
		[WeakPC](bool bSuccess, const FIU_GuildSnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->Client_ReceiveGuildSnapshot(Snap);
					PC->Server_RequestRefreshInventory();
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestUpdateGuildMOTD_Implementation(const FString& NewMOTD)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->UpdateGuildMOTD(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		NewMOTD.TrimStartAndEnd(),
		[WeakPC](bool bSuccess, const FIU_GuildSnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess) { PC->Client_ReceiveGuildSnapshot(Snap); }
				else { PC->Client_NotifyInventoryFeedback(ErrMsg, true); }
			}
		});
}

void AIU_PlayerController::Server_RequestKickGuildMember_Implementation(const FString& TargetCharUUID)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->KickGuildMember(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		TargetCharUUID.TrimStartAndEnd(),
		[WeakPC](bool bSuccess, const FIU_GuildSnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess) { PC->Client_ReceiveGuildSnapshot(Snap); }
				else { PC->Client_NotifyInventoryFeedback(ErrMsg, true); }
			}
		});
}

void AIU_PlayerController::Server_RequestSetGuildMemberRole_Implementation(const FString& TargetCharUUID, const FString& NewRole)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->SetGuildMemberRole(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		TargetCharUUID.TrimStartAndEnd(),
		NewRole.TrimStartAndEnd(),
		[WeakPC](bool bSuccess, const FIU_GuildSnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess) { PC->Client_ReceiveGuildSnapshot(Snap); }
				else { PC->Client_NotifyInventoryFeedback(ErrMsg, true); }
			}
		});
}

void AIU_PlayerController::Client_ReceiveChatMessage_Implementation(const FIU_ChatMessageView& Message)
{
	if (AIU_HUDManager* IUHUD = Cast<AIU_HUDManager>(GetHUD()))
	{
		if (UIU_PlayerHUDWidget* HUDWidget = IUHUD->GetPlayerHUDWidget())
		{
			HUDWidget->AppendChatMessage(Message);
		}
	}
}

void AIU_PlayerController::Server_RequestSendChat_Implementation(
	const FString& Channel,
	const FString& Content,
	const FString& TargetName)
{
	const FString Trimmed = Content.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return;
	}
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	APawn* SenderPawn = GetPawn();
	if (!IUPS || !SenderPawn)
	{
		return;
	}
	const FVector Pos = SenderPawn->GetActorLocation();

	if (!IUPS->HasValidBackendLease())
	{
		// Offline/PIE fallback: display overhead speech bubble & echo locally
		const FString Clean = UIU_BackendSubsystem::SanitizeVietnameseForSlateASCII(Trimmed).Left(120);
		IUPS->SetOverheadSpeechBubble(Clean, TEXT("LOCAL"), 4.5f);
		FIU_ChatMessageView LocalMsg;
		LocalMsg.Channel = TEXT("LOCAL");
		LocalMsg.SenderName = IUPS->GetPersistedCharacterName().IsEmpty() ? TEXT("Warrior") : IUPS->GetPersistedCharacterName();
		LocalMsg.Content = Clean;
		LocalMsg.bShowBubble = true;
		Client_ReceiveChatMessage(LocalMsg);
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	TWeakObjectPtr<AIU_PlayerState> WeakPS(IUPS);
	Backend->SendSocialChatMessage(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetPersistedCharacterName(),
		IUPS->GetActiveFencingEpoch(),
		Channel.TrimStartAndEnd(),
		Trimmed,
		Pos,
		TargetName.TrimStartAndEnd(),
		[WeakPC, WeakPS](bool bSuccess, const FIU_ChatMessageView& Msg, const FString& ErrMsg)
		{
			AIU_PlayerController* SenderPC = WeakPC.Get();
			if (!SenderPC) { return; }
			if (!bSuccess)
			{
				SenderPC->Client_NotifyInventoryFeedback(ErrMsg, true);
				return;
			}

			if (Msg.bShowBubble)
			{
				if (AIU_PlayerState* PS = WeakPS.Get())
				{
					PS->SetOverheadSpeechBubble(Msg.Content, Msg.Channel, 4.5f);
				}
			}

			UWorld* W = SenderPC->GetWorld();
			if (!W) { return; }
			for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
			{
				AIU_PlayerController* ViewerPC = Cast<AIU_PlayerController>(It->Get());
				if (!ViewerPC) { continue; }
				const AIU_PlayerState* ViewerPS = ViewerPC->GetPlayerState<AIU_PlayerState>();
				const APawn* ViewerPawn = ViewerPC->GetPawn();
				if (!ViewerPS) { continue; }

				bool bDeliver = false;
				if (Msg.Channel == TEXT("ZONE") || Msg.Channel == TEXT("SYSTEM"))
				{
					bDeliver = true;
				}
				else if (Msg.Channel == TEXT("LOCAL"))
				{
					bDeliver = (ViewerPS->GetPersistedCharacterUUID() == Msg.SenderCharID) ||
						(ViewerPawn && FVector::DistSquared(ViewerPawn->GetActorLocation(), Msg.SenderPosition) <= FMath::Square(6000.0));
				}
				else if (Msg.Channel == TEXT("PARTY"))
				{
					bDeliver = !Msg.PartyID.IsEmpty() && ViewerPS->GetPartyID() == Msg.PartyID;
				}
				else if (Msg.Channel == TEXT("GUILD"))
				{
					bDeliver = !Msg.GuildID.IsEmpty() && ViewerPS->GetGuildID() == Msg.GuildID;
				}
				else if (Msg.Channel == TEXT("WHISPER"))
				{
					bDeliver = (ViewerPS->GetPersistedCharacterUUID() == Msg.SenderCharID) ||
						ViewerPS->GetPersistedCharacterName().Equals(Msg.TargetName, ESearchCase::IgnoreCase);
				}

				if (bDeliver)
				{
					ViewerPC->Client_ReceiveChatMessage(Msg);
				}
			}
		});
}

void AIU_PlayerController::Client_ReceiveTradeSnapshot_Implementation(const FIU_TradeSessionSnapshot& Snapshot)
{
	CachedTradeSnapshot = Snapshot;
	bHasCachedTradeSnapshot = true;

	if (AIU_HUDManager* IUHUD = Cast<AIU_HUDManager>(GetHUD()))
	{
		if (UIU_PlayerHUDWidget* HUDWidget = IUHUD->GetPlayerHUDWidget())
		{
			HUDWidget->ApplyTradeInviteBanner(Snapshot);
		}
		if (!Snapshot.ActionMessage.IsEmpty())
		{
			IUHUD->NotifyInventoryToast(Snapshot.ActionMessage, Snapshot.State != TEXT("ABORTED_CONFLICT"));
		}
	}

	if (Snapshot.State == TEXT("OPEN") || Snapshot.State == TEXT("BOTH_LOCKED"))
	{
		if (!IsPlayerTradePanelVisible())
		{
			SetPlayerTradePanelVisible(true, !GIsAutomationTesting);
		}
		if (ActiveTradeWidget)
		{
			ActiveTradeWidget->ApplyTradeSnapshot(Snapshot);
		}
	}
	else if (Snapshot.State == TEXT("SETTLED") || Snapshot.State == TEXT("CANCELLED") || Snapshot.State == TEXT("ABORTED_CONFLICT"))
	{
		if (ActiveTradeWidget)
		{
			ActiveTradeWidget->ApplyTradeSnapshot(Snapshot);
		}
		if (IsPlayerTradePanelVisible())
		{
			SetPlayerTradePanelVisible(false, !GIsAutomationTesting);
		}
	}
}

void AIU_PlayerController::Server_RequestRefreshTrade_Implementation()
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->FetchTradeSession(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		[WeakPC](bool bSuccess, const FIU_TradeSessionSnapshot& Snap, const FString&)
		{
			if (bSuccess)
			{
				if (AIU_PlayerController* PC = WeakPC.Get())
				{
					PC->Client_ReceiveTradeSnapshot(Snap);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestTradeWithCharacter_Implementation(const FString& OptionalTargetCharUUID)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	AIU_PlayerCharacter* MyChar = GetIUPlayerCharacter();
	if (!IUPS || !IUPS->HasValidBackendLease() || !MyChar || !MyChar->GetAttributeComponent() || MyChar->GetAttributeComponent()->IsDead())
	{
		return;
	}

	// Enforce <= 600 cm (6m) proximity & living target check on Server Authority!
	AIU_PlayerCharacter* TargetChar = nullptr;
	FString ResolvedTargetID = OptionalTargetCharUUID.TrimStartAndEnd();
	if (!ResolvedTargetID.IsEmpty())
	{
		if (UWorld* W = GetWorld())
		{
			for (TActorIterator<AIU_PlayerCharacter> It(W); It; ++It)
			{
				AIU_PlayerCharacter* Cand = *It;
				if (!IsValid(Cand) || Cand == MyChar) { continue; }
				if (const AIU_PlayerState* CandPS = Cand->GetPlayerState<AIU_PlayerState>())
				{
					if (CandPS->GetPersistedCharacterUUID() == ResolvedTargetID)
					{
						TargetChar = Cand;
						break;
					}
				}
			}
		}
	}
	else
	{
		TargetChar = FindClosestOtherPlayerCharacter(600.0f);
		if (TargetChar)
		{
			if (const AIU_PlayerState* CandPS = TargetChar->GetPlayerState<AIU_PlayerState>())
			{
				ResolvedTargetID = CandPS->GetPersistedCharacterUUID();
			}
		}
	}

	if (!TargetChar || ResolvedTargetID.IsEmpty() ||
		FVector::Dist(MyChar->GetActorLocation(), TargetChar->GetActorLocation()) > 600.0f ||
		(TargetChar->GetAttributeComponent() && TargetChar->GetAttributeComponent()->IsDead()))
	{
		Client_NotifyInventoryFeedback(TEXT("Phai dung gan nguoi choi khac (<= 6m) de giao dich P2P!"), true);
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->RequestPlayerTrade(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		ResolvedTargetID,
		[WeakPC](bool bSuccess, const FIU_TradeSessionSnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->Client_ReceiveTradeSnapshot(Snap);
					PC->SyncPartnerTradeStateOnServer(Snap.InitiatorCharID, Snap.RecipientCharID, false);
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestTradeRespond_Implementation(const FString& TradeID, bool bAccept)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->RespondPlayerTrade(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		TradeID.TrimStartAndEnd(),
		bAccept,
		[WeakPC](bool bSuccess, const FIU_TradeSessionSnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->Client_ReceiveTradeSnapshot(Snap);
					PC->SyncPartnerTradeStateOnServer(Snap.InitiatorCharID, Snap.RecipientCharID, false);
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestTradeUpdateOffer_Implementation(int64 CoinOffer, const TArray<FString>& ItemInstanceIDs)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->UpdateTradeOffer(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		TEXT(""),
		CoinOffer,
		ItemInstanceIDs,
		[WeakPC](bool bSuccess, const FIU_TradeSessionSnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->Client_ReceiveTradeSnapshot(Snap);
					PC->SyncPartnerTradeStateOnServer(Snap.InitiatorCharID, Snap.RecipientCharID, false);
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestTradeLock_Implementation()
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->LockTradeOffer(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		TEXT(""),
		[WeakPC](bool bSuccess, const FIU_TradeSessionSnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->Client_ReceiveTradeSnapshot(Snap);
					PC->SyncPartnerTradeStateOnServer(Snap.InitiatorCharID, Snap.RecipientCharID, false);
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestTradeConfirm_Implementation()
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->ConfirmPlayerTrade(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		TEXT(""),
		[WeakPC](bool bSuccess, const FIU_TradeSessionSnapshot& TradeSnap, const FIU_InventorySnapshot& InvSnap, bool bHasInv, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					if (bHasInv)
					{
						PC->ApplyAuthoritativeInventorySnapshotOnServer(InvSnap);
					}
					PC->Client_ReceiveTradeSnapshot(TradeSnap);
					const bool bSettled = (TradeSnap.State == TEXT("SETTLED"));
					PC->SyncPartnerTradeStateOnServer(TradeSnap.InitiatorCharID, TradeSnap.RecipientCharID, bSettled);
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestTradeCancel_Implementation()
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->CancelPlayerTrade(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		TEXT(""),
		[WeakPC](bool bSuccess, const FIU_TradeSessionSnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess)
				{
					PC->Client_ReceiveTradeSnapshot(Snap);
					PC->SyncPartnerTradeStateOnServer(Snap.InitiatorCharID, Snap.RecipientCharID, false);
				}
				else
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				}
			}
		});
}

// ============================================================================
// Step 6 Multi-Zone / Channel Handoff, Quest Turn-In & Boss Pity Shop RPCs
// ============================================================================

void AIU_PlayerController::Client_ReceiveWorldChannels_Implementation(const TArray<FIU_ZoneChannelView>& Channels)
{
	CachedWorldChannels = Channels;
	bHasCachedWorldChannels = true;
	if (ActiveInventoryWidget)
	{
		ActiveInventoryWidget->ApplyWorldChannels(Channels);
	}
}

void AIU_PlayerController::Server_RequestRefreshChannels_Implementation()
{
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend || !Backend->IsBackendBridgeEnabled())
	{
		return;
	}

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->FetchWorldChannels(
		[WeakPC](bool bSuccess, const TArray<FIU_ZoneChannelView>& Channels, const FString&)
		{
			if (bSuccess)
			{
				if (AIU_PlayerController* PC = WeakPC.Get())
				{
					PC->Client_ReceiveWorldChannels(Channels);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestZoneTransfer_Implementation(const FString& TargetZoneID, int32 TargetChannelID)
{
	if (bZoneTransferInFlight)
	{
		return;
	}
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	APawn* ControlledPawn = GetPawn();
	if (!IUPS || !IUPS->HasValidBackendLease() || !ControlledPawn)
	{
		return;
	}
	const UIU_AttributeComponent* Attr = ControlledPawn->FindComponentByClass<UIU_AttributeComponent>();
	if (Attr && Attr->IsDead())
	{
		Client_NotifyInventoryFeedback(TEXT("Nhan vat dang kiet suc, khong the doi khu / chuyen vung!"), true);
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend || !Backend->IsBackendBridgeEnabled())
	{
		return;
	}

	const FString CleanTargetZone = TargetZoneID.TrimStartAndEnd().IsEmpty()
		? Backend->GetServerZoneID()
		: TargetZoneID.TrimStartAndEnd();
	const int32 CleanTargetChannel = FMath::Max(1, TargetChannelID);

	if (CleanTargetZone == Backend->GetServerZoneID() && CleanTargetChannel == Backend->GetServerChannelID())
	{
		Client_NotifyInventoryFeedback(FString::Printf(TEXT("Ban dang o ngay tai Khu %d cua vung nay!"), CleanTargetChannel), false);
		return;
	}

	FIU_CharacterSavePayload Cp;
	Cp.CharacterID = IUPS->GetPersistedCharacterUUID();
	Cp.FencingEpoch = IUPS->GetActiveFencingEpoch();
	Cp.ServerID = Backend->GetServerInstanceID();
	Cp.SnapshotReason = TEXT("ZONE_TRANSFER");
	Cp.ZoneID = Backend->GetServerZoneID();
	Cp.ChannelID = Backend->GetServerChannelID();
	Cp.Position = ControlledPawn->GetActorLocation();
	Cp.Yaw = ControlledPawn->GetActorRotation().Yaw;
	Cp.bIsAlive = true;
	Cp.HP = Attr ? Attr->GetHP() : 200.0f;
	Cp.Energy = Attr ? Attr->GetKi() : 200.0f;
	Cp.EnemyKills = IUPS->GetEnemyKills();
	Cp.PvPKills = IUPS->GetPvPKills();
	Cp.Deaths = IUPS->GetDeaths();

	bZoneTransferInFlight = true;
	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	TWeakObjectPtr<AIU_PlayerState> WeakPS(IUPS);

	Backend->RequestZoneTransfer(
		Cp,
		CleanTargetZone,
		CleanTargetChannel,
		[WeakPC, WeakPS, CleanTargetZone, CleanTargetChannel](bool bSuccess, const FString& ServerAddr, const FString& TicketID, const FString& ErrMsg)
		{
			AIU_PlayerController* PC = WeakPC.Get();
			if (!PC)
			{
				return;
			}
			if (!bSuccess)
			{
				PC->bZoneTransferInFlight = false;
				PC->Client_NotifyInventoryFeedback(ErrMsg, true);
				return;
			}

			// CRITICAL ANTI-DUPE HANDOFF: Mark local lease revoked immediately on Source Server so GameMode::Logout
			// NEVER overwrites the ZONE_TRANSFER snapshot or destination Epoch #(N+1)!
			if (AIU_PlayerState* PS = WeakPS.Get())
			{
				PS->MarkLeaseRevoked();
			}

			PC->Client_ExecuteZoneTransfer(ServerAddr, TicketID, CleanTargetZone, CleanTargetChannel);
		});
}

void AIU_PlayerController::Client_ExecuteZoneTransfer_Implementation(
	const FString& ServerAddr,
	const FString& TicketID,
	const FString& TargetZoneID,
	int32 TargetChannelID)
{
	if (IsInventoryPanelVisible())
	{
		SetInventoryPanelVisible(false, false);
	}
	if (IsPlayerTradePanelVisible())
	{
		SetPlayerTradePanelVisible(false, false);
	}
	if (AIU_HUDManager* IUHUD = Cast<AIU_HUDManager>(GetHUD()))
	{
		IUHUD->NotifyInventoryToast(
			FString::Printf(TEXT("Dang bay Tau Vu Tru sang %s (Khu %d)..."), *TargetZoneID, TargetChannelID),
			true);
	}

	const FString TravelURL = FString::Printf(TEXT("%s?Ticket=%s"), *ServerAddr, *TicketID);
	ClientTravel(TravelURL, TRAVEL_Absolute);
}

void AIU_PlayerController::Client_ReceiveQuestSnapshot_Implementation(const FIU_QuestSnapshot& Snapshot)
{
	CachedQuestSnapshot = Snapshot;
	bHasCachedQuestSnapshot = true;
	if (ActiveInventoryWidget)
	{
		ActiveInventoryWidget->ApplyQuestSnapshot(Snapshot);
	}
	if (!Snapshot.ActionMessage.IsEmpty())
	{
		if (AIU_HUDManager* IUHUD = Cast<AIU_HUDManager>(GetHUD()))
		{
			IUHUD->NotifyInventoryToast(Snapshot.ActionMessage, true);
		}
	}
}

void AIU_PlayerController::Server_RequestRefreshQuests_Implementation()
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend || !Backend->IsBackendBridgeEnabled()) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->FetchQuestSnapshot(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		[WeakPC](bool bSuccess, const FIU_QuestSnapshot& Snap, const FString&)
		{
			if (bSuccess)
			{
				if (AIU_PlayerController* PC = WeakPC.Get())
				{
					PC->Client_ReceiveQuestSnapshot(Snap);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestClaimQuest_Implementation(const FString& QuestID)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend || !Backend->IsBackendBridgeEnabled()) { return; }

	const uint64 OpSeq = ++GServerEconOpSeq;
	const FString IdemKey = FString::Printf(TEXT("qclaim_%s_%lld_%llu"), *IUPS->GetPersistedCharacterUUID(), IUPS->GetActiveFencingEpoch(), OpSeq);

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->ClaimQuestReward(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		QuestID.TrimStartAndEnd(),
		IdemKey,
		[WeakPC](bool bSuccess, const FIU_QuestSnapshot& QSnap, const FIU_InventorySnapshot& InvSnap, bool bHasInv, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (!bSuccess)
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
					return;
				}
				if (bHasInv)
				{
					PC->ApplyAuthoritativeInventorySnapshotOnServer(InvSnap);
				}
				PC->Client_ReceiveQuestSnapshot(QSnap);
				PC->Server_RequestRefreshPityShop();
			}
		});
}

void AIU_PlayerController::Server_RequestRepeatQuest_Implementation(const FString& QuestID)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend || !Backend->IsBackendBridgeEnabled()) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	Backend->RepeatQuest(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		QuestID.TrimStartAndEnd(),
		[WeakPC](bool bSuccess, const FIU_QuestSnapshot& Snap, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (bSuccess) { PC->Client_ReceiveQuestSnapshot(Snap); }
				else { PC->Client_NotifyInventoryFeedback(ErrMsg, true); }
			}
		});
}

void AIU_PlayerController::Client_ReceivePityShopSnapshot_Implementation(const FIU_PityShopSnapshot& Snapshot)
{
	CachedPityShopSnapshot = Snapshot;
	bHasCachedPityShopSnapshot = true;
	if (ActiveInventoryWidget)
	{
		ActiveInventoryWidget->ApplyPityShopSnapshot(Snapshot);
	}
	if (!Snapshot.ActionMessage.IsEmpty())
	{
		if (AIU_HUDManager* IUHUD = Cast<AIU_HUDManager>(GetHUD()))
		{
			IUHUD->NotifyInventoryToast(Snapshot.ActionMessage, true);
		}
	}
}

void AIU_PlayerController::Server_RequestRefreshPityShop_Implementation()
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend || !Backend->IsBackendBridgeEnabled()) { return; }

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	TWeakObjectPtr<AIU_PlayerState> WeakPS(IUPS);
	Backend->FetchPityShopSnapshot(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		[WeakPC, WeakPS](bool bSuccess, const FIU_PityShopSnapshot& Snap, const FString&)
		{
			if (bSuccess)
			{
				if (AIU_PlayerState* PS = WeakPS.Get())
				{
					PS->SetAuthoritativeTitanTokens(Snap.TokenBalance);
				}
				if (AIU_PlayerController* PC = WeakPC.Get())
				{
					PC->Client_ReceivePityShopSnapshot(Snap);
				}
			}
		});
}

void AIU_PlayerController::Server_RequestRedeemPityEntry_Implementation(const FString& EntryID)
{
	AIU_PlayerState* IUPS = GetPlayerState<AIU_PlayerState>();
	if (!IUPS || !IUPS->HasValidBackendLease()) { return; }
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend || !Backend->IsBackendBridgeEnabled()) { return; }

	const uint64 OpSeq = ++GServerEconOpSeq;
	const FString IdemKey = FString::Printf(TEXT("pity_%s_%lld_%llu"), *IUPS->GetPersistedCharacterUUID(), IUPS->GetActiveFencingEpoch(), OpSeq);

	TWeakObjectPtr<AIU_PlayerController> WeakPC(this);
	TWeakObjectPtr<AIU_PlayerState> WeakPS(IUPS);
	Backend->RedeemPityShopEntry(
		IUPS->GetPersistedCharacterUUID(),
		IUPS->GetActiveFencingEpoch(),
		EntryID.TrimStartAndEnd(),
		IdemKey,
		[WeakPC, WeakPS](bool bSuccess, const FIU_PityShopSnapshot& PitySnap, const FIU_InventorySnapshot& InvSnap, bool bHasInv, const FString& ErrMsg)
		{
			if (AIU_PlayerController* PC = WeakPC.Get())
			{
				if (!bSuccess)
				{
					PC->Client_NotifyInventoryFeedback(ErrMsg, true);
					return;
				}
				if (AIU_PlayerState* PS = WeakPS.Get())
				{
					PS->SetAuthoritativeTitanTokens(PitySnap.TokenBalance);
				}
				if (bHasInv)
				{
					PC->ApplyAuthoritativeInventorySnapshotOnServer(InvSnap);
				}
				PC->Client_ReceivePityShopSnapshot(PitySnap);
			}
		});
}
