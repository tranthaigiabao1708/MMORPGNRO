// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_PlayerState.h"
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
