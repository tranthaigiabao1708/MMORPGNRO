// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Movement/IU_MovementComponent.h"
#include "Movement/IU_FlightComponent.h"
#include "Combat/IU_CombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"

UIU_MovementComponent::UIU_MovementComponent()
{
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = true;
	NavAgentProps.bCanCrouch = true;
	NavAgentProps.bCanFly = true;
	bCanWalkOffLedgesWhenCrouching = true;

	RefreshMovementSettings();
}

void UIU_MovementComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* Owner = GetOwner())
	{
		CachedFlightComponent = Owner->FindComponentByClass<UIU_FlightComponent>();
	}
	RefreshMovementSettings();
	SetCharacterRotationMode(CurrentRotationMode);
}

void UIU_MovementComponent::RefreshMovementSettings()
{
	MaxWalkSpeed = (bIsSprinting && !IsCrouching()) ? SprintSpeed : WalkSpeed;
	MaxWalkSpeedCrouched = CrouchWalkSpeed;
	SetCrouchedHalfHeight(CrouchedCapsuleHalfHeight);
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
		MaxWalkSpeed = (bIsSprinting && !IsCrouching()) ? SprintSpeed : WalkSpeed;
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
		SetIUMovementMode(EIU_PlayerMovementMode::Flying);
		break;

	case MOVE_Custom:
		if (CustomMovementMode == CMOVE_FreeFlight)
		{
			SetIUMovementMode(EIU_PlayerMovementMode::Flying);
		}
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

void UIU_MovementComponent::EnterCustomFreeFlight()
{
	if (!CachedFlightComponent && GetOwner())
	{
		CachedFlightComponent = GetOwner()->FindComponentByClass<UIU_FlightComponent>();
	}
	SetMovementMode(MOVE_Custom, CMOVE_FreeFlight);
}

void UIU_MovementComponent::ExitCustomFreeFlight(bool bTouchdownOnFloor)
{
	// Pass cleanly through Landing state before settling into Grounded or Falling
	SetIUMovementMode(EIU_PlayerMovementMode::Landing);

	if (bTouchdownOnFloor || TryAutoLandOnWalkableFloor())
	{
		Velocity.Z = 0.0f;
		SetMovementMode(MOVE_Walking);
		SetIUMovementMode(EIU_PlayerMovementMode::Grounded);
	}
	else
	{
		SetMovementMode(MOVE_Falling);
		SetIUMovementMode(EIU_PlayerMovementMode::Jumping);
	}
}

bool UIU_MovementComponent::IsInCustomFreeFlight() const
{
	return (MovementMode == MOVE_Custom && CustomMovementMode == CMOVE_FreeFlight);
}

float UIU_MovementComponent::GetMaxSpeed() const
{
	if (IsInCustomFreeFlight() && CachedFlightComponent)
	{
		return CachedFlightComponent->GetEffectiveMaxFlightSpeed();
	}
	return Super::GetMaxSpeed();
}

float UIU_MovementComponent::GetMaxAcceleration() const
{
	if (IsInCustomFreeFlight() && CachedFlightComponent)
	{
		return CachedFlightComponent->GetEffectiveFlightAcceleration();
	}
	return Super::GetMaxAcceleration();
}

float UIU_MovementComponent::GetMaxBrakingDeceleration() const
{
	if (IsInCustomFreeFlight() && CachedFlightComponent)
	{
		return CachedFlightComponent->FlightDeceleration;
	}
	return Super::GetMaxBrakingDeceleration();
}

void UIU_MovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	Super::PhysCustom(deltaTime, Iterations);

	if (CustomMovementMode == CMOVE_FreeFlight)
	{
		PhysCustom_FreeFlight(deltaTime, Iterations);
	}
}

void UIU_MovementComponent::PhysCustom_FreeFlight(float DeltaTime, int32 Iterations)
{
	if (DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (!CachedFlightComponent && GetOwner())
	{
		CachedFlightComponent = GetOwner()->FindComponentByClass<UIU_FlightComponent>();
	}
	if (!CachedFlightComponent || !UpdatedComponent)
	{
		SetMovementMode(MOVE_Falling);
		return;
	}

	RestorePreAdditiveRootMotionVelocity();

	if (!CachedCombatComponent && GetOwner())
	{
		CachedCombatComponent = GetOwner()->FindComponentByClass<UIU_CombatComponent>();
	}
	const bool bInAerialFlashDodge = (CachedCombatComponent && CachedCombatComponent->GetCombatState() == EIU_CombatState::Dodging);

	// Advance 2-stage physical ascent timers in lockstep with physics DeltaTime
	CachedFlightComponent->StepFlightPhysicsTimers(DeltaTime);

	const FVector DesiredVel = CachedFlightComponent->ComputeDesiredFlightVelocity();
	const bool bIdleHoverInput = DesiredVel.IsNearlyZero(1.0f);
	const float Rate = bIdleHoverInput
		? CachedFlightComponent->FlightDeceleration
		: CachedFlightComponent->GetEffectiveFlightAcceleration();

	if (!bInAerialFlashDodge && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Velocity = FMath::VInterpConstantTo(Velocity, DesiredVel, DeltaTime, Rate);
	}

	ApplyRootMotionToVelocity(DeltaTime);

	Iterations++;
	bJustTeleported = false;

	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = Velocity * DeltaTime;

	if (!Adjusted.IsNearlyZero(1e-6f))
	{
		FHitResult Hit(1.0f);
		SafeMoveUpdatedComponent(Adjusted, UpdatedComponent->GetComponentQuat(), true, Hit);

		if (Hit.Time < 1.0f)
		{
			const bool bDescendingIntent = (CachedFlightComponent->GetVerticalFlightInput() < -0.1f) || (DesiredVel.Z < -150.0f);
			if (bDescendingIntent && IsWalkable(Hit))
			{
				CachedFlightComponent->ExitFlight(true);
				return;
			}

			HandleImpact(Hit, DeltaTime, Adjusted);
			SlideAlongSurface(Adjusted, 1.0f - Hit.Time, Hit.Normal, Hit, true);
		}
	}

	// If actively holding descend very close to a walkable surface, transition smoothly into Landing -> Grounded
	if (CachedFlightComponent->GetVerticalFlightInput() < -0.1f && TryAutoLandOnWalkableFloor())
	{
		CachedFlightComponent->ExitFlight(true);
		return;
	}

	if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / DeltaTime;
	}
}

bool UIU_MovementComponent::TryAutoLandOnWalkableFloor()
{
	if (!CharacterOwner || !UpdatedComponent || !GetWorld())
	{
		return false;
	}

	UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	if (!Capsule)
	{
		return false;
	}

	const float TraceDist = CachedFlightComponent ? CachedFlightComponent->AutoLandFloorTraceDistance : 16.0f;
	const FVector Start = UpdatedComponent->GetComponentLocation();
	const FVector End = Start - FVector(0.0f, 0.0f, TraceDist);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IUFlightAutoLand), false, CharacterOwner);
	FHitResult FloorHit;
	const bool bHit = GetWorld()->SweepSingleByChannel(
		FloorHit,
		Start,
		End,
		UpdatedComponent->GetComponentQuat(),
		UpdatedComponent->GetCollisionObjectType(),
		FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius() * 0.95f, Capsule->GetScaledCapsuleHalfHeight()),
		QueryParams);

	return bHit && FloorHit.bBlockingHit && IsWalkable(FloorHit);
}
