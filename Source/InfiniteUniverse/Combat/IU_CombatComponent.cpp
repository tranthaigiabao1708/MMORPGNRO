// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Combat/IU_CombatComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Combat/IU_TargetingComponent.h"
#include "Combat/IU_KiProjectile.h"
#include "Attributes/IU_AttributeComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

UIU_CombatComponent::UIU_CombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	KiProjectileClass = AIU_KiProjectile::StaticClass();
}

void UIU_CombatComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureComponentsCached();
}

void UIU_CombatComponent::EnsureComponentsCached()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (!AttributeComp)
	{
		AttributeComp = Owner->FindComponentByClass<UIU_AttributeComponent>();
		if (AttributeComp)
		{
			AttributeComp->OnHealthDepleted.AddUniqueDynamic(this, &UIU_CombatComponent::HandleOwnerHealthDepleted);
		}
	}
	if (!DamageComp)
	{
		DamageComp = Owner->FindComponentByClass<UIU_DamageComponent>();
		if (DamageComp)
		{
			DamageComp->OnHitReactionStarted.AddUniqueDynamic(this, &UIU_CombatComponent::HandleHitReactionStarted);
			DamageComp->OnHitReactionRecovered.AddUniqueDynamic(this, &UIU_CombatComponent::HandleHitReactionRecovered);
		}
	}
	if (!TargetingComp)
	{
		TargetingComp = Owner->FindComponentByClass<UIU_TargetingComponent>();
	}
}

void UIU_CombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		TM.ClearTimer(AttackStepTimerHandle);
		TM.ClearTimer(ComboResetTimerHandle);
		TM.ClearTimer(KiBlastCooldownTimerHandle);
		TM.ClearTimer(DodgeTimerHandle);
		TM.ClearTimer(DodgeCooldownTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

bool UIU_CombatComponent::RequestMeleeAttack()
{
	EnsureComponentsCached();

	// Block invalid overlapping states
	if (CurrentCombatState == EIU_CombatState::Defeated ||
		CurrentCombatState == EIU_CombatState::Dodging ||
		CurrentCombatState == EIU_CombatState::HitReaction)
	{
		return false;
	}

	const int32 MaxComboSteps = FMath::Max(1, ComboStepDamages.Num());

	if (CurrentCombatState == EIU_CombatState::Attacking)
	{
		// Queue sequential next hit cleanly instead of overlapping attacks
		if (CurrentComboStep < MaxComboSteps)
		{
			bQueuedNextComboHit = true;
			return true;
		}
		return false;
	}

	// Currently Idle: advance sequential combo if inside grace window, else start at 1
	int32 NextStep = 1;
	if (UWorld* World = GetWorld())
	{
		if (World->GetTimerManager().IsTimerActive(ComboResetTimerHandle) && CurrentComboStep > 0 && CurrentComboStep < MaxComboSteps)
		{
			NextStep = CurrentComboStep + 1;
		}
		World->GetTimerManager().ClearTimer(ComboResetTimerHandle);
	}

	ExecuteComboStep(NextStep);
	return true;
}

AIU_KiProjectile* UIU_CombatComponent::RequestKiBlast()
{
	EnsureComponentsCached();

	if (CurrentCombatState != EIU_CombatState::Idle || bKiBlastOnCooldown)
	{
		return nullptr;
	}

	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World || !KiProjectileClass)
	{
		return nullptr;
	}

	if (!AttributeComp)
	{
		AttributeComp = Owner->FindComponentByClass<UIU_AttributeComponent>();
	}
	if (!AttributeComp || !AttributeComp->TryConsumeKi(KiBlastKiCost))
	{
		return nullptr;
	}

	// Determine muzzle spawn location (SkeletalMesh socket if present, else character offset)
	FVector MuzzleLocation = Owner->GetActorLocation() + Owner->GetActorRotation().RotateVector(FallbackMuzzleOffset);
	if (const ACharacter* OwnerChar = Cast<ACharacter>(Owner))
	{
		if (const USkeletalMeshComponent* Mesh = OwnerChar->GetMesh())
		{
			if (Mesh->GetSkeletalMeshAsset() && Mesh->DoesSocketExist(KiMuzzleSocketName))
			{
				MuzzleLocation = Mesh->GetSocketLocation(KiMuzzleSocketName);
			}
		}
	}

	const FVector ShootDir = ComputeCombatAimDirection(MuzzleLocation);
	const FTransform SpawnTM(ShootDir.Rotation(), MuzzleLocation);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.Instigator = Cast<APawn>(Owner);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AIU_KiProjectile* Projectile = World->SpawnActor<AIU_KiProjectile>(KiProjectileClass, SpawnTM, SpawnParams);
	if (Projectile)
	{
		Projectile->InitializeProjectile(Owner, ShootDir, KiBlastDamage, KiBlastSpeed);
		bKiBlastOnCooldown = true;
		World->GetTimerManager().SetTimer(
			KiBlastCooldownTimerHandle,
			this,
			&UIU_CombatComponent::ClearKiBlastCooldown,
			KiBlastCooldown,
			false);
	}

	return Projectile;
}

bool UIU_CombatComponent::RequestDodge(const FVector& DesiredWorldDirection)
{
	EnsureComponentsCached();

	if (bDodgeOnCooldown ||
		CurrentCombatState == EIU_CombatState::Defeated ||
		CurrentCombatState == EIU_CombatState::Dodging ||
		CurrentCombatState == EIU_CombatState::HitReaction)
	{
		return false;
	}

	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return false;
	}

	// Cancel any active attack step cleanly into Dodging
	World->GetTimerManager().ClearTimer(AttackStepTimerHandle);
	bQueuedNextComboHit = false;

	SetCombatState(EIU_CombatState::Dodging);
	bDodgeOnCooldown = true;

	if (DamageComp)
	{
		DamageComp->SetInvulnerable(true);
	}

	FVector EvadeDir = DesiredWorldDirection.GetSafeNormal();
	if (EvadeDir.IsNearlyZero())
	{
		EvadeDir = -Owner->GetActorForwardVector();
	}

	if (ACharacter* OwnerChar = Cast<ACharacter>(Owner))
	{
		if (UCharacterMovementComponent* MoveComp = OwnerChar->GetCharacterMovement())
		{
			if (MoveComp->IsMovingOnGround())
			{
				EvadeDir.Z = 0.0f;
				EvadeDir = EvadeDir.GetSafeNormal();
			}
			MoveComp->Velocity = EvadeDir * DodgeBurstSpeed;
		}
	}

	World->GetTimerManager().SetTimer(
		DodgeTimerHandle,
		this,
		&UIU_CombatComponent::OnDodgeFinished,
		DodgeDuration,
		false);

	World->GetTimerManager().SetTimer(
		DodgeCooldownTimerHandle,
		this,
		&UIU_CombatComponent::ClearDodgeCooldown,
		DodgeCooldown,
		false);

	return true;
}

void UIU_CombatComponent::SetCombatState(EIU_CombatState NewState)
{
	if (CurrentCombatState != NewState)
	{
		const EIU_CombatState OldState = CurrentCombatState;
		CurrentCombatState = NewState;
		OnCombatStateChanged.Broadcast(OldState, CurrentCombatState);
	}
}

void UIU_CombatComponent::ExecuteComboStep(int32 StepIndex)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	CurrentComboStep = StepIndex;
	bQueuedNextComboHit = false;
	SetCombatState(EIU_CombatState::Attacking);

	// Orient toward locked target or controller yaw and apply subtle strike lunge (Ground & Air!)
	FVector StrikeDir = Owner->GetActorForwardVector();
	if (TargetingComp && TargetingComp->IsLockedOn())
	{
		const FVector ToTarget = (TargetingComp->GetLockedTargetAimLocation() - Owner->GetActorLocation()).GetSafeNormal();
		if (!ToTarget.IsNearlyZero())
		{
			StrikeDir = ToTarget;
			Owner->SetActorRotation(FRotator(0.0f, StrikeDir.Rotation().Yaw, 0.0f));
		}
	}

	if (ACharacter* OwnerChar = Cast<ACharacter>(Owner))
	{
		if (UCharacterMovementComponent* MoveComp = OwnerChar->GetCharacterMovement())
		{
			FVector LungeVel = StrikeDir * MeleeStepLungeSpeed;
			if (MoveComp->IsMovingOnGround())
			{
				LungeVel.Z = 0.0f;
			}
			MoveComp->Velocity = LungeVel;
		}

		const int32 MontageIdx = StepIndex - 1;
		if (MeleeComboMontages.IsValidIndex(MontageIdx) && MeleeComboMontages[MontageIdx])
		{
			OwnerChar->PlayAnimMontage(MeleeComboMontages[MontageIdx]);
		}
	}

	const float StepDamage = ComboStepDamages.IsValidIndex(StepIndex - 1)
		? ComboStepDamages[StepIndex - 1]
		: 15.0f;

	PerformMeleeSweep(StepDamage);
	OnComboStepExecuted.Broadcast(CurrentComboStep);

	World->GetTimerManager().SetTimer(
		AttackStepTimerHandle,
		this,
		&UIU_CombatComponent::OnAttackStepTimerFinished,
		AttackStepDuration,
		false);
}

void UIU_CombatComponent::PerformMeleeSweep(float StepDamage)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	FVector AttackDir = Owner->GetActorForwardVector();
	if (TargetingComp && TargetingComp->IsLockedOn())
	{
		const FVector ToTarget = (TargetingComp->GetLockedTargetAimLocation() - Owner->GetActorLocation()).GetSafeNormal();
		if (!ToTarget.IsNearlyZero())
		{
			AttackDir = ToTarget;
		}
	}

	const FVector SweepStart = Owner->GetActorLocation() + (AttackDir * 40.0f);
	const FVector SweepEnd = Owner->GetActorLocation() + (AttackDir * MeleeReachDistance);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IUMeleeSweep), false, Owner);
	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(
		Hits,
		SweepStart,
		SweepEnd,
		FQuat::Identity,
		ObjParams,
		FCollisionShape::MakeSphere(MeleeHitSweepRadius),
		QueryParams);

	TSet<AActor*> ProcessedActors;
	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitActor == Owner || ProcessedActors.Contains(HitActor))
		{
			continue;
		}
		ProcessedActors.Add(HitActor);

		if (UIU_DamageComponent* TargetDmg = HitActor->FindComponentByClass<UIU_DamageComponent>())
		{
			FIU_DamageSpec Spec;
			Spec.Amount = StepDamage;
			Spec.InstigatorActor = Owner;
			Spec.DamageCauser = Owner;
			Spec.HitLocation = Hit.ImpactPoint.IsNearlyZero() ? HitActor->GetActorLocation() : FVector(Hit.ImpactPoint);
			Spec.HitDirection = AttackDir;
			Spec.HitStunDuration = (CurrentComboStep >= 3) ? 0.35f : 0.20f;
			TargetDmg->ApplyDamageSpec(Spec);
		}
	}
}

FVector UIU_CombatComponent::ComputeCombatAimDirection(const FVector& MuzzleOrigin) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ForwardVector;
	}

	// Priority 1: Homing aim directly onto locked 3D target
	if (TargetingComp && TargetingComp->IsLockedOn())
	{
		const FVector AimDir = (TargetingComp->GetLockedTargetAimLocation() - MuzzleOrigin).GetSafeNormal();
		if (!AimDir.IsNearlyZero())
		{
			return AimDir;
		}
	}

	// Priority 2: Camera center raycast convergence point
	if (const APawn* OwnerPawn = Cast<APawn>(Owner))
	{
		if (const AController* Ctrl = OwnerPawn->GetController())
		{
			const FVector CamStart = OwnerPawn->GetPawnViewLocation();
			const FVector CamDir = Ctrl->GetControlRotation().Vector();
			const FVector TraceEnd = CamStart + (CamDir * 6000.0f);

			FHitResult AimHit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(IUKiAimTrace), false, Owner);
			FVector TargetPoint = TraceEnd;
			if (GetWorld()->LineTraceSingleByChannel(AimHit, CamStart, TraceEnd, ECC_Visibility, Params))
			{
				if ((AimHit.ImpactPoint - MuzzleOrigin).SizeSquared() > FMath::Square(100.0))
				{
					TargetPoint = AimHit.ImpactPoint;
				}
			}
			return (TargetPoint - MuzzleOrigin).GetSafeNormal();
		}
	}

	return Owner->GetActorForwardVector();
}

void UIU_CombatComponent::OnAttackStepTimerFinished()
{
	if (CurrentCombatState != EIU_CombatState::Attacking)
	{
		return;
	}

	const int32 MaxComboSteps = FMath::Max(1, ComboStepDamages.Num());
	if (bQueuedNextComboHit && CurrentComboStep < MaxComboSteps)
	{
		ExecuteComboStep(CurrentComboStep + 1);
		return;
	}

	SetCombatState(EIU_CombatState::Idle);

	if (CurrentComboStep >= MaxComboSteps)
	{
		CurrentComboStep = 0;
	}
	else if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ComboResetTimerHandle,
			this,
			&UIU_CombatComponent::OnComboResetTimerExpired,
			ComboResetGraceTime,
			false);
	}
}

void UIU_CombatComponent::OnComboResetTimerExpired()
{
	if (CurrentCombatState == EIU_CombatState::Idle)
	{
		CurrentComboStep = 0;
	}
}

void UIU_CombatComponent::OnDodgeFinished()
{
	if (DamageComp)
	{
		DamageComp->SetInvulnerable(false);
	}

	if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* MoveComp = OwnerChar->GetCharacterMovement())
		{
			MoveComp->Velocity *= 0.35f;
		}
	}

	if (CurrentCombatState == EIU_CombatState::Dodging)
	{
		SetCombatState(EIU_CombatState::Idle);
	}
}

void UIU_CombatComponent::ClearKiBlastCooldown()
{
	bKiBlastOnCooldown = false;
}

void UIU_CombatComponent::ClearDodgeCooldown()
{
	bDodgeOnCooldown = false;
}

void UIU_CombatComponent::HandleHitReactionStarted(const FIU_DamageSpec& Spec)
{
	if (CurrentCombatState == EIU_CombatState::Defeated)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AttackStepTimerHandle);
	}
	bQueuedNextComboHit = false;
	SetCombatState(EIU_CombatState::HitReaction);
}

void UIU_CombatComponent::HandleHitReactionRecovered()
{
	if (CurrentCombatState == EIU_CombatState::HitReaction)
	{
		SetCombatState(EIU_CombatState::Idle);
	}
}

void UIU_CombatComponent::HandleOwnerHealthDepleted(AActor* DepletedActor)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AttackStepTimerHandle);
		World->GetTimerManager().ClearTimer(ComboResetTimerHandle);
		World->GetTimerManager().ClearTimer(DodgeTimerHandle);
	}
	if (DamageComp)
	{
		DamageComp->SetInvulnerable(false);
	}
	bQueuedNextComboHit = false;
	SetCombatState(EIU_CombatState::Defeated);
}
