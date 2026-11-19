// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Combat/IU_PracticeDummy.h"
#include "Combat/IU_DamageComponent.h"
#include "Combat/IU_CombatThreatSubsystem.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "TimerManager.h"

AIU_PracticeDummy::AIU_PracticeDummy()
{
	// Zero-Tick when idle; enabled dynamically ONLY during a 0.50s visible training strike swing
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	CapsuleCollision = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CapsuleCollision"));
	CapsuleCollision->InitCapsuleSize(45.0f, 92.0f);
	CapsuleCollision->SetCollisionProfileName(TEXT("Pawn"));
	RootComponent = CapsuleCollision;

	DummyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DummyMesh"));
	DummyMesh->SetupAttachment(CapsuleCollision);
	DummyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DummyMesh->SetRelativeScale3D(FVector(0.75f, 0.75f, 1.75f));

	StrikeFistMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StrikeFistMesh"));
	StrikeFistMesh->SetupAttachment(CapsuleCollision);
	StrikeFistMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StrikeFistMesh->SetRelativeScale3D(FVector(0.85f));
	StrikeFistMesh->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderAsset.Succeeded())
	{
		DummyMesh->SetStaticMesh(CylinderAsset.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereAsset.Succeeded())
	{
		StrikeFistMesh->SetStaticMesh(SphereAsset.Object);
	}

	AttributeComponent = CreateDefaultSubobject<UIU_AttributeComponent>(TEXT("AttributeComponent"));
	AttributeComponent->MaxHP = 100.0f;
	AttributeComponent->MaxKi = 0.0f;
	AttributeComponent->PassiveKiRegenPerSecond = 0.0f;

	DamageComponent = CreateDefaultSubobject<UIU_DamageComponent>(TEXT("DamageComponent"));
}

void AIU_PracticeDummy::BeginPlay()
{
	Super::BeginPlay();

	if (DamageComponent)
	{
		DamageComponent->OnHitReactionStarted.AddDynamic(this, &AIU_PracticeDummy::HandleHitReactionStarted);
		DamageComponent->OnHitReactionRecovered.AddDynamic(this, &AIU_PracticeDummy::HandleHitReactionRecovered);
	}
	if (AttributeComponent)
	{
		AttributeComponent->OnHealthDepleted.AddDynamic(this, &AIU_PracticeDummy::HandleDummyDefeated);
	}

	if (bEnablePeriodicTestStrike)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				PeriodicStrikeTimerHandle,
				this,
				&AIU_PracticeDummy::TryPeriodicTrainingStrike,
				StrikeIntervalSeconds,
				true);
		}
	}
}

void AIU_PracticeDummy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelActiveStrike();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoResetTimerHandle);
		World->GetTimerManager().ClearTimer(PeriodicStrikeTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AIU_PracticeDummy::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bWindingUpStrike)
	{
		SetActorTickEnabled(false);
		return;
	}

	ActiveStrikeElapsed += DeltaTime;
	const float Alpha = FMath::Clamp(ActiveStrikeElapsed / FMath::Max(ActiveStrikeWindupTotal, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	const FVector CurrentFistWorld = FMath::Lerp(StrikeStartWorldPos, StrikeImpactWorldPos, Alpha);
	StrikeFistMesh->SetWorldLocation(CurrentFistWorld);
}

void AIU_PracticeDummy::TryPeriodicTrainingStrike()
{
	if (!bEnablePeriodicTestStrike || bWindingUpStrike || (AttributeComponent && AttributeComponent->IsDead()))
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
	if (!PlayerPawn)
	{
		return;
	}

	const float DistSq = FVector::DistSquared(GetActorLocation(), PlayerPawn->GetActorLocation());
	if (DistSq <= FMath::Square(460.0f))
	{
		TriggerTelegraphedStrike(PlayerPawn->GetActorLocation(), StrikeWindupDuration);
	}
}

void AIU_PracticeDummy::TriggerTelegraphedStrike(const FVector& TargetWorldLocation, float CustomWindupSeconds)
{
	UWorld* World = GetWorld();
	if (!World || (AttributeComponent && AttributeComponent->IsDead()))
	{
		return;
	}

	CancelActiveStrike();

	FVector Dir = (TargetWorldLocation - GetActorLocation());
	Dir.Z = 0.0f;
	Dir = Dir.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = GetActorForwardVector();
	}

	SetActorRotation(FRotator(0.0f, Dir.Rotation().Yaw, 0.0f));

	ActiveStrikeWindupTotal = (CustomWindupSeconds > 0.0f) ? CustomWindupSeconds : StrikeWindupDuration;
	ActiveStrikeElapsed = 0.0f;
	StrikeStartWorldPos = GetActorLocation() + (Dir * 45.0f) + FVector(0.0f, 0.0f, 20.0f);
	const float ClampedDist = FMath::Min(StrikeReachCm, FVector::Dist2D(GetActorLocation(), TargetWorldLocation));
	StrikeImpactWorldPos = GetActorLocation() + (Dir * FMath::Max(85.0f, ClampedDist)) + FVector(0.0f, 0.0f, 20.0f);

	StrikeFistMesh->SetWorldLocation(StrikeStartWorldPos);
	StrikeFistMesh->SetWorldScale3D(FVector((StrikeHitboxRadius * 2.0f) / 100.0f));
	StrikeFistMesh->SetVisibility(true);

	bWindingUpStrike = true;
	SetActorTickEnabled(true);

	if (UIU_CombatThreatSubsystem* ThreatSubsys = World->GetSubsystem<UIU_CombatThreatSubsystem>())
	{
		ActiveThreatId = ThreatSubsys->RegisterMeleeAttackWindup(
			this,
			StrikeStartWorldPos,
			StrikeImpactWorldPos,
			StrikeHitboxRadius,
			ActiveStrikeWindupTotal);
	}

	World->GetTimerManager().SetTimer(
		StrikeImpactTimerHandle,
		this,
		&AIU_PracticeDummy::ExecuteStrikeImpact,
		ActiveStrikeWindupTotal,
		false);
}

void AIU_PracticeDummy::ExecuteStrikeImpact()
{
	UWorld* World = GetWorld();
	if (World)
	{
		if (UIU_CombatThreatSubsystem* ThreatSubsys = World->GetSubsystem<UIU_CombatThreatSubsystem>())
		{
			ThreatSubsys->UnregisterMeleeThreat(ActiveThreatId);
		}
	}
	ActiveThreatId = 0;
	bWindingUpStrike = false;
	StrikeFistMesh->SetVisibility(false);
	SetActorTickEnabled(false);

	if (!World || (AttributeComponent && AttributeComponent->IsDead()))
	{
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IUDummyStrike), false, this);
	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_Pawn);

	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(
		Hits,
		StrikeImpactWorldPos,
		StrikeImpactWorldPos + FVector(0.0f, 0.0f, 1.0f),
		FQuat::Identity,
		ObjParams,
		FCollisionShape::MakeSphere(StrikeHitboxRadius),
		QueryParams);

	TSet<AActor*> HitActors;
	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitActor == this || HitActors.Contains(HitActor))
		{
			continue;
		}
		HitActors.Add(HitActor);

		if (UIU_DamageComponent* TargetDmg = HitActor->FindComponentByClass<UIU_DamageComponent>())
		{
			FIU_DamageSpec Spec;
			Spec.Amount = StrikeDamage;
			Spec.InstigatorActor = this;
			Spec.DamageCauser = this;
			Spec.HitLocation = StrikeImpactWorldPos;
			Spec.HitDirection = (StrikeImpactWorldPos - StrikeStartWorldPos).GetSafeNormal();
			Spec.HitStunDuration = 0.22f;
			TargetDmg->ApplyDamageSpec(Spec);
		}
	}
}

void AIU_PracticeDummy::CancelActiveStrike()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StrikeImpactTimerHandle);
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
	bWindingUpStrike = false;
	if (StrikeFistMesh)
	{
		StrikeFistMesh->SetVisibility(false);
	}
	SetActorTickEnabled(false);
}

void AIU_PracticeDummy::ResetDummy()
{
	CancelActiveStrike();
	if (AttributeComponent)
	{
		AttributeComponent->ResetAttributesToFull();
	}
	CapsuleCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DummyMesh->SetVisibility(true, true);
	DummyMesh->SetRelativeRotation(FRotator::ZeroRotator);
	DummyMesh->SetRelativeScale3D(FVector(0.75f, 0.75f, 1.75f));
}

void AIU_PracticeDummy::HandleHitReactionStarted(const FIU_DamageSpec& Spec)
{
	CancelActiveStrike();
	const FVector LocalDir = GetActorTransform().InverseTransformVectorNoScale(Spec.HitDirection.GetSafeNormal());
	DummyMesh->SetRelativeRotation(FRotator(LocalDir.X * -18.0f, 0.0f, LocalDir.Y * 18.0f));
	DummyMesh->SetRelativeScale3D(FVector(0.84f, 0.84f, 1.62f));
}

void AIU_PracticeDummy::HandleHitReactionRecovered()
{
	if (AttributeComponent && !AttributeComponent->IsDead())
	{
		DummyMesh->SetRelativeRotation(FRotator::ZeroRotator);
		DummyMesh->SetRelativeScale3D(FVector(0.75f, 0.75f, 1.75f));
	}
}

void AIU_PracticeDummy::HandleDummyDefeated(AActor* DepletedActor)
{
	CancelActiveStrike();
	CapsuleCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DummyMesh->SetRelativeRotation(FRotator(-75.0f, 0.0f, 0.0f));
	DummyMesh->SetRelativeScale3D(FVector(0.75f, 0.75f, 0.35f));

	if (bAutoResetAfterDefeat)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				AutoResetTimerHandle,
				this,
				&AIU_PracticeDummy::ResetDummy,
				AutoResetDelaySeconds,
				false);
		}
	}
}
