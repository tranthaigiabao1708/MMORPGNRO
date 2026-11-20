// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "IU_GameState.generated.h"

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_TacticalPingEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	FString PartyID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	FString SenderName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	float ExpireServerTime = 0.0f;
};

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

	/** Replicated 3D Tactical Party Pings (Alt + LMB, 6.0s lifetime). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "IU|Multiplayer|Party")
	TArray<FIU_TacticalPingEntry> ActiveTacticalPings;

	/** Server-only: places a 6.0s 3D Tactical Ping visible to matching PartyID members. */
	void BroadcastPartyTacticalPing(const FString& InPartyID, const FString& InSenderName, const FVector& InWorldLoc, float DurationSec = 6.0f);
};
