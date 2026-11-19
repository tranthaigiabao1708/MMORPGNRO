// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Combat/IU_TargetingComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Attributes/IU_AttributeComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

UIU_TargetingComponent::UIU_TargetingComponent()
{
	// Zero-Tick when unlocked; enabled dynamically only while LockedTarget is valid
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UIU_TargetingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	AActor* Target = LockedTarget.Get();

	if (!Owner || !IsValidLockCandidate(Target))
	{
		ClearLockOn();
		return;
	}

	const double DistSq = FVector::DistSquared(Owner->GetActorLocation(), Target->GetActorLocation());
	if (DistSq > FMath::Square((double)MaxMaintainLockDistance))
	{
		ClearLockOn();
		return;
	}

	// Smoothly assist controller rotation so 3D target stays framed cleanly ABOVE the character's head (Genshin-style)
	if (APawn* OwnerPawn = Cast<APawn>(Owner))
	{
		if (AController* Ctrl = OwnerPawn->GetController())
		{
			const FVector AimPoint = GetLockedTargetAimLocation();
			FVector PivotOrigin = OwnerPawn->GetPawnViewLocation();
			float ArmLen = 0.0f;
			float SocketOffsetZ = 0.0f;

			if (const AIU_PlayerCharacter* PlayerChar = Cast<AIU_PlayerCharacter>(OwnerPawn))
			{
				if (const USpringArmComponent* Boom = PlayerChar->GetCameraBoom())
				{
					PivotOrigin = Boom->GetComponentLocation() + Boom->TargetOffset;
					ArmLen = Boom->TargetArmLength;
					SocketOffsetZ = Boom->SocketOffset.Z;
				}
			}

			const FVector PivotToTarget = AimPoint - PivotOrigin;
			FRotator DesiredRot = PivotToTarget.Rotation();

			// Exact analytical compensation for elevated CameraSocketOffset.Z so locked enemy projects onto center sightline
			if (!FMath::IsNearlyZero(SocketOffsetZ))
			{
				const float TotalRayRange = FMath::Max(ArmLen + PivotToTarget.Size(), FMath::Abs(SocketOffsetZ) + 1.0f);
				const float ParallaxPitchDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(SocketOffsetZ / TotalRayRange, -0.85f, 0.85f)));
				DesiredRot.Pitch = FMath::Clamp(DesiredRot.Pitch - ParallaxPitchDeg, -75.0f, 75.0f);
			}

			const FRotator CurrentRot = Ctrl->GetControlRotation();
			const FRotator SmoothedRot = FMath::RInterpTo(CurrentRot, DesiredRot, DeltaTime, CameraAssistInterpSpeed);
			Ctrl->SetControlRotation(FRotator(SmoothedRot.Pitch, SmoothedRot.Yaw, 0.0f));
		}
	}
}

bool UIU_TargetingComponent::ToggleLockOn()
{
	if (IsLockedOn())
	{
		ClearLockOn();
		return false;
	}

	TArray<AActor*> Candidates;
	GatherValidCandidates(Candidates);
	if (Candidates.IsEmpty())
	{
		return false;
	}

	AActor* Owner = GetOwner();
	FVector ViewLoc = Owner->GetActorLocation();
	FVector ViewDir = Owner->GetActorForwardVector();
	if (const AIU_PlayerCharacter* PlayerChar = Cast<AIU_PlayerCharacter>(Owner))
	{
		PlayerChar->GetCameraAimRay(ViewLoc, ViewDir);
	}
	else if (const APawn* OwnerPawn = Cast<APawn>(Owner))
	{
		if (const AController* Ctrl = OwnerPawn->GetController())
		{
			ViewLoc = OwnerPawn->GetPawnViewLocation();
			ViewDir = Ctrl->GetControlRotation().Vector();
		}
	}

	AActor* BestCandidate = nullptr;
	float BestScore = -FLT_MAX;

	for (AActor* Cand : Candidates)
	{
		const FVector ToTarget = Cand->GetActorLocation() - ViewLoc;
		const float Dist = FMath::Max(ToTarget.Size(), 1.0f);
		const FVector DirToTarget = ToTarget / Dist;

		const float Alignment = FVector::DotProduct(ViewDir, DirToTarget); // [-1..1]
		const float Proximity = 1.0f - FMath::Clamp(Dist / MaxAcquisitionRange, 0.0f, 1.0f);
		const float Score = (Alignment * 0.70f) + (Proximity * 0.30f);

		if (Score > BestScore)
		{
			BestScore = Score;
			BestCandidate = Cand;
		}
	}

	return SetLockedTarget(BestCandidate);
}

bool UIU_TargetingComponent::SwitchLockedTarget(float Direction)
{
	if (!IsLockedOn() || FMath::IsNearlyZero(Direction))
	{
		return false;
	}

	TArray<AActor*> Candidates;
	GatherValidCandidates(Candidates);
	if (Candidates.Num() <= 1)
	{
		return false;
	}

	AActor* Owner = GetOwner();
	AActor* Current = LockedTarget.Get();
	FRotator ViewRot = Owner->GetActorRotation();
	if (const APawn* OwnerPawn = Cast<APawn>(Owner))
	{
		if (const AController* Ctrl = OwnerPawn->GetController())
		{
			ViewRot = Ctrl->GetControlRotation();
		}
	}

	const FVector RightAxis = FRotationMatrix(FRotator(0.0f, ViewRot.Yaw, 0.0f)).GetUnitAxis(EAxis::Y);
	const FVector CurrentDir = (Current->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal();

	AActor* BestSideTarget = nullptr;
	float BestDist = FLT_MAX;

	for (AActor* Cand : Candidates)
	{
		if (Cand == Current)
		{
			continue;
		}

		const FVector CandVec = Cand->GetActorLocation() - Owner->GetActorLocation();
		const FVector CandDir = CandVec.GetSafeNormal();
		const float SideDot = FVector::DotProduct(CandDir - CurrentDir, RightAxis) * FMath::Sign(Direction);

		if (SideDot > 0.02f)
		{
			const float Dist = CandVec.SizeSquared();
			if (Dist < BestDist)
			{
				BestDist = Dist;
				BestSideTarget = Cand;
			}
		}
	}

	// Fallback: cycle to any other valid target if none lies strictly on that side
	if (!BestSideTarget)
	{
		for (AActor* Cand : Candidates)
		{
			if (Cand != Current)
			{
				BestSideTarget = Cand;
				break;
			}
		}
	}

	return BestSideTarget ? SetLockedTarget(BestSideTarget) : false;
}

bool UIU_TargetingComponent::SetLockedTarget(AActor* CandidateTarget)
{
	if (!IsValidLockCandidate(CandidateTarget))
	{
		return false;
	}

	const double DistSq = FVector::DistSquared(GetOwner()->GetActorLocation(), CandidateTarget->GetActorLocation());
	if (DistSq > FMath::Square((double)MaxAcquisitionRange))
	{
		return false;
	}

	if (LockedTarget.Get() == CandidateTarget)
	{
		return true;
	}

	// Unbind previous target's death delegate
	if (AActor* OldTarget = LockedTarget.Get())
	{
		if (UIU_AttributeComponent* OldAttr = OldTarget->FindComponentByClass<UIU_AttributeComponent>())
		{
			OldAttr->OnHealthDepleted.RemoveDynamic(this, &UIU_TargetingComponent::HandleLockedTargetDepleted);
		}
	}

	LockedTarget = CandidateTarget;

	if (UIU_AttributeComponent* NewAttr = CandidateTarget->FindComponentByClass<UIU_AttributeComponent>())
	{
		NewAttr->OnHealthDepleted.AddDynamic(this, &UIU_TargetingComponent::HandleLockedTargetDepleted);
	}

	SetComponentTickEnabled(true);
	OnTargetLockChanged.Broadcast(CandidateTarget);
	return true;
}

void UIU_TargetingComponent::ClearLockOn()
{
	if (AActor* OldTarget = LockedTarget.Get())
	{
		if (UIU_AttributeComponent* OldAttr = OldTarget->FindComponentByClass<UIU_AttributeComponent>())
		{
			OldAttr->OnHealthDepleted.RemoveDynamic(this, &UIU_TargetingComponent::HandleLockedTargetDepleted);
		}
	}

	const bool bWasLocked = LockedTarget.IsValid();
	LockedTarget.Reset();
	SetComponentTickEnabled(false);

	if (bWasLocked)
	{
		OnTargetLockChanged.Broadcast(nullptr);
	}
}

FVector UIU_TargetingComponent::GetLockedTargetAimLocation() const
{
	if (const AActor* Target = LockedTarget.Get())
	{
		return Target->GetActorLocation() + FVector(0.0f, 0.0f, TargetAimHeightOffset);
	}
	return FVector::ZeroVector;
}

AActor* UIU_TargetingComponent::FindNearestLivingTarget(float MaxSearchRadius) const
{
	UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner || MaxSearchRadius <= 0.0f)
	{
		return nullptr;
	}

	const FVector Origin = Owner->GetActorLocation();
	double BestDistSq = FMath::Square((double)MaxSearchRadius);
	AActor* Nearest = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (IsValidLockCandidate(Actor))
		{
			const double DistSq = FVector::DistSquared(Origin, Actor->GetActorLocation());
			if (DistSq <= BestDistSq)
			{
				BestDistSq = DistSq;
				Nearest = Actor;
			}
		}
	}

	return Nearest;
}

bool UIU_TargetingComponent::IsValidLockCandidate(const AActor* Candidate) const
{
	if (!IsValid(Candidate) || Candidate == GetOwner() || Candidate->IsActorBeingDestroyed())
	{
		return false;
	}

	const UIU_DamageComponent* DmgComp = Candidate->FindComponentByClass<UIU_DamageComponent>();
	const UIU_AttributeComponent* AttrComp = Candidate->FindComponentByClass<UIU_AttributeComponent>();
	if (!DmgComp || !AttrComp || AttrComp->IsDead())
	{
		return false;
	}

	return true;
}

void UIU_TargetingComponent::HandleLockedTargetDepleted(AActor* DepletedActor)
{
	if (DepletedActor == LockedTarget.Get())
	{
		ClearLockOn();
	}
}

void UIU_TargetingComponent::GatherValidCandidates(TArray<AActor*>& OutCandidates) const
{
	OutCandidates.Reset();
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}

	const FVector Origin = Owner->GetActorLocation();
	const double MaxRangeSq = FMath::Square((double)MaxAcquisitionRange);

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (IsValidLockCandidate(Actor))
		{
			if (FVector::DistSquared(Origin, Actor->GetActorLocation()) <= MaxRangeSq)
			{
				OutCandidates.Add(Actor);
			}
		}
	}
}
