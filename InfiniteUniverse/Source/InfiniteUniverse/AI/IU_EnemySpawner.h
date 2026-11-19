// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IU_EnemySpawner.generated.h"

class USceneComponent;
class AIU_EnemyCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIU_OnEnemySpawnedSignature, AIU_EnemyCharacter*, SpawnedEnemy);

/**
 * Zero-Tick Enemy Spawner & Respawn Manager for Milestone 4 testing (P4-08).
 * Spawns an AIU_EnemyCharacter on BeginPlay and automatically resets/respawns it after defeat.
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_EnemySpawner : public AActor
{
	GENERATED_BODY()

public:
	AIU_EnemySpawner();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Enemy class to spawn (defaults to AIU_EnemyCharacter; can be overridden with BP_EnemyCharacter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Spawner")
	TSubclassOf<AIU_EnemyCharacter> EnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Spawner")
	bool bSpawnOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Spawner")
	bool bAutoRespawnAfterDefeat = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Spawner", meta = (ClampMin = "0.2", ClampMax = "60.0"))
	float RespawnDelaySeconds = 4.0f;

	/** Applies aerial 3D flight-chase mode to the spawned enemy instance when enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Spawner")
	bool bSpawnAerialFlyer = false;

	/** Immediately spawns or resets the managed enemy back to this spawner's transform. */
	UFUNCTION(BlueprintCallable, Category = "IU|Spawner")
	AIU_EnemyCharacter* RespawnEnemyNow();

	UFUNCTION(BlueprintPure, Category = "IU|Spawner")
	AIU_EnemyCharacter* GetSpawnedEnemy() const { return SpawnedEnemy; }

	UFUNCTION(BlueprintPure, Category = "IU|Spawner")
	bool IsRespawnPending() const;

	UPROPERTY(BlueprintAssignable, Category = "IU|Spawner|Events")
	FIU_OnEnemySpawnedSignature OnEnemySpawnedOrReset;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Spawner")
	TObjectPtr<USceneComponent> SpawnerRoot;

private:
	UFUNCTION()
	void HandleManagedEnemyDefeated(AIU_EnemyCharacter* DefeatedEnemy);

	void OnRespawnTimerFired();

	UPROPERTY(Transient)
	TObjectPtr<AIU_EnemyCharacter> SpawnedEnemy = nullptr;

	FTimerHandle RespawnTimerHandle;
};
