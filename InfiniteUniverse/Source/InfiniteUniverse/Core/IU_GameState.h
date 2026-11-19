// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "IU_GameState.generated.h"

/**
 * Replicated GameState for Step 2 Multiplayer & Dedicated Server.
 * Enforces Co-op PvE friendly-fire protection in open world zones while enabling
 * Server-Authoritative PvP Sparring inside the West Martial Combat Arena (or when globally toggled).
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_GameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AIU_GameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** If true on Server, allows Player-vs-Player sparring anywhere on the map (toggled via F4 in Development). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "IU|Multiplayer|PvP")
	bool bAllowGlobalPvPSparring = false;

	/** World-space bounds of the West Martial Combat Arena (36m x 36m centered at X=-2600, Y=0 in Lvl_Greybox_Prototype). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "IU|Multiplayer|PvP")
	FBox MartialSparringArenaBox;

	UFUNCTION(BlueprintPure, Category = "IU|Multiplayer|PvP")
	bool IsLocationInMartialArena(const FVector& WorldLocation) const;

	UFUNCTION(BlueprintPure, Category = "IU|Multiplayer|PvP")
	bool ArePlayersAllowedToSpar(const AActor* Attacker, const AActor* Victim) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IU|Multiplayer|PvP")
	void SetAllowGlobalPvPSparring(bool bEnable);
};
