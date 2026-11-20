// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_GameState.h"
#include "Net/UnrealNetwork.h"
#include "InfiniteUniverse.h"

AIU_GameState::AIU_GameState()
{
	bReplicates = true;
	// Exact bounds of the West Sector Martial Combat Arena in Lvl_Greybox_Prototype (-2600 +/- 1800 in X/Y)
	MartialSparringArenaBox = FBox(FVector(-4450.0f, -1850.0f, -150.0f), FVector(-800.0f, 1850.0f, 3000.0f));
}

void AIU_GameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AIU_GameState, bAllowGlobalPvPSparring);
	DOREPLIFETIME(AIU_GameState, MartialSparringArenaBox);
	DOREPLIFETIME(AIU_GameState, ActiveTacticalPings);
}

bool AIU_GameState::IsLocationInMartialArena(const FVector& WorldLocation) const
{
	return MartialSparringArenaBox.IsValid && MartialSparringArenaBox.IsInsideOrOn(WorldLocation);
}

bool AIU_GameState::ArePlayersAllowedToSpar(const AActor* Attacker, const AActor* Victim) const
{
	if (!Attacker || !Victim || Attacker == Victim)
	{
		return false;
	}

	if (bAllowGlobalPvPSparring)
	{
		return true;
	}

	return IsLocationInMartialArena(Attacker->GetActorLocation()) &&
		IsLocationInMartialArena(Victim->GetActorLocation());
}

void AIU_GameState::SetAllowGlobalPvPSparring(bool bEnable)
{
	if (HasAuthority() && bAllowGlobalPvPSparring != bEnable)
	{
		bAllowGlobalPvPSparring = bEnable;
		UE_LOG(LogInfiniteUniverse, Log, TEXT("[GameState] Global PvP Sparring set to %s"), bAllowGlobalPvPSparring ? TEXT("ENABLED") : TEXT("CO-OP ARENA ONLY"));
	}
}

void AIU_GameState::BroadcastPartyTacticalPing(
	const FString& InPartyID,
	const FString& InSenderName,
	const FVector& InWorldLoc,
	float DurationSec)
{
	if (!HasAuthority() || InPartyID.IsEmpty())
	{
		return;
	}
	const float NowSrv = GetServerWorldTimeSeconds();
	for (int32 i = ActiveTacticalPings.Num() - 1; i >= 0; --i)
	{
		if (ActiveTacticalPings[i].ExpireServerTime <= NowSrv)
		{
			ActiveTacticalPings.RemoveAt(i);
		}
	}
	if (ActiveTacticalPings.Num() >= 12)
	{
		ActiveTacticalPings.RemoveAt(0);
	}

	FIU_TacticalPingEntry Entry;
	Entry.PartyID = InPartyID;
	Entry.SenderName = InSenderName.IsEmpty() ? TEXT("Ally") : InSenderName.Left(20);
	Entry.WorldLocation = InWorldLoc;
	Entry.ExpireServerTime = NowSrv + FMath::Clamp(DurationSec, 1.0f, 15.0f);
	ActiveTacticalPings.Add(MoveTemp(Entry));
	ForceNetUpdate();
}
