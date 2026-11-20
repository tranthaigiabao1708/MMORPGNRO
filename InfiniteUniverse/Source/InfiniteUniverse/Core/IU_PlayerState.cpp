// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_PlayerState.h"
#include "Core/IU_GameState.h"
#include "Attributes/IU_AttributeComponent.h"
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
	DOREPLIFETIME(AIU_PlayerState, PotentialPoints);
	DOREPLIFETIME(AIU_PlayerState, HPUpgradeCount);
	DOREPLIFETIME(AIU_PlayerState, KiUpgradeCount);
	DOREPLIFETIME(AIU_PlayerState, AtkUpgradeCount);
	DOREPLIFETIME(AIU_PlayerState, NextHPCost);
	DOREPLIFETIME(AIU_PlayerState, NextKiCost);
	DOREPLIFETIME(AIU_PlayerState, NextAtkCost);
	DOREPLIFETIME(AIU_PlayerState, StandardCoin);
	DOREPLIFETIME(AIU_PlayerState, ElixirCount);
	DOREPLIFETIME(AIU_PlayerState, CrimsonOreCount);
	DOREPLIFETIME(AIU_PlayerState, EquippedItemCount);
	DOREPLIFETIME(AIU_PlayerState, GuildID);
	DOREPLIFETIME(AIU_PlayerState, GuildName);
	DOREPLIFETIME(AIU_PlayerState, GuildTag);
	DOREPLIFETIME(AIU_PlayerState, GuildRole);
	DOREPLIFETIME(AIU_PlayerState, PartyID);
	DOREPLIFETIME(AIU_PlayerState, bIsPartyLeader);
	DOREPLIFETIME(AIU_PlayerState, OverheadChatBubbleText);
	DOREPLIFETIME(AIU_PlayerState, OverheadChatChannel);
	DOREPLIFETIME(AIU_PlayerState, OverheadChatExpireServerTime);
	DOREPLIFETIME(AIU_PlayerState, PityTitanTokens);
	DOREPLIFETIME(AIU_PlayerState, CurrentZoneID);
	DOREPLIFETIME(AIU_PlayerState, CurrentChannelID);
	DOREPLIFETIME(AIU_PlayerState, PersistedCharacterUUID);
	DOREPLIFETIME_CONDITION(AIU_PlayerState, ActiveFencingEpoch, COND_OwnerOnly);
}

float AIU_PlayerState::ComputePartySynergyMultiplier(int32 NearbyPartyMembers)
{
	switch (NearbyPartyMembers)
	{
	case 2: return 1.15f;
	case 3: return 1.25f;
	case 4: return 1.35f;
	default:
		return (NearbyPartyMembers >= 5) ? 1.45f : 1.0f;
	}
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
	PotentialPoints = FMath::Max<int64>(0, Summary.PotentialPoints);
	HPUpgradeCount = FMath::Max(0, Summary.HPUpgrades);
	KiUpgradeCount = FMath::Max(0, Summary.KiUpgrades);
	AtkUpgradeCount = FMath::Max(0, Summary.AtkUpgrades);
	NextHPCost = FMath::Max<int64>(100, Summary.NextHPCost);
	NextKiCost = FMath::Max<int64>(100, Summary.NextKiCost);
	NextAtkCost = FMath::Max<int64>(150, Summary.NextAtkCost);
	StandardCoin = FMath::Max<int64>(0, Summary.StandardCoin);
	PityTitanTokens = FMath::Max(0, Summary.PityTitanTokens);
	CurrentZoneID = Summary.ZoneID.IsEmpty() ? TEXT("zone.p01_west_hub") : Summary.ZoneID;
	CurrentChannelID = FMath::Max(1, Summary.ChannelID);
	EnemyKills = FMath::Max(0, Summary.EnemyKills);
	PvPKills = FMath::Max(0, Summary.PvPKills);
	Deaths = FMath::Max(0, Summary.Deaths);
	GuildID = Summary.GuildID;
	GuildName = Summary.GuildName;
	GuildTag = Summary.GuildTag;
	GuildRole = Summary.GuildRole;
	ActiveFencingEpoch = Summary.FencingEpoch;
	bLeaseRevokedDoNotSave = false;
	bHasReceivedInitialLevel = true;

	if (!Summary.CharacterName.IsEmpty())
	{
		SetPlayerName(Summary.CharacterName);
	}

	OnCharacterEXPChanged.Broadcast(CurrentEXP, NextLevelEXP, CharacterLevel);
	OnCharacterPotentialChanged.Broadcast(PotentialPoints, NextHPCost, NextKiCost, NextAtkCost);
	OnCharacterEconomyChanged.Broadcast(StandardCoin, ElixirCount, CrimsonOreCount, EquippedItemCount);
	OnCharacterSocialChanged.Broadcast(GuildTag, GuildName, PartyID);
	ForceNetUpdate();
}

void AIU_PlayerState::ApplyAuthoritativeEXPUpdate(
	int32 NewLevel,
	int64 NewCurrentEXP,
	int64 NewNextLevelEXP,
	int64 NewPotentialPoints,
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
	PotentialPoints = FMath::Max<int64>(0, NewPotentialPoints);
	EnemyKills = FMath::Max(EnemyKills, NewEnemyKills);

	OnCharacterEXPChanged.Broadcast(CurrentEXP, NextLevelEXP, CharacterLevel);
	OnCharacterPotentialChanged.Broadcast(PotentialPoints, NextHPCost, NextKiCost, NextAtkCost);
	if (bLeveledUp && CharacterLevel > OldLevel)
	{
		OnCharacterLevelUp.Broadcast(OldLevel, CharacterLevel);
	}
	ForceNetUpdate();
}

void AIU_PlayerState::ApplyAuthoritativePotentialUpgrade(const FIU_BackendCharacterSummary& Summary)
{
	if (!HasAuthority())
	{
		return;
	}

	PotentialPoints = FMath::Max<int64>(0, Summary.PotentialPoints);
	HPUpgradeCount = FMath::Max(0, Summary.HPUpgrades);
	KiUpgradeCount = FMath::Max(0, Summary.KiUpgrades);
	AtkUpgradeCount = FMath::Max(0, Summary.AtkUpgrades);
	NextHPCost = FMath::Max<int64>(100, Summary.NextHPCost);
	NextKiCost = FMath::Max<int64>(100, Summary.NextKiCost);
	NextAtkCost = FMath::Max<int64>(150, Summary.NextAtkCost);

	OnCharacterPotentialChanged.Broadcast(PotentialPoints, NextHPCost, NextKiCost, NextAtkCost);
	ForceNetUpdate();
}

void AIU_PlayerState::AwardOfflineHitProgress(float OutgoingDamage, int32 NearbyPartyMembers)
{
	if (!HasAuthority() || OutgoingDamage <= 0.0f)
	{
		return;
	}

	const int64 BaseReward = FMath::Max<int64>(1, FMath::RoundToInt64(static_cast<double>(OutgoingDamage) * 1.5));
	int64 SynergyPercent = 100;
	if (NearbyPartyMembers >= 5)      { SynergyPercent = 145; }
	else if (NearbyPartyMembers == 4) { SynergyPercent = 135; }
	else if (NearbyPartyMembers == 3) { SynergyPercent = 125; }
	else if (NearbyPartyMembers == 2) { SynergyPercent = 115; }
	const int64 Reward = FMath::Max<int64>(1, (BaseReward * SynergyPercent + 50) / 100);
	const int32 OldLv = CharacterLevel;
	CurrentEXP += Reward;
	bool bLeveled = false;
	while (CharacterLevel < 200)
	{
		const int64 L = static_cast<int64>(CharacterLevel);
		const int64 Req = 100 * L + 25 * (L - 1) * (L - 1);
		if (CurrentEXP < Req)
		{
			NextLevelEXP = Req;
			break;
		}
		CurrentEXP -= Req;
		++CharacterLevel;
		bLeveled = true;
		const int64 NL = static_cast<int64>(CharacterLevel);
		NextLevelEXP = (CharacterLevel < 200) ? (100 * NL + 25 * (NL - 1) * (NL - 1)) : 0;
	}

	PotentialPoints += Reward;
	OnCharacterEXPChanged.Broadcast(CurrentEXP, NextLevelEXP, CharacterLevel);
	OnCharacterPotentialChanged.Broadcast(PotentialPoints, NextHPCost, NextKiCost, NextAtkCost);
	if (bLeveled)
	{
		OnCharacterLevelUp.Broadcast(OldLv, CharacterLevel);
	}
	ForceNetUpdate();
}

bool AIU_PlayerState::TryOfflineAllocatePotential(EIU_PotentialStatType StatType, APawn* TargetPawn)
{
	if (!HasAuthority() || !TargetPawn)
	{
		return false;
	}
	UIU_AttributeComponent* Attr = TargetPawn->FindComponentByClass<UIU_AttributeComponent>();
	if (!Attr)
	{
		return false;
	}

	int64 Cost = 0;
	switch (StatType)
	{
	case EIU_PotentialStatType::MaxHP:
		Cost = 100 * (static_cast<int64>(HPUpgradeCount) + 1);
		break;
	case EIU_PotentialStatType::MaxKi:
		Cost = 100 * (static_cast<int64>(KiUpgradeCount) + 1);
		break;
	case EIU_PotentialStatType::AttackPower:
		Cost = 150 * (static_cast<int64>(AtkUpgradeCount) + 1);
		break;
	default:
		return false;
	}

	if (PotentialPoints < Cost)
	{
		return false;
	}

	PotentialPoints -= Cost;
	float CurHP = Attr->GetHP();
	float CurKi = Attr->GetKi();
	switch (StatType)
	{
	case EIU_PotentialStatType::MaxHP:
		++HPUpgradeCount;
		CurHP += 100.0f;
		break;
	case EIU_PotentialStatType::MaxKi:
		++KiUpgradeCount;
		CurKi += 100.0f;
		break;
	case EIU_PotentialStatType::AttackPower:
		++AtkUpgradeCount;
		break;
	}

	NextHPCost = 100 * (static_cast<int64>(HPUpgradeCount) + 1);
	NextKiCost = 100 * (static_cast<int64>(KiUpgradeCount) + 1);
	NextAtkCost = 150 * (static_cast<int64>(AtkUpgradeCount) + 1);

	const float NewMaxHP = 200.0f + 100.0f * static_cast<float>(HPUpgradeCount);
	const float NewMaxKi = 200.0f + 100.0f * static_cast<float>(KiUpgradeCount);
	const float NewAtk = 100.0f + 10.0f * static_cast<float>(AtkUpgradeCount);
	Attr->ApplyPersistedVitals(NewMaxHP, CurHP, NewMaxKi, CurKi, NewAtk / 100.0f, NewAtk, Attr->GetDefense());

	OnCharacterPotentialChanged.Broadcast(PotentialPoints, NextHPCost, NextKiCost, NextAtkCost);
	ForceNetUpdate();
	return true;
}

void AIU_PlayerState::ApplyAuthoritativeEconomySummary(
	int64 InStandardCoin,
	int32 InElixirCount,
	int32 InCrimsonOreCount,
	int32 InEquippedCount)
{
	if (!HasAuthority())
	{
		return;
	}
	StandardCoin = FMath::Max<int64>(0, InStandardCoin);
	ElixirCount = FMath::Max(0, InElixirCount);
	CrimsonOreCount = FMath::Max(0, InCrimsonOreCount);
	EquippedItemCount = FMath::Max(0, InEquippedCount);
	OnCharacterEconomyChanged.Broadcast(StandardCoin, ElixirCount, CrimsonOreCount, EquippedItemCount);
	ForceNetUpdate();
}

void AIU_PlayerState::SetAuthoritativeTitanTokens(int32 NewTokenBalance)
{
	if (!HasAuthority())
	{
		return;
	}
	PityTitanTokens = FMath::Max(0, NewTokenBalance);
	OnCharacterEconomyChanged.Broadcast(StandardCoin, ElixirCount, CrimsonOreCount, EquippedItemCount);
	ForceNetUpdate();
}

void AIU_PlayerState::ApplyAuthoritativeGuildIdentity(
	const FString& InGuildID,
	const FString& InGuildName,
	const FString& InGuildTag,
	const FString& InGuildRole)
{
	if (!HasAuthority())
	{
		return;
	}
	GuildID = InGuildID;
	GuildName = UIU_BackendSubsystem::SanitizeVietnameseForSlateASCII(InGuildName);
	GuildTag = InGuildTag.ToUpper();
	GuildRole = InGuildRole;
	OnCharacterSocialChanged.Broadcast(GuildTag, GuildName, PartyID);
	ForceNetUpdate();
}

void AIU_PlayerState::ApplyAuthoritativePartyIdentity(const FString& InPartyID, bool bInLeader)
{
	if (!HasAuthority())
	{
		return;
	}
	PartyID = InPartyID;
	bIsPartyLeader = bInLeader;
	OnCharacterSocialChanged.Broadcast(GuildTag, GuildName, PartyID);
	ForceNetUpdate();
}

void AIU_PlayerState::SetOverheadSpeechBubble(const FString& CleanText, const FString& InChannel, float DurationSeconds)
{
	if (!HasAuthority())
	{
		return;
	}
	OverheadChatBubbleText = UIU_BackendSubsystem::SanitizeVietnameseForSlateASCII(CleanText).Left(120);
	OverheadChatChannel = InChannel;
	const UWorld* W = GetWorld();
	const AIU_GameState* GS = W ? W->GetGameState<AIU_GameState>() : nullptr;
	const float SrvTime = GS ? GS->GetServerWorldTimeSeconds() : (W ? W->GetTimeSeconds() : 0.0f);
	OverheadChatExpireServerTime = SrvTime + FMath::Clamp(DurationSeconds, 0.5f, 12.0f);
	ForceNetUpdate();
}

bool AIU_PlayerState::TryOfflineUseElixir(APawn* TargetPawn)
{
	if (!HasAuthority() || !TargetPawn || ElixirCount <= 0)
	{
		return false;
	}
	UIU_AttributeComponent* Attr = TargetPawn->FindComponentByClass<UIU_AttributeComponent>();
	if (!Attr || Attr->IsDead())
	{
		return false;
	}
	--ElixirCount;
	Attr->ModifyHP(150.0f);
	Attr->RestoreKi(150.0f);
	OnCharacterEconomyChanged.Broadcast(StandardCoin, ElixirCount, CrimsonOreCount, EquippedItemCount);
	ForceNetUpdate();
	return true;
}

void AIU_PlayerState::AwardOfflineMobLoot(int64 OfflineCoinGain)
{
	if (!HasAuthority() || OfflineCoinGain <= 0)
	{
		return;
	}
	StandardCoin += OfflineCoinGain;
	OnCharacterEconomyChanged.Broadcast(StandardCoin, ElixirCount, CrimsonOreCount, EquippedItemCount);
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

void AIU_PlayerState::OnRep_PotentialState()
{
	OnCharacterPotentialChanged.Broadcast(PotentialPoints, NextHPCost, NextKiCost, NextAtkCost);
}

void AIU_PlayerState::OnRep_EconomyState()
{
	OnCharacterEconomyChanged.Broadcast(StandardCoin, ElixirCount, CrimsonOreCount, EquippedItemCount);
}

void AIU_PlayerState::OnRep_SocialIdentity()
{
	OnCharacterSocialChanged.Broadcast(GuildTag, GuildName, PartyID);
}
