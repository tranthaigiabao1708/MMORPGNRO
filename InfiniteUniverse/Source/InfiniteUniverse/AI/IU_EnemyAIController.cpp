// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "AI/IU_EnemyAIController.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Attributes/IU_AttributeComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InfiniteUniverse.h"

AIU_EnemyAIController::AIU_EnemyAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

void AIU_EnemyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	ControlledEnemy = Cast<AIU_EnemyCharacter>(InPawn);
	if (ControlledEnemy)
	{
		HomeLocation = ControlledEnemy->GetActorLocation();
		CurrentState = ControlledEnemy->IsDead() ? EIU_EnemyAIState::Dead : EIU_EnemyAIState::Idle;
		StateElapsedSeconds = 0.0f;
		SetActorTickEnabled(!ControlledEnemy->IsDead());
	}
}

void AIU_EnemyAIController::OnUnPossess()
{
	ControlledEnemy = nullptr;
	CurrentTarget.Reset();
	Super::OnUnPossess();
}

void AIU_EnemyAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!ControlledEnemy || ControlledEnemy->IsDead() || CurrentState == EIU_EnemyAIState::Dead)
	{
		if (CurrentState != EIU_EnemyAIState::Dead)
		{
			TransitionToState(EIU_EnemyAIState::Dead);
		}
		SetActorTickEnabled(false);
		return;
	}

	StateElapsedSeconds += DeltaSeconds;

	switch (CurrentState)
	{
	case EIU_EnemyAIState::Idle:
		TickIdleState(DeltaSeconds);
		break;

	case EIU_EnemyAIState::Detect:
		TickDetectState(DeltaSeconds);
		break;

	case EIU_EnemyAIState::Chase:
		TickChaseState(DeltaSeconds);
		break;

	case EIU_EnemyAIState::Attack:
		TickAttackState(DeltaSeconds);
		break;

	case EIU_EnemyAIState::Recover:
		TickRecoverState(DeltaSeconds);
		break;

	case EIU_EnemyAIState::HitReaction:
		// Wait deterministically for UIU_DamageComponent::OnHitReactionRecovered -> NotifyHitReactionRecovered()
		break;

	case EIU_EnemyAIState::Dead:
		break;
	}
}

void AIU_EnemyAIController::SetTargetActorOverride(AActor* NewTarget)
{
	ExplicitTargetOverride = NewTarget;
	if (IsValidHostileTarget(NewTarget))
	{
		CurrentTarget = NewTarget;
	}
	else if (!NewTarget)
	{
		CurrentTarget.Reset();
	}
}

void AIU_EnemyAIController::NotifyHitReactionStarted(AActor* InstigatorActor)
{
	if (!ControlledEnemy || ControlledEnemy->IsDead() || CurrentState == EIU_EnemyAIState::Dead)
	{
		return;
	}

	if (IsValidHostileTarget(InstigatorActor))
	{
		CurrentTarget = InstigatorActor;
	}

	StopMovement();
	if (UCharacterMovementComponent* CMC = ControlledEnemy->GetCharacterMovement())
	{
		CMC->ConsumeInputVector();
	}

	TransitionToState(EIU_EnemyAIState::HitReaction);
}

void AIU_EnemyAIController::NotifyHitReactionRecovered()
{
	if (!ControlledEnemy || ControlledEnemy->IsDead() || CurrentState == EIU_EnemyAIState::Dead)
	{
		return;
	}

	if (CurrentState != EIU_EnemyAIState::HitReaction)
	{
		return;
	}

	AActor* Target = CurrentTarget.Get();
	if (IsValidHostileTarget(Target) && ComputeEngagementDistance(Target) <= ControlledEnemy->LeashDisengageRange)
	{
		TransitionToState(EIU_EnemyAIState::Chase);
	}
	else
	{
		CurrentTarget.Reset();
		TransitionToState(EIU_EnemyAIState::Idle);
	}
}

void AIU_EnemyAIController::NotifyAttackImpactFinished()
{
	if (!ControlledEnemy || ControlledEnemy->IsDead() || CurrentState == EIU_EnemyAIState::Dead)
	{
		return;
	}

	if (CurrentState == EIU_EnemyAIState::Attack)
	{
		TransitionToState(EIU_EnemyAIState::Recover);
	}
}

void AIU_EnemyAIController::NotifyEnemyDied()
{
	StopMovement();
	CurrentTarget.Reset();
	TransitionToState(EIU_EnemyAIState::Dead);
	SetActorTickEnabled(false);
}

void AIU_EnemyAIController::ResetToIdle(const FVector& NewHomeLocation)
{
	StopMovement();
	HomeLocation = NewHomeLocation;
	CurrentTarget.Reset();
	StateElapsedSeconds = 0.0f;
	SetActorTickEnabled(true);
	TransitionToState(EIU_EnemyAIState::Idle);
}

void AIU_EnemyAIController::TransitionToState(EIU_EnemyAIState NewState)
{
	if (CurrentState == NewState)
	{
		return;
	}

	const EIU_EnemyAIState OldState = CurrentState;
	CurrentState = NewState;
	StateElapsedSeconds = 0.0f;

	if (ControlledEnemy)
	{
		if (UCharacterMovementComponent* CMC = ControlledEnemy->GetCharacterMovement())
		{
			if (NewState == EIU_EnemyAIState::Idle ||
				NewState == EIU_EnemyAIState::Detect ||
				NewState == EIU_EnemyAIState::Attack ||
				NewState == EIU_EnemyAIState::Recover ||
				NewState == EIU_EnemyAIState::Dead)
			{
				CMC->ConsumeInputVector();
				if (NewState != EIU_EnemyAIState::HitReaction)
				{
					CMC->StopMovementImmediately();
				}
			}
			else if (NewState == EIU_EnemyAIState::Chase)
			{
				CMC->MaxWalkSpeed = ControlledEnemy->ChaseMoveSpeed;
				CMC->MaxFlySpeed = ControlledEnemy->ChaseMoveSpeed;
				if (ControlledEnemy->bCanFlyChase && CMC->MovementMode != MOVE_Flying)
				{
					CMC->SetMovementMode(MOVE_Flying);
				}
			}
		}

		if (NewState == EIU_EnemyAIState::Attack)
		{
			AActor* Target = CurrentTarget.Get();
			if (!ControlledEnemy->StartTelegraphedAttack(Target))
			{
				// Fallback to Recover if target vanished right at state entry
				CurrentState = EIU_EnemyAIState::Recover;
			}
		}
	}

	UE_LOG(LogIUAI, Verbose, TEXT("[%s] AI State: %d -> %d"),
		*GetNameSafe(ControlledEnemy), static_cast<int32>(OldState), static_cast<int32>(CurrentState));
	OnAIStateChanged.Broadcast(OldState, CurrentState);
}

void AIU_EnemyAIController::TickIdleState(float DeltaSeconds)
{
	AActor* FoundTarget = ScanForHostileTarget();
	if (FoundTarget)
	{
		CurrentTarget = FoundTarget;
		TransitionToState(EIU_EnemyAIState::Detect);
	}
}

void AIU_EnemyAIController::TickDetectState(float DeltaSeconds)
{
	AActor* Target = CurrentTarget.Get();
	if (!IsValidHostileTarget(Target))
	{
		CurrentTarget.Reset();
		TransitionToState(EIU_EnemyAIState::Idle);
		return;
	}

	FaceTargetSmoothly(Target, DeltaSeconds, 16.0f);

	if (StateElapsedSeconds >= ControlledEnemy->DetectAlertDuration)
	{
		const float Dist = ComputeEngagementDistance(Target);
		if (Dist <= ControlledEnemy->AttackRange)
		{
			TransitionToState(EIU_EnemyAIState::Attack);
		}
		else
		{
			TransitionToState(EIU_EnemyAIState::Chase);
		}
	}
}

void AIU_EnemyAIController::TickChaseState(float DeltaSeconds)
{
	AActor* Target = CurrentTarget.Get();
	if (!IsValidHostileTarget(Target))
	{
		CurrentTarget.Reset();
		TransitionToState(EIU_EnemyAIState::Idle);
		return;
	}

	const float DistToTarget = ComputeEngagementDistance(Target);
	const float DistFromHome = FVector::Dist(ControlledEnemy->GetActorLocation(), HomeLocation);

	if (DistToTarget > ControlledEnemy->LeashDisengageRange || DistFromHome > (ControlledEnemy->LeashDisengageRange * 1.35f))
	{
		CurrentTarget.Reset();
		TransitionToState(EIU_EnemyAIState::Idle);
		return;
	}

	if (DistToTarget <= ControlledEnemy->AttackRange)
	{
		FaceTargetSmoothly(Target, DeltaSeconds, 25.0f);
		TransitionToState(EIU_EnemyAIState::Attack);
		return;
	}

	FaceTargetSmoothly(Target, DeltaSeconds, 14.0f);

	const FVector EnemyLoc = ControlledEnemy->GetActorLocation();
	const FVector TargetLoc = Target->GetActorLocation();
	FVector SteeringTarget = TargetLoc;

	// Use NavMesh corner steering when a Navigation Data instance is present for ground enemies;
	// gracefully fall back to direct vector steering on unbaked greybox/unit-test arenas.
	if (!ControlledEnemy->bCanFlyChase)
	{
		if (UWorld* World = GetWorld())
		{
			if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
			{
				if (NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) != nullptr)
				{
					UNavigationPath* NavPath = NavSys->FindPathToLocationSynchronously(World, EnemyLoc, TargetLoc, ControlledEnemy);
					if (NavPath && NavPath->PathPoints.Num() >= 2)
					{
						SteeringTarget = NavPath->PathPoints[1];
					}
				}
			}
		}
	}

	FVector MoveDir = SteeringTarget - EnemyLoc;
	if (!ControlledEnemy->bCanFlyChase)
	{
		MoveDir.Z = 0.0f;
	}

	MoveDir = MoveDir.GetSafeNormal();
	if (!MoveDir.IsNearlyZero())
	{
		ControlledEnemy->AddMovementInput(MoveDir, 1.0f);
	}
}

void AIU_EnemyAIController::TickAttackState(float DeltaSeconds)
{
	// Safety timeout guard in case windup was externally cleared without state transition
	if (!ControlledEnemy->IsWindingUpAttack() && StateElapsedSeconds > (ControlledEnemy->AttackWindupDuration + 0.10f))
	{
		TransitionToState(EIU_EnemyAIState::Recover);
	}
}

void AIU_EnemyAIController::TickRecoverState(float DeltaSeconds)
{
	AActor* Target = CurrentTarget.Get();
	if (IsValidHostileTarget(Target))
	{
		FaceTargetSmoothly(Target, DeltaSeconds, 8.0f);
	}

	if (StateElapsedSeconds < ControlledEnemy->AttackRecoveryDuration)
	{
		return;
	}

	if (!IsValidHostileTarget(Target) || ComputeEngagementDistance(Target) > ControlledEnemy->LeashDisengageRange)
	{
		CurrentTarget.Reset();
		TransitionToState(EIU_EnemyAIState::Idle);
		return;
	}

	if (ComputeEngagementDistance(Target) <= ControlledEnemy->AttackRange)
	{
		TransitionToState(EIU_EnemyAIState::Attack);
	}
	else
	{
		TransitionToState(EIU_EnemyAIState::Chase);
	}
}

AActor* AIU_EnemyAIController::ScanForHostileTarget() const
{
	if (!ControlledEnemy)
	{
		return nullptr;
	}

	const float MaxDetectRange = ControlledEnemy->DetectionRange;

	if (AActor* OverrideActor = ExplicitTargetOverride.Get())
	{
		if (IsValidHostileTarget(OverrideActor) &&
			ComputeEngagementDistance(OverrideActor) <= MaxDetectRange &&
			HasVisibilityToTarget(OverrideActor))
		{
			return OverrideActor;
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// Fast path: check possessed player controllers first (O(1) in single-player prototype)
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		APawn* CandidatePawn = PC ? PC->GetPawn() : nullptr;
		if (IsValidHostileTarget(CandidatePawn) &&
			ComputeEngagementDistance(CandidatePawn) <= MaxDetectRange &&
			HasVisibilityToTarget(CandidatePawn))
		{
			return CandidatePawn;
		}
	}

	// Fallback for headless unit tests where AIU_PlayerCharacter may be spawned without a PlayerController
	AActor* NearestCandidate = nullptr;
	float BestDist = MaxDetectRange;
	for (TActorIterator<AIU_PlayerCharacter> It(World); It; ++It)
	{
		AIU_PlayerCharacter* Candidate = *It;
		if (IsValidHostileTarget(Candidate))
		{
			const float Dist = ComputeEngagementDistance(Candidate);
			if (Dist <= BestDist && HasVisibilityToTarget(Candidate))
			{
				BestDist = Dist;
				NearestCandidate = Candidate;
			}
		}
	}

	return NearestCandidate;
}

bool AIU_EnemyAIController::IsValidHostileTarget(const AActor* Candidate) const
{
	if (!IsValid(Candidate) || Candidate == ControlledEnemy || Candidate->IsActorBeingDestroyed())
	{
		return false;
	}

	// Never treat other AIU_EnemyCharacter instances as hostile targets
	if (Candidate->IsA<AIU_EnemyCharacter>())
	{
		return false;
	}

	const UIU_AttributeComponent* TargetAttr = Candidate->FindComponentByClass<UIU_AttributeComponent>();
	if (!TargetAttr || TargetAttr->IsDead())
	{
		return false;
	}

	return true;
}

bool AIU_EnemyAIController::HasVisibilityToTarget(const AActor* Candidate) const
{
	if (!ControlledEnemy || !Candidate)
	{
		return false;
	}

	if (!ControlledEnemy->bRequireLineOfSight)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(IUEnemyLOS), false, ControlledEnemy);
	Params.AddIgnoredActor(Candidate);

	FHitResult Hit;
	const FVector EyeStart = ControlledEnemy->GetPawnViewLocation();
	const FVector TargetAim = Candidate->GetActorLocation() + FVector(0.0f, 0.0f, 30.0f);
	return !World->LineTraceSingleByChannel(Hit, EyeStart, TargetAim, ECC_Visibility, Params);
}

float AIU_EnemyAIController::ComputeEngagementDistance(const AActor* Target) const
{
	if (!ControlledEnemy || !Target)
	{
		return FLT_MAX;
	}

	if (ControlledEnemy->bCanFlyChase)
	{
		return FVector::Dist(ControlledEnemy->GetActorLocation(), Target->GetActorLocation());
	}

	// Ground enemies evaluate 2D horizontal distance when target is near ground height,
	// or full 3D distance if player flew high overhead out of melee reach.
	const FVector Delta = Target->GetActorLocation() - ControlledEnemy->GetActorLocation();
	if (FMath::Abs(Delta.Z) <= 140.0f)
	{
		return Delta.Size2D();
	}
	return Delta.Size();
}

void AIU_EnemyAIController::FaceTargetSmoothly(const AActor* Target, float DeltaSeconds, float InterpSpeed)
{
	if (!ControlledEnemy || !Target)
	{
		return;
	}

	const FVector ToTarget = Target->GetActorLocation() - ControlledEnemy->GetActorLocation();
	if (ToTarget.SizeSquared2D() < 1.0f)
	{
		return;
	}

	const FRotator CurrentRot = ControlledEnemy->GetActorRotation();
	const FRotator DesiredRot(0.0f, ToTarget.Rotation().Yaw, 0.0f);
	const FRotator NewRot = FMath::RInterpTo(CurrentRot, DesiredRot, DeltaSeconds, InterpSpeed);
	ControlledEnemy->SetActorRotation(NewRot);
}
