// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "IU_GameMode.generated.h"

struct FIU_CharacterSavePayload;

/**
 * Authoritative GameMode for Step 1 Standalone, Step 2 Dedicated Server Multiplayer,
 * and Step 3 Lease-Fenced PostgreSQL Persistence & Progression.
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_GameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AIU_GameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	/** Computes a distinct non-overlapping spawn transform on a 180 cm ring around PlayerStart for each connecting client. */
	UFUNCTION(BlueprintCallable, Category = "IU|Multiplayer")
	FTransform AllocateMultiplayerSpawnTransform(AController* NewPlayer);

	/** Used by Step 3 Automation Tests to verify PreLogin ticket enforcement without modifying global CLI flags. */
	void SetForceRequireTicketForTesting(bool bInForceRequire) { bForceRequireTicketForTest = bInForceRequire; }

private:
	bool BuildCharacterSavePayload(AController* Controller, const FString& SnapshotReason, FIU_CharacterSavePayload& OutPayload) const;
	void HandleLeaseRenewTick();
	void HandlePeriodicSaveTick();
	void KickPlayerControllerWithReason(APlayerController* TargetPC, const FString& Reason);

	int32 NextSpawnSlotIndex = 0;
	bool bForceRequireTicketForTest = false;
	TMap<TWeakObjectPtr<APlayerController>, FString> PendingTicketsByController;

	FTimerHandle LeaseRenewTimerHandle;
	FTimerHandle PeriodicSaveTimerHandle;
};
