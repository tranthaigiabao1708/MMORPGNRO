// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "IU_PlayerState.generated.h"

struct FIU_BackendCharacterSummary;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FIU_OnCharacterLevelUpSignature, int32, OldLevel, int32, NewLevel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FIU_OnCharacterEXPChangedSignature, int64, CurrentEXP, int64, NextLevelEXP, int32, CurrentLevel);

/**
 * Replicated PlayerState for Step 2 Multiplayer session tracking & Step 3 Persistent Character Progression.
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

	UFUNCTION(BlueprintPure, Category = "IU|Progression")
	FString GetPersistedCharacterName() const { return PersistedCharacterName; }

	UFUNCTION(BlueprintPure, Category = "IU|Progression")
	FString GetCharacterRaceID() const { return CharacterRaceID; }

	UFUNCTION(BlueprintPure, Category = "IU|Progression")
	int32 GetCharacterLevel() const { return CharacterLevel; }

	UFUNCTION(BlueprintPure, Category = "IU|Progression")
	int64 GetCurrentEXP() const { return CurrentEXP; }

	UFUNCTION(BlueprintPure, Category = "IU|Progression")
	int64 GetNextLevelEXP() const { return NextLevelEXP; }

	UFUNCTION(BlueprintPure, Category = "IU|Progression")
	float GetEXPPercent() const { return NextLevelEXP > 0 ? FMath::Clamp(static_cast<float>(CurrentEXP) / static_cast<float>(NextLevelEXP), 0.0f, 1.0f) : 1.0f; }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	int64 GetActiveFencingEpoch() const { return ActiveFencingEpoch; }

	const FString& GetPersistedCharacterUUID() const { return PersistedCharacterUUID; }
	bool HasValidBackendLease() const { return !bLeaseRevokedDoNotSave && ActiveFencingEpoch > 0 && !PersistedCharacterUUID.IsEmpty(); }
	bool IsLeaseRevoked() const { return bLeaseRevokedDoNotSave; }

	void AddEnemyKill();
	void AddPvPKill();
	void AddDeath();

	/** Server-only: populates PlayerState from ClaimLease response. */
	void ApplyLoadedBackendSummary(const FIU_BackendCharacterSummary& Summary);

	/** Server-only: updates Level/EXP/Kills after authoritative Go Backend GrantEXP response. */
	void ApplyAuthoritativeEXPUpdate(int32 NewLevel, int64 NewCurrentEXP, int64 NewNextLevelEXP, int32 NewEnemyKills, bool bLeveledUp);

	/** Server-only: immediately invalidates local lease ownership so Logout NEVER overwrites a newer server's epoch! */
	void MarkLeaseRevoked();

	/** Server-only: caches live Pawn transform & vitals right before UnPossess/Destroy so GameMode::Logout always has exact final coordinates! */
	void CachePawnWorldSnapshot(const FVector& InPos, float InYaw, float InHP, float InKi, bool bInAlive);
	bool GetCachedPawnWorldSnapshot(FVector& OutPos, float& OutYaw, float& OutHP, float& OutKi, bool& bOutAlive) const;

	UPROPERTY(BlueprintAssignable, Category = "IU|Progression|Events")
	FIU_OnCharacterLevelUpSignature OnCharacterLevelUp;

	UPROPERTY(BlueprintAssignable, Category = "IU|Progression|Events")
	FIU_OnCharacterEXPChangedSignature OnCharacterEXPChanged;

protected:
	UFUNCTION()
	void OnRep_CharacterLevel(int32 OldLevel);

	UFUNCTION()
	void OnRep_CurrentEXP();

	UFUNCTION()
	void OnRep_PersistedCharacterName();

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "IU|PlayerState")
	int32 EnemyKills = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "IU|PlayerState")
	int32 PvPKills = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "IU|PlayerState")
	int32 Deaths = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_PersistedCharacterName, Category = "IU|Progression")
	FString PersistedCharacterName;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "IU|Progression")
	FString CharacterRaceID = TEXT("race.zenith_warrior");

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_CharacterLevel, Category = "IU|Progression")
	int32 CharacterLevel = 1;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentEXP, Category = "IU|Progression")
	int64 CurrentEXP = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentEXP, Category = "IU|Progression")
	int64 NextLevelEXP = 100;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "IU|Backend")
	int64 ActiveFencingEpoch = 0;

private:
	FString PersistedCharacterUUID;
	bool bLeaseRevokedDoNotSave = false;
	bool bHasReceivedInitialLevel = false;

	bool bHasCachedPawnSnapshot = false;
	FVector CachedLogoutPosition = FVector::ZeroVector;
	float CachedLogoutYaw = 0.0f;
	float CachedLogoutHP = 100.0f;
	float CachedLogoutKi = 100.0f;
	bool bCachedLogoutAlive = true;
};
