// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Combat/IU_KiProjectile.h"
#include "Combat/IU_DamageComponent.h"
#include "Combat/IU_CombatThreatSubsystem.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Engine/World.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "UObject/ConstructorHelpers.h"

AIU_KiProjectile::AIU_KiProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(CollisionRadius);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionObjectType(ECC_GameTraceChannel1); // KiProjectile channel
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionSphere->SetGenerateOverlapEvents(true);
	RootComponent = CollisionSphere;

	OrbMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OrbMesh"));
	OrbMesh->SetupAttachment(CollisionSphere);
	OrbMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OrbMesh->SetRelativeScale3D(FVector(0.42f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereAsset.Succeeded())
	{
		OrbMesh->SetStaticMesh(SphereAsset.Object);
	}

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionSphere;
	ProjectileMovement->InitialSpeed = ProjectileSpeed;
	ProjectileMovement->MaxSpeed = ProjectileSpeed;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;
	ProjectileMovement->ProjectileGravityScale = 0.0f;
}

void AIU_KiProjectile::BeginPlay()
{
	Super::BeginPlay();

	CollisionSphere->SetSphereRadius(CollisionRadius);
	CollisionSphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &AIU_KiProjectile::HandleSphereOverlap);
	CollisionSphere->OnComponentHit.AddUniqueDynamic(this, &AIU_KiProjectile::HandleSphereHit);

	SetLifeSpan(MaxLifetimeSeconds);
}

void AIU_KiProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UIU_CombatThreatSubsystem* ThreatSubsys = World->GetSubsystem<UIU_CombatThreatSubsystem>())
		{
			ThreatSubsys->UnregisterProjectileThreat(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AIU_KiProjectile::InitializeProjectile(AActor* InShooter, const FVector& ShootDirection, float InDamage, float InSpeed)
{
	ShooterActor = InShooter;
	ProjectileDamage = InDamage;
	ProjectileSpeed = InSpeed;

	if (CollisionSphere)
	{
		CollisionSphere->SetSphereRadius(CollisionRadius);
		CollisionSphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &AIU_KiProjectile::HandleSphereOverlap);
		CollisionSphere->OnComponentHit.AddUniqueDynamic(this, &AIU_KiProjectile::HandleSphereHit);
		if (InShooter)
		{
			CollisionSphere->IgnoreActorWhenMoving(InShooter, true);
		}
	}

	if (ProjectileMovement)
	{
		const FVector NormDir = ShootDirection.GetSafeNormal();
		ProjectileMovement->InitialSpeed = ProjectileSpeed;
		ProjectileMovement->MaxSpeed = ProjectileSpeed;
		ProjectileMovement->Velocity = NormDir * ProjectileSpeed;
	}

	if (UWorld* World = GetWorld())
	{
		if (UIU_CombatThreatSubsystem* ThreatSubsys = World->GetSubsystem<UIU_CombatThreatSubsystem>())
		{
			ThreatSubsys->RegisterProjectileThreat(this);
		}
	}
}

void AIU_KiProjectile::HandleSphereOverlap(
	UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (bHasImpacted || !OtherActor || OtherActor == this || OtherActor == ShooterActor.Get())
	{
		return;
	}

	// Ignore already-dead targets so Ki Blasts fly past defeated bodies cleanly
	if (const UIU_AttributeComponent* TargetAttr = OtherActor->FindComponentByClass<UIU_AttributeComponent>())
	{
		if (TargetAttr->IsDead())
		{
			return;
		}
	}

	if (OtherActor->FindComponentByClass<UIU_DamageComponent>())
	{
		const FVector HitPoint = bFromSweep ? FVector(SweepResult.ImpactPoint) : GetActorLocation();
		ProcessImpact(OtherActor, HitPoint);
	}
}

void AIU_KiProjectile::HandleSphereHit(
	UPrimitiveComponent* HitComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (bHasImpacted || OtherActor == ShooterActor.Get())
	{
		return;
	}

	ProcessImpact(OtherActor, Hit.ImpactPoint);
}

void AIU_KiProjectile::ProcessImpact(AActor* TargetActor, const FVector& ImpactPoint)
{
	if (bHasImpacted)
	{
		return;
	}
	bHasImpacted = true;

	if (UWorld* World = GetWorld())
	{
		if (UIU_CombatThreatSubsystem* ThreatSubsys = World->GetSubsystem<UIU_CombatThreatSubsystem>())
		{
			ThreatSubsys->UnregisterProjectileThreat(this);
		}
	}

	if (TargetActor)
	{
		if (UIU_DamageComponent* DmgComp = TargetActor->FindComponentByClass<UIU_DamageComponent>())
		{
			FIU_DamageSpec Spec;
			Spec.Amount = ProjectileDamage;
			Spec.InstigatorActor = ShooterActor.Get();
			Spec.DamageCauser = this;
			Spec.HitLocation = ImpactPoint;
			Spec.HitDirection = GetVelocity().GetSafeNormal();
			Spec.HitStunDuration = 0.20f;
			DmgComp->ApplyDamageSpec(Spec);
		}
	}

	if (ImpactVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), ImpactVFX, ImpactPoint, GetActorRotation());
	}

	Destroy();
}
