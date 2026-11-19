// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Movement/IU_MovementComponent.h"
#include "GameFramework/Character.h"

UIU_MovementComponent::UIU_MovementComponent()
{
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = true;
	NavAgentProps.bCanCrouch = false;
	NavAgentProps.bCanFly = true;

	RefreshMovementSettings();
}

void UIU_MovementComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshMovementSettings();
	SetCharacterRotationMode(CurrentRotationMode);
}

void UIU_MovementComponent::RefreshMovementSettings()
{
	MaxWalkSpeed = bIsSprinting ? SprintSpeed : WalkSpeed;
	MaxAcceleration = GroundAcceleration;
	BrakingDecelerationWalking = GroundBrakingDeceleration;
	JumpZVelocity = JumpVelocity;
	AirControl = AirControlRatio;
	GravityScale = DefaultGravityScale;
	RotationRate = FRotator(0.0f, MovementRotationRateYaw, 0.0f);
}

void UIU_MovementComponent::SetSprinting(bool bNewSprinting)
{
	bIsSprinting = bNewSprinting;
	if (CurrentIUMovementMode == EIU_PlayerMovementMode::Grounded ||
		CurrentIUMovementMode == EIU_PlayerMovementMode::Jumping)
	{
		MaxWalkSpeed = bIsSprinting ? SprintSpeed : WalkSpeed;
	}
}

void UIU_MovementComponent::SetCharacterRotationMode(EIU_CharacterRotationMode NewRotationMode)
{
	CurrentRotationMode = NewRotationMode;

	if (ACharacter* OwnerChar = GetCharacterOwner())
	{
		// Keep Actor's direct controller yaw lock off so all yaw transitions rotate smoothly via RotationRate
		OwnerChar->bUseControllerRotationYaw = false;
	}

	if (CurrentRotationMode == EIU_CharacterRotationMode::OrientToMovement)
	{
		bOrientRotationToMovement = true;
		bUseControllerDesiredRotation = false;
	}
	else
	{
		bOrientRotationToMovement = false;
		bUseControllerDesiredRotation = true;
	}
}

void UIU_MovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	switch (MovementMode)
	{
	case MOVE_Walking:
	case MOVE_NavWalking:
		SetIUMovementMode(EIU_PlayerMovementMode::Grounded);
		break;

	case MOVE_Falling:
		if (CurrentIUMovementMode != EIU_PlayerMovementMode::Landing)
		{
			SetIUMovementMode(EIU_PlayerMovementMode::Jumping);
		}
		break;

	case MOVE_Flying:
	case MOVE_Custom:
		SetIUMovementMode(EIU_PlayerMovementMode::Flying);
		break;

	default:
		break;
	}
}

void UIU_MovementComponent::SetIUMovementMode(EIU_PlayerMovementMode NewMode)
{
	if (CurrentIUMovementMode != NewMode)
	{
		const EIU_PlayerMovementMode OldMode = CurrentIUMovementMode;
		CurrentIUMovementMode = NewMode;
		OnIUMovementModeChanged.Broadcast(OldMode, CurrentIUMovementMode);
	}
}
