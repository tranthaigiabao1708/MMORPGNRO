// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_PlayerController.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"

AIU_PlayerController::AIU_PlayerController()
{
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
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
		{
			if (DefaultMappingContext)
			{
				InputSubsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}
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

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (MoveAction)
		{
			EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AIU_PlayerController::HandleMove);
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
		if (SprintAction)
		{
			EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &AIU_PlayerController::HandleSprintStarted);
			EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this, &AIU_PlayerController::HandleSprintCompleted);
		}
		if (ToggleRotationModeAction)
		{
			EnhancedInput->BindAction(ToggleRotationModeAction, ETriggerEvent::Started, this, &AIU_PlayerController::HandleToggleRotationMode);
		}
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
