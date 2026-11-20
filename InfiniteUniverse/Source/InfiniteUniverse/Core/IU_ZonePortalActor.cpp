// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_ZonePortalActor.h"
#include "Core/IU_PlayerController.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

AIU_ZonePortalActor::AIU_ZonePortalActor()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;

	TriggerSphere = CreateDefaultSubobject<USphereComponent>(TEXT("TriggerSphere"));
	TriggerSphere->InitSphereRadius(185.0f);
	TriggerSphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	SetRootComponent(TriggerSphere);

	PortalPedestalMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PortalPedestalMesh"));
	PortalPedestalMesh->SetupAttachment(TriggerSphere);
	PortalPedestalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PortalPedestalMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -95.0f));
	PortalPedestalMesh->SetRelativeScale3D(FVector(2.8f, 2.8f, 0.14f));

	PortalEnergyPodMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PortalEnergyPodMesh"));
	PortalEnergyPodMesh->SetupAttachment(TriggerSphere);
	PortalEnergyPodMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PortalEnergyPodMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 18.0f));
	PortalEnergyPodMesh->SetRelativeScale3D(FVector(1.55f, 1.55f, 1.55f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylAsset.Succeeded())
	{
		PortalPedestalMesh->SetStaticMesh(CylAsset.Object);
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphAsset.Succeeded())
	{
		PortalEnergyPodMesh->SetStaticMesh(SphAsset.Object);
	}
}

void AIU_ZonePortalActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AIU_ZonePortalActor, PortalTitle);
	DOREPLIFETIME(AIU_ZonePortalActor, TargetZoneID);
	DOREPLIFETIME(AIU_ZonePortalActor, TargetChannelID);
}

void AIU_ZonePortalActor::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && TriggerSphere)
	{
		TriggerSphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &AIU_ZonePortalActor::HandleTriggerBeginOverlap);
	}
}

void AIU_ZonePortalActor::HandleTriggerBeginOverlap(
	UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!HasAuthority() || !bEnableOverlapAutoTransfer)
	{
		return;
	}
	AIU_PlayerCharacter* Player = Cast<AIU_PlayerCharacter>(OtherActor);
	if (!Player)
	{
		return;
	}
	if (const UIU_AttributeComponent* Attr = Player->GetAttributeComponent())
	{
		if (Attr->IsDead())
		{
			return;
		}
	}

	const UWorld* W = GetWorld();
	const double Now = W ? W->GetTimeSeconds() : 0.0;
	// 4.0s grace window after join/spawn so freshly arrived players don't immediately re-trigger!
	if ((Now - Player->CreationTime) < 4.0)
	{
		return;
	}
	if (const double* LastTime = RecentActorCooldowns.Find(Player))
	{
		if ((Now - *LastTime) < 5.0)
		{
			return;
		}
	}
	RecentActorCooldowns.Add(Player, Now);

	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(Player->GetController()))
	{
		IUPC->Server_RequestZoneTransfer(TargetZoneID, TargetChannelID);
	}
}
