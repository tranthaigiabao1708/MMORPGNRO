// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "IU_GameMode.generated.h"

/**
 * Authoritative GameMode for Step 1 Standalone & Step 2 Dedicated Server Multiplayer.
 * Assigns AIU_PlayerCharacter, AIU_PlayerController, AIU_GameState, AIU_PlayerState, and AIU_HUDManager,
 * and distributes multi-client spawn transforms cleanly around PlayerStart without capsule overlap.
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_GameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AIU_GameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	/** Computes a distinct non-overlapping spawn transform on a 180 cm ring around PlayerStart for each connecting client. */
	UFUNCTION(BlueprintCallable, Category = "IU|Multiplayer")
	FTransform AllocateMultiplayerSpawnTransform(AController* NewPlayer);

private:
	int32 NextSpawnSlotIndex = 0;
};
