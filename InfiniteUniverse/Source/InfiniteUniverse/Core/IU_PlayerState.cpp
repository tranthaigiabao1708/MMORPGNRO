// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_PlayerState.h"
#include "Network/IU_BackendSubsystem.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AIU_PlayerState::AIU_PlayerState()
{
	bReplicates = true;
	SetNetUpdateFrequency(10.0f);
}

void AIU_PlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AIU_PlayerState, EnemyKills);
	DOREPLIFETIME(AIU_PlayerState, PvPKills);
	DOREPLIFETIME(AIU_PlayerState, Deaths);
	DOREPLIFETIME(AIU_PlayerState, PersistedCharacterName);
	DOREPLIFETIME(AIU_PlayerState, CharacterRaceID);
	DOREPLIFETIME(AIU_PlayerState, CharacterLevel);
	DOREPLIFETIME(AIU_PlayerState, CurrentEXP);
	DOREPLIFETIME(AIU_PlayerState, NextLevelEXP);
	DOREPLIFETIME_CONDITION(AIU_PlayerState, ActiveFencingEpoch, COND_OwnerOnly);
}

void AIU_PlayerState::AddEnemyKill()
{
	if (HasAuthority())
	{
		++EnemyKills;
	}
}

void AIU_PlayerState::AddPvPKill()
{
	if (HasAuthority())
	{
		++PvPKills;
	}
}

void AIU_PlayerState::AddDeath()
{
	if (HasAuthority())
	{
		++Deaths;
	}
}

void AIU_PlayerState::ApplyLoadedBackendSummary(const FIU_BackendCharacterSummary& Summary)
{
	if (!HasAuthority())
	{
		return;
	}

	PersistedCharacterUUID = Summary.CharacterID;
	PersistedCharacterName = Summary.CharacterName;
	CharacterRaceID = Summary.RaceID;
	CharacterLevel = FMath::Max(1, Summary.Level);
	CurrentEXP = FMath::Max<int64>(0, Summary.CurrentExp);
	NextLevelEXP = FMath::Max<int64>(1, Summary.NextLevelExp);
	EnemyKills = FMath::Max(0, Summary.EnemyKills);
	PvPKills = FMath::Max(0, Summary.PvPKills);
	Deaths = FMath::Max(0, Summary.Deaths);
	ActiveFencingEpoch = Summary.FencingEpoch;
	bLeaseRevokedDoNotSave = false;
	bHasReceivedInitialLevel = true;

	if (!Summary.CharacterName.IsEmpty())
	{
		SetPlayerName(Summary.CharacterName);
	}

	OnCharacterEXPChanged.Broadcast(CurrentEXP, NextLevelEXP, CharacterLevel);
	ForceNetUpdate();
}

void AIU_PlayerState::ApplyAuthoritativeEXPUpdate(
	int32 NewLevel,
	int64 NewCurrentEXP,
	int64 NewNextLevelEXP,
	int32 NewEnemyKills,
	bool bLeveledUp)
{
	if (!HasAuthority())
	{
		return;
	}

	const int32 OldLevel = CharacterLevel;
	CharacterLevel = FMath::Max(1, NewLevel);
	CurrentEXP = FMath::Max<int64>(0, NewCurrentEXP);
	NextLevelEXP = FMath::Max<int64>(1, NewNextLevelEXP);
	EnemyKills = FMath::Max(EnemyKills, NewEnemyKills);

	OnCharacterEXPChanged.Broadcast(CurrentEXP, NextLevelEXP, CharacterLevel);
	if (bLeveledUp && CharacterLevel > OldLevel)
	{
		OnCharacterLevelUp.Broadcast(OldLevel, CharacterLevel);
	}
	ForceNetUpdate();
}

void AIU_PlayerState::MarkLeaseRevoked()
{
	bLeaseRevokedDoNotSave = true;
	ActiveFencingEpoch = 0;
}

void AIU_PlayerState::CachePawnWorldSnapshot(const FVector& InPos, float InYaw, float InHP, float InKi, bool bInAlive)
{
	CachedLogoutPosition = InPos;
	CachedLogoutYaw = InYaw;
	CachedLogoutHP = InHP;
	CachedLogoutKi = InKi;
	bCachedLogoutAlive = bInAlive;
	bHasCachedPawnSnapshot = true;
}

bool AIU_PlayerState::GetCachedPawnWorldSnapshot(FVector& OutPos, float& OutYaw, float& OutHP, float& OutKi, bool& bOutAlive) const
{
	if (!bHasCachedPawnSnapshot)
	{
		return false;
	}
	OutPos = CachedLogoutPosition;
	OutYaw = CachedLogoutYaw;
	OutHP = CachedLogoutHP;
	OutKi = CachedLogoutKi;
	bOutAlive = bCachedLogoutAlive;
	return true;
}

void AIU_PlayerState::OnRep_CharacterLevel(int32 OldLevel)
{
	OnCharacterEXPChanged.Broadcast(CurrentEXP, NextLevelEXP, CharacterLevel);
	const UWorld* W = GetWorld();
	const bool bPastInitialJoinWindow = W && (W->GetTimeSeconds() - CreationTime) > 1.25f;
	if (bHasReceivedInitialLevel && bPastInitialJoinWindow && CharacterLevel > OldLevel && OldLevel >= 1)
	{
		OnCharacterLevelUp.Broadcast(OldLevel, CharacterLevel);
	}
	bHasReceivedInitialLevel = true;
}

void AIU_PlayerState::OnRep_CurrentEXP()
{
	OnCharacterEXPChanged.Broadcast(CurrentEXP, NextLevelEXP, CharacterLevel);
}

void AIU_PlayerState::OnRep_PersistedCharacterName()
{
	OnCharacterEXPChanged.Broadcast(CurrentEXP, NextLevelEXP, CharacterLevel);
}
