// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Characters/Enemy/IU_EnemyCharacter.h"
#include "AI/IU_EnemyAIController.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Combat/IU_CombatThreatSubsystem.h"
#include "Characters/IU_SkeletalAnimDirectorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "InfiniteUniverse.h"

AIU_EnemyCharacter::AIU_EnemyCharacter()
{
	// Zero-Tick on the character actor when not swinging; AI decisions tick in AIU_EnemyAIController
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	GetCapsuleComponent()->InitCapsuleSize(44.0f, 92.0f);
	GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	AIControllerClass = AIU_EnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// Position SkeletalMesh at bottom of capsule facing +X
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -92.0f));
	GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

	PlaceholderBodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBodyMesh"));
	PlaceholderBodyMesh->SetupAttachment(GetCapsuleComponent());
	PlaceholderBodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -14.0f));
	PlaceholderBodyMesh->SetRelativeScale3D(FVector(0.76f, 0.76f, 1.55f));

	PlaceholderHeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderHeadMesh"));
	PlaceholderHeadMesh->SetupAttachment(PlaceholderBodyMesh);
	PlaceholderHeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderHeadMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 62.0f));
	PlaceholderHeadMesh->SetRelativeScale3D(FVector(0.62f, 0.62f, 0.32f));

	StrikeFistMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StrikeFistMesh"));
	StrikeFistMesh->SetupAttachment(GetCapsuleComponent());
	StrikeFistMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StrikeFistMesh->SetRelativeScale3D(FVector(0.90f));
	StrikeFistMesh->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderAsset.Succeeded())
	{
		PlaceholderBodyMesh->SetStaticMesh(CylinderAsset.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereAsset.Succeeded())
	{
		PlaceholderHeadMesh->SetStaticMesh(SphereAsset.Object);
		StrikeFistMesh->SetStaticMesh(SphereAsset.Object);
	}

	AttributeComponent = CreateDefaultSubobject<UIU_AttributeComponent>(TEXT("AttributeComponent"));
	AttributeComponent->MaxHP = 120.0f;
	AttributeComponent->MaxKi = 60.0f;
	AttributeComponent->PassiveKiRegenPerSecond = 5.0f;

	DamageComponent = CreateDefaultSubobject<UIU_DamageComponent>(TEXT("DamageComponent"));
	DamageComponent->HitKnockbackSpeed = 220.0f;

	AnimDirectorComponent = CreateDefaultSubobject<UIU_SkeletalAnimDirectorComponent>(TEXT("AnimDirectorComponent"));

	ApplyDefaultMovementConfig();
}

void AIU_EnemyCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (DamageComponent)
	{
		DamageComponent->OnHitReactionStarted.AddUniqueDynamic(this, &AIU_EnemyCharacter::HandleHitReactionStarted);
		DamageComponent->OnHitReactionRecovered.AddUniqueDynamic(this, &AIU_EnemyCharacter::HandleHitReactionRecovered);
	}

	if (AttributeComponent)
	{
		AttributeComponent->OnHealthDepleted.AddUniqueDynamic(this, &AIU_EnemyCharacter::HandleHealthDepleted);
	}
}

void AIU_EnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	HomeSpawnTransform = GetActorTransform();
	ApplyDefaultMovementConfig();

	if (bAutoLoadDefaultAnimeMesh && GetMesh() && !GetMesh()->GetSkeletalMeshAsset())
	{
		if (USkeletalMesh* DefaultEnemyMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Enemy/Meshes/SK_Enemy.SK_Enemy")))
		{
			GetMesh()->SetSkeletalMeshAsset(DefaultEnemyMesh);
		}
	}

	if (StrikeFistMesh)
	{
		if (UMaterialInterface* CrimsonGlowMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Characters/Enemy/Materials/M_EnemyVisor.M_EnemyVisor")))
		{
			StrikeFistMesh->SetMaterial(0, CrimsonGlowMat);
		}
	}

	UpdatePlaceholderVisibility();
	if (AnimDirectorComponent)
	{
		AnimDirectorComponent->RefreshMeshAndBindings();
	}

	// Ensure an AIU_EnemyAIController possesses this enemy even in bare transient test worlds
	if (!GetController() && !IsDead())
	{
		SpawnDefaultController();
	}
}

void AIU_EnemyCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelActiveAttack();
	Super::EndPlay(EndPlayReason);
}

void AIU_EnemyCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bWindingUpAttack || IsDead())
	{
		SetActorTickEnabled(false);
		return;
	}

	ActiveWindupElapsed += DeltaTime;
	const float Alpha = FMath::Clamp(ActiveWindupElapsed / FMath::Max(ActiveWindupTotal, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	const FVector CurrentFistPos = FMath::Lerp(StrikeStartWorldPos, StrikeImpactWorldPos, Alpha);
	StrikeFistMesh->SetWorldLocation(CurrentFistPos);
}

bool AIU_EnemyCharacter::StartTelegraphedAttack(AActor* TargetActor)
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(TargetActor) || IsDead())
	{
		return false;
	}

	CancelActiveAttack();

	const FVector MyLoc = GetActorLocation();
	const FVector TargetLoc = TargetActor->GetActorLocation();

	FVector StrikeDir = TargetLoc - MyLoc;
	if (!bCanFlyChase)
	{
		StrikeDir.Z = 0.0f;
	}
	StrikeDir = StrikeDir.GetSafeNormal();
	if (StrikeDir.IsNearlyZero())
	{
		StrikeDir = GetActorForwardVector();
	}

	SetActorRotation(FRotator(0.0f, StrikeDir.Rotation().Yaw, 0.0f));

	ActiveWindupTotal = FMath::Max(AttackWindupDuration, 0.05f);
	ActiveWindupElapsed = 0.0f;

	const float DistToTarget = bCanFlyChase ? FVector::Dist(MyLoc, TargetLoc) : FVector::Dist2D(MyLoc, TargetLoc);
	const float ClampedReach = FMath::Clamp(DistToTarget, 80.0f, AttackReachCm);

	StrikeStartWorldPos = MyLoc + (StrikeDir * 46.0f) + FVector(0.0f, 0.0f, 20.0f);
	StrikeImpactWorldPos = MyLoc + (StrikeDir * ClampedReach) + FVector(0.0f, 0.0f, 20.0f);

	StrikeFistMesh->SetWorldLocation(StrikeStartWorldPos);
	StrikeFistMesh->SetWorldScale3D(FVector((AttackHitboxRadius * 2.0f) / 100.0f));
	StrikeFistMesh->SetVisibility(true);

	bWindingUpAttack = true;
	SetActorTickEnabled(true);

	// Register live melee windup with UIU_CombatThreatSubsystem so Player RMB Dodge triggers Perfect Evade
	if (UIU_CombatThreatSubsystem* ThreatSubsys = World->GetSubsystem<UIU_CombatThreatSubsystem>())
	{
		ActiveThreatId = ThreatSubsys->RegisterMeleeAttackWindup(
			this,
			StrikeStartWorldPos,
			StrikeImpactWorldPos,
			AttackHitboxRadius,
			ActiveWindupTotal,
			StrikeFistMesh);
	}

	World->GetTimerManager().SetTimer(
		AttackWindupTimerHandle,
		this,
		&AIU_EnemyCharacter::ExecuteAttackImpact,
		ActiveWindupTotal,
		false);

	return true;
}

void AIU_EnemyCharacter::CancelActiveAttack()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AttackWindupTimerHandle);
		if (UIU_CombatThreatSubsystem* ThreatSubsys = World->GetSubsystem<UIU_CombatThreatSubsystem>())
		{
			if (ActiveThreatId > 0)
			{
				ThreatSubsys->UnregisterMeleeThreat(ActiveThreatId);
			}
			ThreatSubsys->CancelThreatsFromAttacker(this);
		}
	}

	ActiveThreatId = 0;
	bWindingUpAttack = false;
	ActiveWindupElapsed = 0.0f;

	if (StrikeFistMesh)
	{
		StrikeFistMesh->SetVisibility(false);
	}
	SetActorTickEnabled(false);
}

void AIU_EnemyCharacter::ExecuteAttackImpact()
{
	UWorld* World = GetWorld();
	if (World)
	{
		if (UIU_CombatThreatSubsystem* ThreatSubsys = World->GetSubsystem<UIU_CombatThreatSubsystem>())
		{
			if (ActiveThreatId > 0)
			{
				ThreatSubsys->UnregisterMeleeThreat(ActiveThreatId);
			}
		}
	}

	ActiveThreatId = 0;
	bWindingUpAttack = false;
	if (StrikeFistMesh)
	{
		StrikeFistMesh->SetVisibility(false);
	}
	SetActorTickEnabled(false);

	// Strict guard: never deal damage if defeated or interrupted out of Attack state
	if (!World || IsDead())
	{
		return;
	}

	AIU_EnemyAIController* EnemyAI = GetEnemyAIController();
	if (EnemyAI && EnemyAI->GetAIState() != EIU_EnemyAIState::Attack)
	{
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IUEnemyMeleeStrike), false, this);
	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_Pawn);

	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(
		Hits,
		StrikeImpactWorldPos,
		StrikeImpactWorldPos + FVector(0.0f, 0.0f, 1.0f),
		FQuat::Identity,
		ObjParams,
		FCollisionShape::MakeSphere(AttackHitboxRadius),
		QueryParams);

	TSet<AActor*> HitActors;
	const FVector SwingDir = (StrikeImpactWorldPos - StrikeStartWorldPos).GetSafeNormal();

	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!IsValid(HitActor) || HitActor == this || HitActors.Contains(HitActor))
		{
			continue;
		}

		// Prevent enemy-on-enemy friendly fire
		if (HitActor->IsA<AIU_EnemyCharacter>())
		{
			continue;
		}

		HitActors.Add(HitActor);

		if (UIU_DamageComponent* TargetDmg = HitActor->FindComponentByClass<UIU_DamageComponent>())
		{
			FIU_DamageSpec Spec;
			Spec.Amount = AttackDamage;
			Spec.InstigatorActor = this;
			Spec.DamageCauser = this;
			Spec.HitLocation = StrikeImpactWorldPos;
			Spec.HitDirection = SwingDir.IsNearlyZero() ? GetActorForwardVector() : SwingDir;
			Spec.HitStunDuration = AttackHitStunDuration;
			TargetDmg->ApplyDamageSpec(Spec);
		}
	}

	if (EnemyAI)
	{
		EnemyAI->NotifyAttackImpactFinished();
	}
}

void AIU_EnemyCharacter::ResetEnemy(const FTransform& NewSpawnTransform)
{
	CancelActiveAttack();
	HomeSpawnTransform = NewSpawnTransform;
	LastDamageInstigator.Reset();

	if (AttributeComponent)
	{
		AttributeComponent->ResetAttributesToFull();
	}
	if (DamageComponent)
	{
		DamageComponent->ResetDamageState();
	}

	GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	TeleportTo(NewSpawnTransform.GetLocation(), NewSpawnTransform.GetRotation().Rotator(), false, true);

	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		ApplyDefaultMovementConfig();
		CMC->StopMovementImmediately();
		CMC->SetMovementMode(bCanFlyChase ? MOVE_Flying : MOVE_Walking);
	}

	if (PlaceholderBodyMesh)
	{
		PlaceholderBodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -14.0f));
		PlaceholderBodyMesh->SetRelativeRotation(FRotator::ZeroRotator);
		PlaceholderBodyMesh->SetRelativeScale3D(FVector(0.76f, 0.76f, 1.55f));
	}

	UpdatePlaceholderVisibility();
	if (AnimDirectorComponent)
	{
		AnimDirectorComponent->ResetAnimDirector();
	}

	if (!GetController())
	{
		SpawnDefaultController();
	}

	if (AIU_EnemyAIController* EnemyAI = GetEnemyAIController())
	{
		EnemyAI->ResetToIdle(NewSpawnTransform.GetLocation());
	}

	OnEnemyReset.Broadcast(this);
}

void AIU_EnemyCharacter::ResetEnemyAtHome()
{
	ResetEnemy(HomeSpawnTransform);
}

AIU_EnemyAIController* AIU_EnemyCharacter::GetEnemyAIController() const
{
	return Cast<AIU_EnemyAIController>(GetController());
}

EIU_EnemyAIState AIU_EnemyCharacter::GetEnemyAIState() const
{
	if (IsDead())
	{
		return EIU_EnemyAIState::Dead;
	}
	if (const AIU_EnemyAIController* EnemyAI = GetEnemyAIController())
	{
		return EnemyAI->GetAIState();
	}
	return EIU_EnemyAIState::Idle;
}

bool AIU_EnemyCharacter::IsDead() const
{
	return AttributeComponent && AttributeComponent->IsDead();
}

void AIU_EnemyCharacter::HandleHitReactionStarted(const FIU_DamageSpec& Spec)
{
	if (IsDead())
	{
		return;
	}

	CancelActiveAttack();
	LastDamageInstigator = Spec.InstigatorActor;

	if (PlaceholderBodyMesh)
	{
		const FVector LocalDir = GetActorTransform().InverseTransformVectorNoScale(Spec.HitDirection.GetSafeNormal());
		PlaceholderBodyMesh->SetRelativeRotation(FRotator(LocalDir.X * -18.0f, 0.0f, LocalDir.Y * 18.0f));
		PlaceholderBodyMesh->SetRelativeScale3D(FVector(0.84f, 0.84f, 1.44f));
	}

	if (AIU_EnemyAIController* EnemyAI = GetEnemyAIController())
	{
		EnemyAI->NotifyHitReactionStarted(Spec.InstigatorActor.Get());
	}
}

void AIU_EnemyCharacter::HandleHitReactionRecovered()
{
	if (IsDead())
	{
		return;
	}

	if (PlaceholderBodyMesh)
	{
		PlaceholderBodyMesh->SetRelativeRotation(FRotator::ZeroRotator);
		PlaceholderBodyMesh->SetRelativeScale3D(FVector(0.76f, 0.76f, 1.55f));
	}

	if (AIU_EnemyAIController* EnemyAI = GetEnemyAIController())
	{
		EnemyAI->NotifyHitReactionRecovered();
	}
}

void AIU_EnemyCharacter::HandleHealthDepleted(AActor* DepletedActor)
{
	CancelActiveAttack();

	if (AIU_EnemyAIController* EnemyAI = GetEnemyAIController())
	{
		EnemyAI->NotifyEnemyDied();
	}

	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->StopMovementImmediately();
		CMC->DisableMovement();
	}

	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (PlaceholderBodyMesh)
	{
		PlaceholderBodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -68.0f));
		PlaceholderBodyMesh->SetRelativeRotation(FRotator(-85.0f, 0.0f, 0.0f));
		PlaceholderBodyMesh->SetRelativeScale3D(FVector(0.76f, 0.76f, 0.45f));
	}

	UE_LOG(LogIUAI, Verbose, TEXT("[%s] Defeated (0 HP) -> transitioned to Dead state"), *GetName());
	OnEnemyDefeated.Broadcast(this);
}

void AIU_EnemyCharacter::ApplyDefaultMovementConfig()
{
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->bOrientRotationToMovement = false;
		CMC->MaxWalkSpeed = ChaseMoveSpeed;
		CMC->MaxFlySpeed = ChaseMoveSpeed;
		CMC->MaxAcceleration = 2200.0f;
		CMC->BrakingDecelerationWalking = 2000.0f;
		CMC->BrakingDecelerationFlying = 2200.0f;
		CMC->NavAgentProps.bCanFly = true;
		if (bCanFlyChase && HasActorBegunPlay() && !IsDead())
		{
			CMC->SetMovementMode(MOVE_Flying);
		}
	}
}

void AIU_EnemyCharacter::UpdatePlaceholderVisibility()
{
	const bool bHasSkeletalMesh = (GetMesh() && GetMesh()->GetSkeletalMeshAsset() != nullptr);
	if (bHasSkeletalMesh && GetMesh())
	{
		GetMesh()->SetVisibility(true, true);
	}
	if (PlaceholderBodyMesh)
	{
		PlaceholderBodyMesh->SetVisibility(!bHasSkeletalMesh, true);
	}
	if (PlaceholderHeadMesh)
	{
		PlaceholderHeadMesh->SetVisibility(!bHasSkeletalMesh, true);
	}
}
