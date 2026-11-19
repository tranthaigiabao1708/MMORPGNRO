// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "IU_PlayerState.generated.h"

/**
 * Replicated PlayerState for Step 2 Multiplayer session tracking (Kills, PvP Wins, Deaths, Ping).
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_PlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AIU_PlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "IU|PlayerState")
	int32 GetEnemyKills() const { return EnemyKills; }

	UFUNCTION(BlueprintPure, Category = "IU|PlayerState")
	int32 GetPvPKills() const { return PvPKills; }

	UFUNCTION(BlueprintPure, Category = "IU|PlayerState")
	int32 GetDeaths() const { return Deaths; }

	void AddEnemyKill();
	void AddPvPKill();
	void AddDeath();

protected:
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "IU|PlayerState")
	int32 EnemyKills = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "IU|PlayerState")
	int32 PvPKills = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "IU|PlayerState")
	int32 Deaths = 0;
};
