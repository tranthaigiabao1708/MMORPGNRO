// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Combat/IU_CombatComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Combat/IU_TargetingComponent.h"
#include "Combat/IU_KiProjectile.h"
#include "Combat/IU_AfterimageGhost.h"
#include "Combat/IU_CombatThreatSubsystem.h"
#include "Attributes/IU_AttributeComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

UIU_CombatComponent::UIU_CombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	KiProjectileClass = AIU_KiProjectile::StaticClass();
	AfterimageGhostClass = AIU_AfterimageGhost::StaticClass();
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
	CancelPendingMeleeWindup();
	SetDashZeroGravityOverride(false);
	SetOwnerPawnCollisionIgnored(false);
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		TM.ClearTimer(MeleeImpactTimerHandle);
		TM.ClearTimer(AttackStepTimerHandle);
		TM.ClearTimer(ComboResetTimerHandle);
		TM.ClearTimer(KiBlastCooldownTimerHandle);
		TM.ClearTimer(DodgeTimerHandle);
		TM.ClearTimer(DodgeCooldownTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void UIU_CombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bActiveKinematicDash || CurrentCombatState != EIU_CombatState::Dodging)
	{
		bActiveKinematicDash = false;
		SetComponentTickEnabled(false);
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner || RemainingDashDistance <= KINDA_SMALL_NUMBER)
	{
		bActiveKinematicDash = false;
		SetComponentTickEnabled(false);
		return;
	}

	const float StepDist = FMath::Min(RemainingDashDistance, ActiveDashSpeed * DeltaTime);
	FHitResult SweepHit;
	Owner->SetActorLocation(Owner->GetActorLocation() + (ActiveDashDirection * StepDist), /*bSweep=*/true, &SweepHit);
	RemainingDashDistance -= StepDist;

	if (ACharacter* OwnerChar = Cast<ACharacter>(Owner))
	{
		if (UCharacterMovementComponent* MoveComp = OwnerChar->GetCharacterMovement())
		{
			MoveComp->Velocity = FVector::ZeroVector;
		}
	}

	if (SweepHit.bBlockingHit || RemainingDashDistance <= KINDA_SMALL_NUMBER)
	{
		RemainingDashDistance = 0.0f;
		bActiveKinematicDash = false;
		SetComponentTickEnabled(false);
	}
}

void UIU_CombatComponent::SetMobilityStyle(EIU_MobilityStepStyle NewStyle)
{
	if (MobilityStyle != NewStyle)
	{
		MobilityStyle = NewStyle;
		OnMobilityStyleChanged.Broadcast(MobilityStyle);
	}
}

EIU_MobilityStepStyle UIU_CombatComponent::ToggleMobilityStyle()
{
	const EIU_MobilityStepStyle NextStyle = (MobilityStyle == EIU_MobilityStepStyle::Dash)
		? EIU_MobilityStepStyle::InstantBlink
		: EIU_MobilityStepStyle::Dash;
	SetMobilityStyle(NextStyle);
	return MobilityStyle;
}

bool UIU_CombatComponent::RequestMeleeAttack()
{
	EnsureComponentsCached();

	if (CurrentCombatState == EIU_CombatState::Defeated ||
		CurrentCombatState == EIU_CombatState::Dodging ||
		CurrentCombatState == EIU_CombatState::HitReaction)
	{
		return false;
	}

	const int32 MaxComboSteps = FMath::Max(1, ComboStepDamages.Num());

	if (CurrentCombatState == EIU_CombatState::Attacking)
	{
		if (CurrentComboStep < MaxComboSteps)
		{
			bQueuedNextComboHit = true;
			return true;
		}
		return false;
	}

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

	// Cancel any active melee windup/swing cleanly when cancelling into Dash/Evade
	CancelPendingMeleeWindup();
	World->GetTimerManager().ClearTimer(AttackStepTimerHandle);
	bQueuedNextComboHit = false;
	LastSpawnedAfterimage.Reset();

	ACharacter* OwnerChar = Cast<ACharacter>(Owner);
	UCharacterMovementComponent* MoveComp = OwnerChar ? OwnerChar->GetCharacterMovement() : nullptr;
	const bool bGrounded = (MoveComp && MoveComp->IsMovingOnGround());
	const FTransform OriginTransform = Owner->GetActorTransform();

	// 1. Compute unified A -> B direction (strictly identical across Dash, InstantBlink, and Perfect Evade!)
	FVector TravelDir = DesiredWorldDirection.GetSafeNormal();
	if (TravelDir.IsNearlyZero())
	{
		if (const APawn* OwnerPawn = Cast<APawn>(Owner))
		{
			if (const AController* Ctrl = OwnerPawn->GetController())
			{
				const FRotator AimRot = bGrounded
					? FRotator(0.0f, Ctrl->GetControlRotation().Yaw, 0.0f)
					: Ctrl->GetControlRotation();
				TravelDir = AimRot.Vector();
			}
		}
		if (TravelDir.IsNearlyZero())
		{
			TravelDir = Owner->GetActorForwardVector();
		}
	}

	if (bGrounded)
	{
		TravelDir.Z = 0.0f;
		TravelDir = TravelDir.GetSafeNormal();
	}
	if (TravelDir.IsNearlyZero())
	{
		TravelDir = Owner->GetActorForwardVector();
	}

	// 2. Check STRICT Imminent-Damage Condition ("sắp gây sát thương" from Monster, Boss, OR Rival Player):
	//    True ONLY if an active attack is <= PerfectEvadeTimeWindow (0.30s) from hitting OR < PerfectEvadePhysicalGapCm (20 cm)!
	UIU_CombatThreatSubsystem* ThreatSubsys = World->GetSubsystem<UIU_CombatThreatSubsystem>();
	const bool bImminentIncomingAttack = (ThreatSubsys != nullptr) &&
		ThreatSubsys->HasImminentThreatAgainstActor(Owner, PerfectEvadeTimeWindow, PerfectEvadePhysicalGapCm);

	bLastDodgeWasCombatEvade = bImminentIncomingAttack;
	bLastDodgeBlinkedInstantly = (bImminentIncomingAttack || MobilityStyle == EIU_MobilityStepStyle::InstantBlink);

	// 3. ONLY Perfect Evade ("Né") leaves a Black Myth: Wukong style Afterimage Ghost at origin A & grants i-frames!
	if (bImminentIncomingAttack)
	{
		LastSpawnedAfterimage = SpawnMirageAfterimageAt(OriginTransform);
		if (DamageComp)
		{
			DamageComp->SetInvulnerable(true);
		}
	}

	// Temporarily ignore Pawn-vs-Pawn blocking so A -> B traversal never snags on capsules
	SetOwnerPawnCollisionIgnored(true);
	bDodgeOnCooldown = true;
	SetCombatState(EIU_CombatState::Dodging);

	if (MoveComp)
	{
		MoveComp->Velocity = FVector::ZeroVector;
	}

	// 4. Execute A -> B traversal (arrives at the EXACT SAME B whether Dash, InstantBlink, or Perfect Evade!)
	if (bLastDodgeBlinkedInstantly)
	{
		bActiveKinematicDash = false;
		SetComponentTickEnabled(false);

		FHitResult SweepHit;
		Owner->SetActorLocation(OriginTransform.GetLocation() + (TravelDir * MobilityTravelDistance), /*bSweep=*/true, &SweepHit);
	}
	else
	{
		bActiveKinematicDash = true;
		ActiveDashDirection = TravelDir;
		RemainingDashDistance = MobilityTravelDistance;
		ActiveDashSpeed = MobilityTravelDistance / FMath::Max(DodgeDuration, 0.05f);
		SetDashZeroGravityOverride(true);
		SetComponentTickEnabled(true);
	}

	// 5. Orient character yaw (keep facing locked target if locked-on, else face travel direction)
	if (TargetingComp && TargetingComp->IsLockedOn())
	{
		if (const AActor* Locked = TargetingComp->GetLockedTarget())
		{
			const FVector ExpectedEndLoc = OriginTransform.GetLocation() + (TravelDir * MobilityTravelDistance);
			const FVector ToLocked = Locked->GetActorLocation() - ExpectedEndLoc;
			if (!ToLocked.IsNearlyZero(1.0f))
			{
				Owner->SetActorRotation(FRotator(0.0f, ToLocked.Rotation().Yaw, 0.0f));
			}
		}
	}
	else
	{
		const FVector PlanarDir(TravelDir.X, TravelDir.Y, 0.0f);
		if (!PlanarDir.IsNearlyZero(0.01f))
		{
			Owner->SetActorRotation(FRotator(0.0f, PlanarDir.Rotation().Yaw, 0.0f));
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

void UIU_CombatComponent::SetOwnerPawnCollisionIgnored(bool bIgnorePawns)
{
	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	UCapsuleComponent* Capsule = OwnerChar ? OwnerChar->GetCapsuleComponent() : nullptr;
	if (!Capsule)
	{
		return;
	}

	if (bIgnorePawns && !bPawnCollisionTemporarilyIgnored)
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		bPawnCollisionTemporarilyIgnored = true;
	}
	if (!bIgnorePawns && bPawnCollisionTemporarilyIgnored)
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		bPawnCollisionTemporarilyIgnored = false;
	}
}

void UIU_CombatComponent::SetDashZeroGravityOverride(bool bEnableZeroGravity)
{
	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* MoveComp = OwnerChar ? OwnerChar->GetCharacterMovement() : nullptr;
	if (!MoveComp)
	{
		return;
	}

	if (bEnableZeroGravity && !bDashGravityOverridden)
	{
		SavedDashGravityScale = MoveComp->GravityScale;
		MoveComp->GravityScale = 0.0f;
		MoveComp->Velocity = FVector::ZeroVector;
		bDashGravityOverridden = true;
	}
	else if (!bEnableZeroGravity && bDashGravityOverridden)
	{
		MoveComp->GravityScale = SavedDashGravityScale;
		bDashGravityOverridden = false;
	}
}

AIU_AfterimageGhost* UIU_CombatComponent::SpawnMirageAfterimageAt(const FTransform& OriginTransform)
{
	UWorld* World = GetWorld();
	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!World || !OwnerChar)
	{
		return nullptr;
	}

	TSubclassOf<AIU_AfterimageGhost> GhostClassToSpawn = AfterimageGhostClass;
	if (!GhostClassToSpawn)
	{
		GhostClassToSpawn = AIU_AfterimageGhost::StaticClass();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerChar;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AIU_AfterimageGhost* Ghost = World->SpawnActor<AIU_AfterimageGhost>(GhostClassToSpawn, OriginTransform, SpawnParams);
	if (Ghost)
	{
		Ghost->InitializeFromCharacter(OwnerChar, MirageAfterimageLifetime);
	}
	return Ghost;
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

	CancelPendingMeleeWindup();
	CurrentComboStep = StepIndex;
	bQueuedNextComboHit = false;
	SetCombatState(EIU_CombatState::Attacking);

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

	PendingMeleeStepDamage = ComboStepDamages.IsValidIndex(StepIndex - 1)
		? ComboStepDamages[StepIndex - 1]
		: 15.0f;

	const float RawWindup = ComboStepWindupTimes.IsValidIndex(StepIndex - 1)
		? ComboStepWindupTimes[StepIndex - 1]
		: 0.11f;
	const float EffectiveWindup = FMath::Clamp(RawWindup, 0.0f, FMath::Max(0.0f, AttackStepDuration - 0.03f));

	OnComboStepExecuted.Broadcast(CurrentComboStep);

	if (EffectiveWindup > KINDA_SMALL_NUMBER)
	{
		// Register melee windup threat so an opposing Player (PvP) or AI can Perfect-Evade right before impact!
		if (UIU_CombatThreatSubsystem* ThreatSubsys = World->GetSubsystem<UIU_CombatThreatSubsystem>())
		{
			const FVector WindupStartPos = Owner->GetActorLocation() + (StrikeDir * 45.0f);
			const FVector ImpactTargetPos = Owner->GetActorLocation() + (StrikeDir * FMath::Max(60.0f, MeleeReachDistance - MeleeHitSweepRadius * 0.35f));
			ActiveMeleeThreatId = ThreatSubsys->RegisterMeleeAttackWindup(
				Owner,
				WindupStartPos,
				ImpactTargetPos,
				MeleeHitSweepRadius,
				EffectiveWindup);
		}

		World->GetTimerManager().SetTimer(
			MeleeImpactTimerHandle,
			this,
			&UIU_CombatComponent::OnMeleeImpactTimerFired,
			EffectiveWindup,
			false);
	}
	else
	{
		PerformMeleeSweep(PendingMeleeStepDamage);
	}

	World->GetTimerManager().SetTimer(
		AttackStepTimerHandle,
		this,
		&UIU_CombatComponent::OnAttackStepTimerFinished,
		AttackStepDuration,
		false);
}

void UIU_CombatComponent::OnMeleeImpactTimerFired()
{
	if (UWorld* World = GetWorld())
	{
		if (UIU_CombatThreatSubsystem* ThreatSubsys = World->GetSubsystem<UIU_CombatThreatSubsystem>())
		{
			ThreatSubsys->UnregisterMeleeThreat(ActiveMeleeThreatId);
		}
	}
	ActiveMeleeThreatId = 0;

	if (CurrentCombatState == EIU_CombatState::Attacking)
	{
		PerformMeleeSweep(PendingMeleeStepDamage);
	}
}

void UIU_CombatComponent::CancelPendingMeleeWindup()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MeleeImpactTimerHandle);
		if (UIU_CombatThreatSubsystem* ThreatSubsys = World->GetSubsystem<UIU_CombatThreatSubsystem>())
		{
			if (ActiveMeleeThreatId > 0)
			{
				ThreatSubsys->UnregisterMeleeThreat(ActiveMeleeThreatId);
			}
			ThreatSubsys->CancelThreatsFromAttacker(GetOwner());
		}
	}
	ActiveMeleeThreatId = 0;
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

	if (TargetingComp && TargetingComp->IsLockedOn())
	{
		const FVector AimDir = (TargetingComp->GetLockedTargetAimLocation() - MuzzleOrigin).GetSafeNormal();
		if (!AimDir.IsNearlyZero())
		{
			return AimDir;
		}
	}

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
	if (bActiveKinematicDash && RemainingDashDistance > 0.01f)
	{
		if (AActor* Owner = GetOwner())
		{
			FHitResult SweepHit;
			Owner->SetActorLocation(Owner->GetActorLocation() + (ActiveDashDirection * RemainingDashDistance), /*bSweep=*/true, &SweepHit);
		}
	}

	bActiveKinematicDash = false;
	RemainingDashDistance = 0.0f;
	SetComponentTickEnabled(false);

	SetDashZeroGravityOverride(false);
	SetOwnerPawnCollisionIgnored(false);

	if (DamageComp)
	{
		DamageComp->SetInvulnerable(false);
	}

	if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* MoveComp = OwnerChar->GetCharacterMovement())
		{
			MoveComp->Velocity = FVector::ZeroVector;
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

	CancelPendingMeleeWindup();
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
	CancelPendingMeleeWindup();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AttackStepTimerHandle);
		World->GetTimerManager().ClearTimer(ComboResetTimerHandle);
		World->GetTimerManager().ClearTimer(DodgeTimerHandle);
	}
	bActiveKinematicDash = false;
	RemainingDashDistance = 0.0f;
	SetComponentTickEnabled(false);
	SetDashZeroGravityOverride(false);
	SetOwnerPawnCollisionIgnored(false);
	if (DamageComp)
	{
		DamageComp->SetInvulnerable(false);
	}
	bQueuedNextComboHit = false;
	SetCombatState(EIU_CombatState::Defeated);
}
