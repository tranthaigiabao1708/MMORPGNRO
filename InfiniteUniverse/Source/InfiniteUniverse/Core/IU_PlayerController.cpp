// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_PlayerController.h"
#include "Core/IU_GameState.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Network/IU_BackendSubsystem.h"
#include "UI/IU_HUDManager.h"
#include "UI/IU_LoginCharacterWidget.h"
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
