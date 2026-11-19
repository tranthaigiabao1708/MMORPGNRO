// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "AI/IU_EnemySpawner.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

AIU_EnemySpawner::AIU_EnemySpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	bNetLoadOnClient = false;

	SpawnerRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SpawnerRoot"));
	RootComponent = SpawnerRoot;

	EnemyClass = AIU_EnemyCharacter::StaticClass();
}

void AIU_EnemySpawner::BeginPlay()
{
	Super::BeginPlay();

	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !HasAuthority())
	{
		return;
	}

	if (bSpawnOnBeginPlay)
	{
		RespawnEnemyNow();
	}
}

void AIU_EnemySpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimerHandle);
	}
	if (IsValid(SpawnedEnemy))
	{
		SpawnedEnemy->OnEnemyDefeated.RemoveDynamic(this, &AIU_EnemySpawner::HandleManagedEnemyDefeated);
	}
	Super::EndPlay(EndPlayReason);
}

AIU_EnemyCharacter* AIU_EnemySpawner::RespawnEnemyNow()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !HasAuthority())
	{
		return nullptr;
	}

	World->GetTimerManager().ClearTimer(RespawnTimerHandle);

	const FTransform SpawnXform = GetActorTransform();

	if (IsValid(SpawnedEnemy) && !SpawnedEnemy->IsActorBeingDestroyed())
	{
		SpawnedEnemy->bCanFlyChase = bSpawnAerialFlyer;
		SpawnedEnemy->ResetEnemy(SpawnXform);
		OnEnemySpawnedOrReset.Broadcast(SpawnedEnemy);
		return SpawnedEnemy;
	}

	TSubclassOf<AIU_EnemyCharacter> ClassToSpawn = EnemyClass ? EnemyClass : TSubclassOf<AIU_EnemyCharacter>(AIU_EnemyCharacter::StaticClass());

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	SpawnedEnemy = World->SpawnActor<AIU_EnemyCharacter>(ClassToSpawn, SpawnXform, SpawnParams);
	if (SpawnedEnemy)
	{
		SpawnedEnemy->bCanFlyChase = bSpawnAerialFlyer;
		SpawnedEnemy->ResetEnemy(SpawnXform);
		SpawnedEnemy->OnEnemyDefeated.AddUniqueDynamic(this, &AIU_EnemySpawner::HandleManagedEnemyDefeated);
		OnEnemySpawnedOrReset.Broadcast(SpawnedEnemy);
	}

	return SpawnedEnemy;
}

bool AIU_EnemySpawner::IsRespawnPending() const
{
	if (const UWorld* World = GetWorld())
	{
		return World->GetTimerManager().IsTimerActive(RespawnTimerHandle);
	}
	return false;
}

void AIU_EnemySpawner::HandleManagedEnemyDefeated(AIU_EnemyCharacter* DefeatedEnemy)
{
	const UWorld* WorldCheck = GetWorld();
	if (!bAutoRespawnAfterDefeat || !WorldCheck || WorldCheck->GetNetMode() == NM_Client || !HasAuthority())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RespawnTimerHandle,
			this,
			&AIU_EnemySpawner::OnRespawnTimerFired,
			FMath::Max(RespawnDelaySeconds, 0.1f),
			false);
	}
}

void AIU_EnemySpawner::OnRespawnTimerFired()
{
	RespawnEnemyNow();
}
