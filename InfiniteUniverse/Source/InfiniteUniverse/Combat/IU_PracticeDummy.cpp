// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Combat/IU_PracticeDummy.h"
#include "Combat/IU_DamageComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "TimerManager.h"

AIU_PracticeDummy::AIU_PracticeDummy()
{
	PrimaryActorTick.bCanEverTick = false;

	CapsuleCollision = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CapsuleCollision"));
	CapsuleCollision->InitCapsuleSize(45.0f, 92.0f);
	CapsuleCollision->SetCollisionProfileName(TEXT("Pawn"));
	RootComponent = CapsuleCollision;

	DummyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DummyMesh"));
	DummyMesh->SetupAttachment(CapsuleCollision);
	DummyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DummyMesh->SetRelativeScale3D(FVector(0.75f, 0.75f, 1.75f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderAsset.Succeeded())
	{
		DummyMesh->SetStaticMesh(CylinderAsset.Object);
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
}

void AIU_PracticeDummy::ResetDummy()
{
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
	// Visual recoil tilt away from impact direction
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
