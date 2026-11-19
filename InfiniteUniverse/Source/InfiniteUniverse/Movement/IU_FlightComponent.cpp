// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Movement/IU_FlightComponent.h"
#include "Movement/IU_MovementComponent.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Net/UnrealNetwork.h"
#include "InfiniteUniverse.h"

UIU_FlightComponent::UIU_FlightComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UIU_FlightComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UIU_FlightComponent, bReplicatedFlightDashing, COND_SkipOwner);
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
		// Starting a fresh ascent while already hovering/flying
		ContinuousAscentHoldTime = 0.0f;
		return;
	}

	if (!OwnerCharacter || !MovementComp)
	{
		OwnerCharacter = Cast<AIU_PlayerCharacter>(GetOwner());
		MovementComp = OwnerCharacter ? OwnerCharacter->GetIUMovementComponent() : nullptr;
		if (!MovementComp)
		{
			return;
		}
	}

	// STRICT RULE: Flight ONLY activates when pressing Space while ALREADY airborne (2nd Space press)!
	if (MovementComp->IsFalling() || MovementComp->GetIUMovementMode() == EIU_PlayerMovementMode::Jumping)
	{
		EnterFlight();
		bInInitialActivationAscent = true;
		return;
	}

	// First Space press on ground: uncrouch if crouched, then perform standard Jump ONLY (never auto-fly)
	if (OwnerCharacter->bIsCrouched)
	{
		OwnerCharacter->UnCrouch();
	}
	OwnerCharacter->Jump();
}

void UIU_FlightComponent::HandleJumpOrAscendCompleted()
{
	bAscendHeld = false;
	bInInitialActivationAscent = false;
	ContinuousAscentHoldTime = 0.0f;

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
	if (OwnerCharacter && OwnerCharacter->HasAuthority())
	{
		bReplicatedFlightDashing = bActive && IsFlying();
	}
}

bool UIU_FlightComponent::IsFlightDashing() const
{
	if (!IsFlying() || !MovementComp)
	{
		return false;
	}
	if (OwnerCharacter && OwnerCharacter->GetLocalRole() == ROLE_SimulatedProxy)
	{
		return bReplicatedFlightDashing && MovementComp->Velocity.SizeSquared() > FMath::Square(30.0f);
	}
	return bWantsFlightDash && !IsHoveringInputIdle();
}

void UIU_FlightComponent::ApplyNetworkSavedFlightFlags(bool bNetWantsFlight, bool bNetAscendHeld, bool bNetDescendHeld, bool bNetFlightDash)
{
	bWantsFlightDash = bNetFlightDash;

	if (bNetWantsFlight && !IsFlying())
	{
		EnterFlight();
		bInInitialActivationAscent = bNetAscendHeld;
	}
	else if (!bNetWantsFlight && IsFlying())
	{
		ExitFlight(false);
	}

	if (bNetAscendHeld && !bAscendHeld && IsFlying())
	{
		ContinuousAscentHoldTime = 0.0f;
	}
	bAscendHeld = bNetAscendHeld;
	bDescendHeld = bNetDescendHeld;

	if (OwnerCharacter && OwnerCharacter->HasAuthority())
	{
		bReplicatedFlightDashing = bNetWantsFlight && bNetFlightDash;
	}
}

void UIU_FlightComponent::ToggleFlightMode()
{
	// Explicit toggle key only cancels active flight; entering flight strictly requires double-Space in air
	if (IsFlying())
	{
		ExitFlight(false);
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

	if (OwnerCharacter->bIsCrouched)
	{
		OwnerCharacter->UnCrouch();
	}
	OwnerCharacter->StopJumping();

	TimeSinceFlightEntered = 0.0f;
	ContinuousAscentHoldTime = 0.0f;

	// Cushion existing vertical jump/fall momentum smoothly into initial slow anti-gravity float (never teleport!)
	const float InitialSlowCap = VerticalFlightSpeed * InitialAscentStartRatio;
	MovementComp->Velocity.Z = FMath::Clamp(MovementComp->Velocity.Z * 0.25f, -80.0f, InitialSlowCap);

	MovementComp->EnterCustomFreeFlight();
	SetComponentTickEnabled(true);

	OnFlightStateChanged.Broadcast(true);
	UE_LOG(LogIUFlight, Verbose, TEXT("Activated Free 3D Flight via airborne Space at %s"), *OwnerCharacter->GetActorLocation().ToCompactString());
	return true;
}

void UIU_FlightComponent::ExitFlight(bool bEnteredViaLandingTouchdown)
{
	if (!MovementComp || !IsFlying())
	{
		return;
	}

	bDescendHeld = false;
	bInInitialActivationAscent = false;
	ContinuousAscentHoldTime = 0.0f;

	if (bLastBroadcastDashState)
	{
		bLastBroadcastDashState = false;
		OnFlightDashChanged.Broadcast(false);
	}

	MovementComp->ExitCustomFreeFlight(bEnteredViaLandingTouchdown);
	OnFlightStateChanged.Broadcast(false);
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

void UIU_FlightComponent::StepFlightPhysicsTimers(float DeltaTime)
{
	if (!IsFlying())
	{
		return;
	}

	TimeSinceFlightEntered += DeltaTime;
	if (bAscendHeld && !bDescendHeld)
	{
		ContinuousAscentHoldTime += DeltaTime;
	}
	else
	{
		ContinuousAscentHoldTime = 0.0f;
		bInInitialActivationAscent = false;
	}
}

float UIU_FlightComponent::EvaluateAscentSpeedCurveFactor() const
{
	// During the first ~1.0s after double-Space activation, rise gently with quadratic gather;
	// after 1.0s, surge smoothly up to 100% terminal flight speed!
	const float SlowWindow = bInInitialActivationAscent
		? FMath::Max(InitialSlowAscentDuration, 0.1f)
		: FMath::Max(InitialSlowAscentDuration * 0.35f, 0.1f);

	const float T = ContinuousAscentHoldTime;
	if (T < SlowWindow)
	{
		const float Alpha = FMath::Clamp(T / SlowWindow, 0.0f, 1.0f);
		return FMath::Lerp(InitialAscentStartRatio, SlowPhaseEndSpeedRatio, Alpha * Alpha);
	}

	const float PostSlowT = T - SlowWindow;
	const float RampAlpha = FMath::Clamp(PostSlowT / FMath::Max(AscentSurgeRampDuration, 0.1f), 0.0f, 1.0f);
	// Smooth ease-in-out power curve surging to 1.0 (100% max speed cap)
	const float CurvedAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, RampAlpha, 1.4f);
	return FMath::Lerp(SlowPhaseEndSpeedRatio, 1.0f, CurvedAlpha);
}

FVector UIU_FlightComponent::ComputeDesiredFlightVelocity() const
{
	if (!OwnerCharacter)
	{
		return FVector::ZeroVector;
	}

	const FRotator ViewRot = OwnerCharacter->Controller
		? OwnerCharacter->Controller->GetControlRotation()
		: OwnerCharacter->GetActorRotation();

	const FVector CamForward3D = ViewRot.Vector();
	const FVector CamRightHorizontal = FRotationMatrix(FRotator(0.0f, ViewRot.Yaw, 0.0f)).GetUnitAxis(EAxis::Y);

	const float DashMult = bWantsFlightDash ? FlightDashMultiplier : 1.0f;
	const float MaxPlanarSpeed = FlightSpeed * DashMult;
	const float MaxVertSpeed = VerticalFlightSpeed * DashMult;

	FVector PlanarInputDir = (CamForward3D * CachedMoveAxis.Y) + (CamRightHorizontal * CachedMoveAxis.X);

	// On Dedicated/Listen Server Authority simulating a remote AutonomousProxy's ServerMove,
	// recover the exact 3D input vector streamed inside UCharacterMovementComponent::Acceleration!
	if (!OwnerCharacter->IsLocallyControlled() && MovementComp && PlanarInputDir.IsNearlyZero(0.01f))
	{
		const float MaxAccel = FMath::Max(MovementComp->GetMaxAcceleration(), 1.0f);
		PlanarInputDir = MovementComp->GetCurrentAcceleration() / MaxAccel;
	}

	if (PlanarInputDir.SizeSquared() > 1.0f)
	{
		PlanarInputDir.Normalize();
	}

	const float VertInput = GetVerticalFlightInput();
	float TargetVertSpeed = 0.0f;
	if (VertInput > 0.05f)
	{
		// Apply 2-stage physical ascent momentum curve (slow 1s gather -> surge to terminal max speed; x2 with Ctrl!)
		TargetVertSpeed = VertInput * MaxVertSpeed * EvaluateAscentSpeedCurveFactor();
	}
	else if (VertInput < -0.05f)
	{
		TargetVertSpeed = VertInput * MaxVertSpeed;
	}

	FVector DesiredVel = (PlanarInputDir * MaxPlanarSpeed) + FVector(0.0f, 0.0f, TargetVertSpeed);

	// Enforce terminal flight speed ceiling (1.5x base, or x2 when holding Ctrl!)
	const float TerminalSpeedCeiling = FMath::Max(MaxPlanarSpeed, MaxVertSpeed);
	if (DesiredVel.SizeSquared() > FMath::Square(TerminalSpeedCeiling))
	{
		DesiredVel = DesiredVel.GetSafeNormal() * TerminalSpeedCeiling;
	}

	return DesiredVel;
}

bool UIU_FlightComponent::IsHoveringInputIdle() const
{
	return CachedMoveAxis.IsNearlyZero(0.05f) && FMath::IsNearlyZero(GetVerticalFlightInput(), 0.05f);
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

		TargetLean.Pitch = -ForwardRatio * MaxVisualPitchLeanDegrees;
		TargetLean.Roll = RightRatio * MaxVisualRollBankDegrees;
	}

	CurrentVisualLean = FMath::RInterpTo(CurrentVisualLean, TargetLean, DeltaTime, 8.0f);
	if (OwnerCharacter)
	{
		OwnerCharacter->ApplyVisualFlightLean(CurrentVisualLean);
	}
}
