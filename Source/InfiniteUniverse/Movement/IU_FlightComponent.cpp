// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Movement/IU_FlightComponent.h"
#include "Movement/IU_MovementComponent.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "InfiniteUniverse.h"

UIU_FlightComponent::UIU_FlightComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UIU_FlightComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<AIU_PlayerCharacter>(GetOwner());
	if (OwnerCharacter)
	{
		MovementComp = OwnerCharacter->GetIUMovementComponent();
	}
}

void UIU_FlightComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const bool bEffectiveDash = IsFlightDashing();
	if (bEffectiveDash != bLastBroadcastDashState)
	{
		bLastBroadcastDashState = bEffectiveDash;
		OnFlightDashChanged.Broadcast(bEffectiveDash);
	}

	UpdateVisualFlightLean(DeltaTime);

	// Disable tick automatically once back on ground and visual lean has returned to zero
	if (!IsFlying() && CurrentVisualLean.IsNearlyZero(0.2f))
	{
		CurrentVisualLean = FRotator::ZeroRotator;
		SetComponentTickEnabled(false);
	}
}

void UIU_FlightComponent::HandleJumpOrAscendStarted()
{
	bAscendHeld = true;

	if (IsFlying())
	{
		return;
	}

	if (!OwnerCharacter || !MovementComp)
	{
		return;
	}

	// Pressing Space a second time while already airborne immediately engages smooth Free Flight
	if (MovementComp->GetIUMovementMode() == EIU_PlayerMovementMode::Jumping)
	{
		EnterFlight();
		return;
	}

	// On ground: perform normal Jump immediately on tap, and start hold timer for seamless liftoff into Flight
	OwnerCharacter->Jump();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			SpaceHoldTimerHandle,
			this,
			&UIU_FlightComponent::OnSpaceHoldTimerExpired,
			SpaceHoldActivationTime,
			false);
	}
}

void UIU_FlightComponent::HandleJumpOrAscendCompleted()
{
	bAscendHeld = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpaceHoldTimerHandle);
	}

	if (OwnerCharacter && !IsFlying())
	{
		OwnerCharacter->StopJumping();
	}
}

void UIU_FlightComponent::HandleDescendStarted()
{
	bDescendHeld = true;
}

void UIU_FlightComponent::HandleDescendCompleted()
{
	bDescendHeld = false;
}

void UIU_FlightComponent::SetMoveAxisInput(const FVector2D& InMoveAxis)
{
	CachedMoveAxis = InMoveAxis;
}

void UIU_FlightComponent::SetFlightDashActive(bool bActive)
{
	bWantsFlightDash = bActive;
}

void UIU_FlightComponent::ToggleFlightMode()
{
	if (IsFlying())
	{
		ExitFlight(false);
	}
	else
	{
		EnterFlight();
	}
}

bool UIU_FlightComponent::EnterFlight()
{
	if (!OwnerCharacter || !MovementComp)
	{
		OwnerCharacter = Cast<AIU_PlayerCharacter>(GetOwner());
		MovementComp = OwnerCharacter ? OwnerCharacter->GetIUMovementComponent() : nullptr;
		if (!MovementComp)
		{
			return false;
		}
	}

	if (IsFlying())
	{
		return true;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpaceHoldTimerHandle);
	}

	const bool bWasOnGround = MovementComp->IsMovingOnGround();
	OwnerCharacter->StopJumping();

	// Smoothly preserve existing momentum; if standing on ground, impart gentle upward velocity (NEVER teleport position!)
	if (bWasOnGround && MovementComp->Velocity.Z < GroundLiftoffUpwardVelocity)
	{
		MovementComp->Velocity.Z = GroundLiftoffUpwardVelocity;
	}
	else if (MovementComp->Velocity.Z < 0.0f)
	{
		// Dampen steep downward fall smoothly rather than snapping to zero
		MovementComp->Velocity.Z *= 0.35f;
	}

	MovementComp->EnterCustomFreeFlight();
	SetComponentTickEnabled(true);

	OnFlightStateChanged.Broadcast(true);
	UE_LOG(LogIUFlight, Verbose, TEXT("Entered Free 3D Flight at %s"), *OwnerCharacter->GetActorLocation().ToCompactString());
	return true;
}

void UIU_FlightComponent::ExitFlight(bool bEnteredViaLandingTouchdown)
{
	if (!MovementComp || !IsFlying())
	{
		return;
	}

	if (bLastBroadcastDashState)
	{
		bLastBroadcastDashState = false;
		OnFlightDashChanged.Broadcast(false);
	}

	MovementComp->ExitCustomFreeFlight(bEnteredViaLandingTouchdown);
	OnFlightStateChanged.Broadcast(false);
	UE_LOG(LogIUFlight, Verbose, TEXT("Exited Free 3D Flight (Touchdown=%d)"), bEnteredViaLandingTouchdown ? 1 : 0);
}

bool UIU_FlightComponent::IsFlying() const
{
	return MovementComp && MovementComp->IsInCustomFreeFlight();
}

bool UIU_FlightComponent::IsHovering() const
{
	if (!IsFlying() || !MovementComp)
	{
		return false;
	}
	return IsHoveringInputIdle() && MovementComp->Velocity.SizeSquared() <= FMath::Square(25.0f);
}

float UIU_FlightComponent::GetVerticalFlightInput() const
{
	float Vertical = 0.0f;
	if (bAscendHeld)
	{
		Vertical += 1.0f;
	}
	if (bDescendHeld)
	{
		Vertical -= 1.0f;
	}
	return Vertical;
}

float UIU_FlightComponent::GetEffectiveMaxFlightSpeed() const
{
	const float Mult = bWantsFlightDash ? FlightDashMultiplier : 1.0f;
	return FlightSpeed * Mult;
}

float UIU_FlightComponent::GetEffectiveFlightAcceleration() const
{
	const float Mult = bWantsFlightDash ? FlightDashMultiplier : 1.0f;
	return FlightAcceleration * Mult;
}

FVector UIU_FlightComponent::ComputeDesiredFlightVelocity() const
{
	if (!OwnerCharacter)
	{
		return FVector::ZeroVector;
	}

	// Use Controller's exact 3D ControlRotation (zero CameraLag delay and immune to wall-probe jitter)
	const FRotator ViewRot = OwnerCharacter->Controller
		? OwnerCharacter->Controller->GetControlRotation()
		: OwnerCharacter->GetActorRotation();

	const FVector CamForward3D = ViewRot.Vector();
	const FVector CamRightHorizontal = FRotationMatrix(FRotator(0.0f, ViewRot.Yaw, 0.0f)).GetUnitAxis(EAxis::Y);

	const float MaxPlanarSpeed = GetEffectiveMaxFlightSpeed();
	const float DashMult = bWantsFlightDash ? FlightDashMultiplier : 1.0f;
	const float MaxVertSpeed = VerticalFlightSpeed * DashMult;

	FVector PlanarInputDir = (CamForward3D * CachedMoveAxis.Y) + (CamRightHorizontal * CachedMoveAxis.X);
	if (PlanarInputDir.SizeSquared() > 1.0f)
	{
		PlanarInputDir.Normalize();
	}

	FVector DesiredVel = (PlanarInputDir * MaxPlanarSpeed) + FVector(0.0f, 0.0f, GetVerticalFlightInput() * MaxVertSpeed);

	// Cap combined diagonal speed gracefully so 3D + Vertical input stays controlled
	const float CombinedCap = FMath::Max(MaxPlanarSpeed, MaxVertSpeed);
	if (DesiredVel.SizeSquared() > FMath::Square(CombinedCap))
	{
		DesiredVel = DesiredVel.GetSafeNormal() * CombinedCap;
	}

	return DesiredVel;
}

bool UIU_FlightComponent::IsHoveringInputIdle() const
{
	return CachedMoveAxis.IsNearlyZero(0.05f) && FMath::IsNearlyZero(GetVerticalFlightInput(), 0.05f);
}

void UIU_FlightComponent::OnSpaceHoldTimerExpired()
{
	if (bAscendHeld && !IsFlying())
	{
		EnterFlight();
	}
}

void UIU_FlightComponent::UpdateVisualFlightLean(float DeltaTime)
{
	FRotator TargetLean = FRotator::ZeroRotator;

	if (IsFlying() && MovementComp && OwnerCharacter)
	{
		const FVector LocalVel = OwnerCharacter->GetActorTransform().InverseTransformVectorNoScale(MovementComp->Velocity);
		const float SpeedDenom = FMath::Max(FlightSpeed * FlightDashMultiplier, 1.0f);

		const float ForwardRatio = FMath::Clamp(LocalVel.X / SpeedDenom, -1.0f, 1.0f);
		const float RightRatio = FMath::Clamp(LocalVel.Y / SpeedDenom, -1.0f, 1.0f);

		// In UE5 character local space (+X forward), negative Pitch tilts torso forward into flight direction
		TargetLean.Pitch = -ForwardRatio * MaxVisualPitchLeanDegrees;
		TargetLean.Roll = RightRatio * MaxVisualRollBankDegrees;
	}

	CurrentVisualLean = FMath::RInterpTo(CurrentVisualLean, TargetLean, DeltaTime, 8.0f);
	OwnerCharacter->ApplyVisualFlightLean(CurrentVisualLean);
}
