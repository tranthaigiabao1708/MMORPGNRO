// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IU_BackendSubsystem.generated.h"

/**
 * Authoritative character snapshot returned by Go Backend (/v1/characters, /internal/v1/lease/claim, /internal/v1/character/exp).
 */
USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_BackendCharacterSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FString CharacterID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FString AccountID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FString CharacterName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FString RaceID = TEXT("race.zenith_warrior");

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 CurrentExp = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 NextLevelExp = 100;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 PotentialPoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 StandardCoin = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 PityTitanTokens = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FString ZoneID = TEXT("zone.p01_west_hub");

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 ChannelID = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FVector SavedPosition = FVector(0.0f, 0.0f, 112.0f);

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float SavedYaw = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	bool bHasSavedPosition = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	bool bIsAlive = true;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float HP = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float MaxHP = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float Energy = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float MaxEnergy = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float AttackPower = 100.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float AttackPowerMultiplier = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float Defense = 20.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 HPUpgrades = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 KiUpgrades = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 AtkUpgrades = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 NextHPCost = 100;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 NextKiCost = 100;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 NextAtkCost = 150;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 EnemyKills = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 PvPKills = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 Deaths = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FString GuildID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FString GuildName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FString GuildTag;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FString GuildRole;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 FencingEpoch = 0;

	// Populated on /internal/v1/character/exp & /internal/v1/character/potential responses
	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	bool bLeveledUp = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 OldLevel = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 ExpGranted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 PotentialGranted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float PartySynergyMult = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 PotentialSpent = 0;
};

/**
 * Replicated/NetSerializable view of a single inventory or equipped item instance from Go Backend.
 */
USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_InventoryItemView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	FString InstanceID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	FString ItemDefID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	FString Category;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	FString EquipSlot;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	FString EquippedSlotID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	FString Rarity;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int32 RequiredLevel = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int32 BagSlotIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int32 StackCount = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int32 MaxStack = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int32 EnhanceLevel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int32 MaxEnhanceLevel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int32 EnhancePity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int64 SellPriceCoin = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	bool bIsBound = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	bool bIsPlayerLocked = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	FString StatSummaryText;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int64 NextEnhanceCoin = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int32 NextEnhanceMat = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int32 NextEnhanceBP = 0;
};

/**
 * Complete authoritative Step 4 snapshot (Wallet + 48-Slot Bag + 8-Slot Equipment + Compiled Live Stats).
 */
USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_InventorySnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	FString CharacterID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int64 StandardCoin = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	float HP = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	float MaxHP = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	float Energy = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	float MaxEnergy = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	float AttackPower = 100.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	float AttackPowerMultiplier = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	float Defense = 20.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int32 UsedBagSlots = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int32 MaxBagSlots = 48;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int32 ElixirCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int32 CrimsonOreCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	FString ActionResult;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	bool bHasEnhanceOutcome = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	bool bEnhanceSucceeded = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	int64 CoinsDelta = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	TArray<FIU_InventoryItemView> BagItems;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Inventory")
	TArray<FIU_InventoryItemView> EquippedItems;
};

/**
 * Authoritative CSPRNG Monster Personal Loot & Vang summary returned by /internal/v1/loot/roll-mob.
 */
USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_MobLootDropSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Loot")
	int64 CoinsGranted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Loot")
	int64 NewCoinBalance = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Loot")
	bool bBagFullSkipped = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Loot")
	TArray<FString> DroppedItemLabels;
};

// ============================================================================
// Step 5 Authoritative Party, Guild, Multi-Channel Chat & P2P Trade Snapshots
// ============================================================================

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_PartyMemberView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	FString CharacterID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	FString CharacterName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	float HP = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	float MaxHP = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	float Energy = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	float MaxEnergy = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	bool bIsLeader = false;
};

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_PartyInviteView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	FString InviteID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	FString InviterCharID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	FString InviterName;
};

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_PartySnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	bool bInParty = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	FString PartyID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	FString LeaderCharID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	FString ActionMessage;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	TArray<FIU_PartyMemberView> Members;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Party")
	TArray<FIU_PartyInviteView> PendingInvites;
};

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_GuildMemberView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString CharacterID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString CharacterName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString MemberRole = TEXT("MEMBER");

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	int64 WeeklyContribution = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	int64 TotalContribution = 0;
};

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_GuildDirectoryEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString GuildID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString GuildName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString GuildTag;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString LeaderName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	int32 GuildLevel = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	int32 MemberCount = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	int32 MemberCap = 30;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString MOTD;
};

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_GuildSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	bool bInGuild = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString GuildID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString GuildName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString GuildTag;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString LeaderCharID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString LeaderName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	int32 GuildLevel = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	int64 GuildExp = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	int32 MemberCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	int32 MemberCap = 30;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	int64 TreasuryStandardCoin = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString MOTD;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString MyRole;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	int64 MyWeeklyContribution = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	int64 MyTotalContribution = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	int64 StandardCoin = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	FString ActionMessage;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	TArray<FIU_GuildMemberView> Members;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Guild")
	TArray<FIU_GuildDirectoryEntry> Directory;
};

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_ChatMessageView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Social")
	int64 Seq = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Social")
	FString Channel = TEXT("LOCAL");

	UPROPERTY(BlueprintReadOnly, Category = "IU|Social")
	FString SenderCharID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Social")
	FString SenderName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Social")
	FString SenderGuildTag;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Social")
	FString PartyID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Social")
	FString GuildID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Social")
	FString TargetCharID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Social")
	FString TargetName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Social")
	FVector SenderPosition = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Social")
	FString Content;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Social")
	bool bShowBubble = true;
};

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_TradeItemView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	FString InstanceID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	FString ItemDefID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	FString Rarity;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	int32 StackCount = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	int32 EnhanceLevel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	FString StatSummaryText;
};

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_TradeSessionSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	bool bHasActiveTrade = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	FString TradeID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	FString State;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	FString InitiatorCharID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	FString InitiatorName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	FString RecipientCharID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	FString RecipientName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	int64 InitiatorCoin = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	int64 RecipientCoin = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	int64 TaxCoinPreview = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	bool bInitiatorLocked = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	bool bRecipientLocked = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	bool bInitiatorConfirmed = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	bool bRecipientConfirmed = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	int64 RemainingInspectMs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	int32 OfferVersion = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	FString ActionMessage;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	TArray<FIU_TradeItemView> InitiatorItems;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Trade")
	TArray<FIU_TradeItemView> RecipientItems;
};

// ============================================================================
// Step 6 Authoritative Multi-Zone Channels, 3-Phase World Boss, Pity Shop & Quests
// ============================================================================

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_ZoneChannelView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|World")
	FString ServerID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|World")
	FString ZoneID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|World")
	FString ZoneLabel;

	UPROPERTY(BlueprintReadOnly, Category = "IU|World")
	int32 ChannelID = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|World")
	FString ServerAddr;

	UPROPERTY(BlueprintReadOnly, Category = "IU|World")
	int32 OnlineCCU = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|World")
	int32 SoftCapCCU = 20;

	UPROPERTY(BlueprintReadOnly, Category = "IU|World")
	int32 HardCapCCU = 25;

	UPROPERTY(BlueprintReadOnly, Category = "IU|World")
	FString StatusColor = TEXT("GREEN");

	UPROPERTY(BlueprintReadOnly, Category = "IU|World")
	bool bBossAlive = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|World")
	FString BossName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|World")
	float BossHPRatio = 0.0f;
};

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_BossParticipantReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "IU|Boss")
	FString CharacterID;

	UPROPERTY(BlueprintReadWrite, Category = "IU|Boss")
	FString CharacterName;

	UPROPERTY(BlueprintReadWrite, Category = "IU|Boss")
	int64 FencingEpoch = 0;

	UPROPERTY(BlueprintReadWrite, Category = "IU|Boss")
	float DamageDealt = 0.0f;
};

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_BossParticipantRewardView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	FString CharacterID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	FString CharacterName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	int32 Rank = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	float SharePct = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	bool bEligible = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	int64 CoinsGranted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	int32 TokensGranted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	int32 NewTokenBalance = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	int32 ConsecutiveDryKills = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	bool bHardPityTriggered = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	TArray<FString> DroppedItemLabels;
};

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_BossKillSettleSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	FString KillID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	FString BossID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	FString TopKillerName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	FString Announcement;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Boss")
	TArray<FIU_BossParticipantRewardView> Rewards;
};

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_PityShopEntryView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	FString EntryID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	FString TokenItemID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	int32 TokenCost = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	int64 StandardCoinCost = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	FString RewardItemID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	FString RewardName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	FString RewardRarity;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	int32 RewardQuantity = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	int32 DailyLimit = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	int32 DailyPurchased = 0;
};

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_PityShopSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	FString TokenID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	int32 TokenBalance = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	int32 LifetimeEarned = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	int32 LifetimeRedeemed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	int32 ConsecutiveDryKills = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	int32 HardPityThreshold = 4;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	int64 StandardCoin = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	FString ActionMessage;

	UPROPERTY(BlueprintReadOnly, Category = "IU|PityShop")
	TArray<FIU_PityShopEntryView> Entries;
};

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_QuestEntryView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Quest")
	FString QuestID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Quest")
	FString Title;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Quest")
	FString Description;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Quest")
	FString QuestState = TEXT("IN_PROGRESS");

	UPROPERTY(BlueprintReadOnly, Category = "IU|Quest")
	int32 CurrentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Quest")
	int32 TargetCount = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Quest")
	int32 RepeatCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Quest")
	int64 RewardCoin = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Quest")
	int64 RewardExpAndPot = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Quest")
	int32 RewardTitanToken = 0;
};

USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_QuestSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Quest")
	FString ActionMessage;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Quest")
	TArray<FIU_QuestEntryView> Quests;
};

/**
 * Payload sent from UE5 Dedicated Server to Go Backend for fenced periodic/logout/zone-transfer saves.
 */
struct INFINITEUNIVERSE_API FIU_CharacterSavePayload
{
	FString CharacterID;
	int64 FencingEpoch = 0;
	FString ServerID;
	FString SnapshotReason = TEXT("PERIODIC_60S");
	FString ZoneID = TEXT("zone.p01_west_hub");
	int32 ChannelID = 1;
	FVector Position = FVector::ZeroVector;
	float Yaw = 0.0f;
	bool bIsAlive = true;
	float HP = 100.0f;
	float Energy = 100.0f;
	int32 EnemyKills = 0;
	int32 PvPKills = 0;
	int32 Deaths = 0;
};

struct INFINITEUNIVERSE_API FIU_LeaseRenewalEntry
{
	FString CharacterID;
	int64 FencingEpoch = 0;
};

/**
 * Step 3 Backend Integration Subsystem:
 * - Client Edge: Register / Login (Argon2id + Ed25519 JWT), List/Create Characters, EnterWorld Ticket + ClientTravel.
 * - Dedicated Server Bridge: HMAC-SHA256 authenticated ClaimLease, RenewLeases, SaveCheckpoint, GrantEXP, ReleaseLease.
 * - Gracefully inactive when -UseBackend / -BackendURL is not supplied so all Step 1 & Step 2 offline tests remain 100% green.
 */
UCLASS()
class INFINITEUNIVERSE_API UIU_BackendSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ========================================================================
	// Pure Cryptographic Primitives (FIPS 180-4 SHA-256 & RFC 2104 HMAC-SHA256)
	// ========================================================================

	static void ComputeSHA256(const uint8* Data, SIZE_T Length, uint8 OutDigest[32]);
	static FString ComputeHMACSHA256Hex(const FString& SecretUtf8, const FString& PayloadUtf8);
	static FString SanitizeVietnameseForSlateASCII(const FString& InStr);
	static bool ParseCharacterSummaryFromJson(const FString& JsonString, FIU_BackendCharacterSummary& OutSummary);
	static bool ParseInventorySnapshotFromJson(const FString& JsonString, FIU_InventorySnapshot& OutSnapshot);
	static bool ParseMobLootResponseFromJson(const FString& JsonString, FIU_MobLootDropSummary& OutLoot, FIU_InventorySnapshot& OutSnapshot);
	static bool ParsePartySnapshotFromJson(const FString& JsonString, FIU_PartySnapshot& OutSnapshot);
	static bool ParseGuildSnapshotFromJson(const FString& JsonString, FIU_GuildSnapshot& OutSnapshot);
	static bool ParseTradeSnapshotFromJson(const FString& JsonString, FIU_TradeSessionSnapshot& OutSnapshot);
	static bool ParseTradeConfirmResponseFromJson(const FString& JsonString, FIU_TradeSessionSnapshot& OutTrade, FIU_InventorySnapshot& OutInv, bool& bOutHasInv);
	static bool ParseChatMessageFromJson(const FString& JsonString, FIU_ChatMessageView& OutMessage);
	static bool ParseWorldChannelsFromJson(const FString& JsonString, TArray<FIU_ZoneChannelView>& OutChannels);
	static bool ParseBossKillSettleFromJson(const FString& JsonString, FIU_BossKillSettleSummary& OutSummary);
	static bool ParsePityShopSnapshotFromJson(const FString& JsonString, FIU_PityShopSnapshot& OutSnapshot);
	static bool ParsePityRedeemResponseFromJson(const FString& JsonString, FIU_PityShopSnapshot& OutPity, FIU_InventorySnapshot& OutInv, bool& bOutHasInv);
	static bool ParseQuestSnapshotFromJson(const FString& JsonString, FIU_QuestSnapshot& OutSnapshot);
	static bool ParseQuestClaimResponseFromJson(const FString& JsonString, FIU_QuestSnapshot& OutQuests, FIU_InventorySnapshot& OutInv, bool& bOutHasInv);
	static FString SerializeSavePayloadToJson(const FIU_CharacterSavePayload& Payload);

	// ========================================================================
	// Configuration & Telemetry
	// ========================================================================

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	bool IsBackendBridgeEnabled() const { return bBackendBridgeEnabled; }

	UFUNCTION(BlueprintCallable, Category = "IU|Backend")
	void SetBackendBridgeEnabled(bool bEnabled) { bBackendBridgeEnabled = bEnabled; }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	bool ShouldRequireBackendTicket() const { return bRequireBackendTicket; }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	FString GetBackendBaseURL() const { return BackendBaseURL; }

	UFUNCTION(BlueprintCallable, Category = "IU|Backend")
	void SetBackendBaseURL(const FString& InURL);

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	FString GetServerInstanceID() const { return ServerInstanceID; }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	FString GetServerZoneID() const { return ServerZoneID; }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	int32 GetServerChannelID() const { return ServerChannelID; }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	FString GetServerPublicAddr() const { return ServerPublicAddr; }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	bool IsClientAuthenticated() const { return !ClientAccessToken.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	FString GetLoggedInUsername() const { return LoggedInUsername; }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	const TArray<FIU_BackendCharacterSummary>& GetCachedCharacters() const { return CachedCharacters; }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	double GetLastBridgeLatencyMs() const { return LastBridgeLatencyMs; }

	// ========================================================================
	// Client Edge REST Operations (/v1/*)
	// ========================================================================

	void RegisterAccount(
		const FString& Username,
		const FString& Email,
		const FString& Password,
		TFunction<void(bool bSuccess, const FString& ErrorMessage)> Callback);

	void LoginAccount(
		const FString& Username,
		const FString& Password,
		TFunction<void(bool bSuccess, const FString& ErrorMessage)> Callback);

	void FetchCharacters(
		TFunction<void(bool bSuccess, const TArray<FIU_BackendCharacterSummary>& Characters, const FString& ErrorMessage)> Callback);

	void CreateCharacter(
		const FString& CharacterName,
		const FString& RaceID,
		TFunction<void(bool bSuccess, const FIU_BackendCharacterSummary& Created, const FString& ErrorMessage)> Callback);

	void RequestEnterWorldAndTravel(
		const FString& CharacterID,
		APlayerController* TravelController,
		TFunction<void(bool bSuccess, const FString& TicketID, const FString& ErrorMessage)> Callback);

	/** Automated one-shot Login-or-Register -> Select-or-Create Character -> EnterWorld ClientTravel (used by -AutoAuthUser= and Quick Play UI). */
	void QuickAuthenticateAndEnterWorld(
		const FString& Username,
		const FString& Password,
		const FString& DesiredCharName,
		const FString& DesiredRaceID,
		APlayerController* TravelController,
		TFunction<void(bool bSuccess, const FString& StatusMessage)> OnStatus);

	/** Invoked by AIU_PlayerController::BeginPlay on local client to trigger -AutoAuthUser= once per process session. */
	void TryTriggerCommandLineAutoAuth(APlayerController* LocalPC);

	// ========================================================================
	// Authoritative Dedicated Server Bridge Operations (/internal/v1/* + HMAC)
	// ========================================================================

	void ClaimCharacterLease(
		const FString& TicketID,
		TFunction<void(bool bSuccess, const FIU_BackendCharacterSummary& LoadedState, const FString& ErrorMessage)> Callback);

	void RenewActiveLeases(
		const TArray<FIU_LeaseRenewalEntry>& ActiveLeases,
		TFunction<void(bool bSuccess, const TArray<FString>& RevokedCharacterIDs)> Callback);

	void SaveCharacterCheckpoint(
		const FIU_CharacterSavePayload& Payload,
		TFunction<void(bool bSuccess, const FString& ErrorMessage)> Callback = nullptr);

	void GrantCharacterEXP(
		const FString& CharacterID,
		int64 FencingEpoch,
		float OutgoingDamage,
		bool bKilledTarget,
		const FString& IdempotencyKey,
		TFunction<void(bool bSuccess, const FIU_BackendCharacterSummary& UpdatedState, const FString& ErrorMessage)> Callback,
		int32 NearbyPartyMembers = 1);

	void AllocateCharacterPotential(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& StatTypeCode,
		TFunction<void(bool bSuccess, const FIU_BackendCharacterSummary& UpdatedState, const FString& ErrorMessage)> Callback);

	void ReleaseCharacterLease(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FIU_CharacterSavePayload* OptionalFinalSave);

	// ========================================================================
	// Step 4 Authoritative Inventory, 8-Slot Equipment, CSPRNG Loot & Economy
	// ========================================================================

	void FetchCharacterInventory(
		const FString& CharacterID,
		int64 FencingEpoch,
		TFunction<void(bool bSuccess, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void EquipInventoryItem(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& ItemInstanceID,
		TFunction<void(bool bSuccess, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void UnequipEquipmentSlot(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& SlotID,
		TFunction<void(bool bSuccess, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void UseInventoryConsumable(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& OptionalItemInstanceID,
		TFunction<void(bool bSuccess, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void ToggleInventoryItemLock(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& ItemInstanceID,
		bool bLocked,
		TFunction<void(bool bSuccess, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void SellInventoryItem(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& ItemInstanceID,
		int32 Quantity,
		const FString& IdempotencyKey,
		TFunction<void(bool bSuccess, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void EnhanceInventoryEquipment(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& ItemInstanceID,
		const FString& IdempotencyKey,
		TFunction<void(bool bSuccess, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void RollMonsterLoot(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& SourceEntityID,
		const FString& IdempotencyKey,
		TFunction<void(bool bSuccess, const FIU_MobLootDropSummary& LootResult, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	// ========================================================================
	// Step 5 Authoritative Party, Guild, Multi-Channel Chat & P2P Trade Bridge
	// ========================================================================

	void FetchPartyState(
		const FString& CharacterID,
		const FString& CharacterName,
		int64 FencingEpoch,
		int32 Level,
		float HP,
		float MaxHP,
		float Energy,
		float MaxEnergy,
		TFunction<void(bool bSuccess, const FIU_PartySnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void InviteToParty(
		const FString& CharacterID,
		const FString& CharacterName,
		int32 Level,
		int64 FencingEpoch,
		const FString& TargetCharID,
		const FString& TargetName,
		TFunction<void(bool bSuccess, const FIU_PartySnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void RespondPartyInvite(
		const FString& CharacterID,
		const FString& CharacterName,
		int32 Level,
		int64 FencingEpoch,
		const FString& InviteID,
		bool bAccept,
		TFunction<void(bool bSuccess, const FIU_PartySnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void LeaveParty(
		const FString& CharacterID,
		int64 FencingEpoch,
		TFunction<void(bool bSuccess, const FIU_PartySnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void KickPartyMember(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& TargetCharID,
		TFunction<void(bool bSuccess, const FIU_PartySnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void PromotePartyLeader(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& TargetCharID,
		TFunction<void(bool bSuccess, const FIU_PartySnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void FetchGuildSnapshot(
		const FString& CharacterID,
		int64 FencingEpoch,
		TFunction<void(bool bSuccess, const FIU_GuildSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void CreateGuild(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& GuildName,
		const FString& GuildTag,
		const FString& MOTD,
		const FString& IdempotencyKey,
		TFunction<void(bool bSuccess, const FIU_GuildSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void JoinGuild(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& GuildID,
		TFunction<void(bool bSuccess, const FIU_GuildSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void LeaveGuild(
		const FString& CharacterID,
		int64 FencingEpoch,
		TFunction<void(bool bSuccess, const FIU_GuildSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void KickGuildMember(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& TargetCharID,
		TFunction<void(bool bSuccess, const FIU_GuildSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void SetGuildMemberRole(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& TargetCharID,
		const FString& NewRole,
		TFunction<void(bool bSuccess, const FIU_GuildSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void UpdateGuildMOTD(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& NewMOTD,
		TFunction<void(bool bSuccess, const FIU_GuildSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void DonateGuildCoins(
		const FString& CharacterID,
		int64 FencingEpoch,
		int64 AmountCoin,
		const FString& IdempotencyKey,
		TFunction<void(bool bSuccess, const FIU_GuildSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void FetchTradeSession(
		const FString& CharacterID,
		int64 FencingEpoch,
		TFunction<void(bool bSuccess, const FIU_TradeSessionSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void RequestPlayerTrade(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& RecipientCharID,
		TFunction<void(bool bSuccess, const FIU_TradeSessionSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void RespondPlayerTrade(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& TradeID,
		bool bAccept,
		TFunction<void(bool bSuccess, const FIU_TradeSessionSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void UpdateTradeOffer(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& TradeID,
		int64 CoinOffer,
		const TArray<FString>& ItemInstanceIDs,
		TFunction<void(bool bSuccess, const FIU_TradeSessionSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void LockTradeOffer(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& TradeID,
		TFunction<void(bool bSuccess, const FIU_TradeSessionSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void ConfirmPlayerTrade(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& TradeID,
		TFunction<void(bool bSuccess, const FIU_TradeSessionSnapshot& TradeSnap, const FIU_InventorySnapshot& InvSnap, bool bHasInv, const FString& ErrorMessage)> Callback);

	void CancelPlayerTrade(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& TradeID,
		TFunction<void(bool bSuccess, const FIU_TradeSessionSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void SendSocialChatMessage(
		const FString& CharacterID,
		const FString& CharacterName,
		int64 FencingEpoch,
		const FString& Channel,
		const FString& Content,
		const FVector& SenderPos,
		const FString& TargetName,
		TFunction<void(bool bSuccess, const FIU_ChatMessageView& SentMsg, const FString& ErrorMessage)> Callback);

	// ========================================================================
	// Step 6 Multi-Zone / Channel Handoff, World Boss Settlement, Pity & Quests
	// ========================================================================

	void SendWorldHeartbeat(
		int32 OnlineCCU,
		bool bBossAlive,
		const FString& BossName,
		float BossHPRatio,
		TFunction<void(bool bSuccess, const FString& ErrorMessage)> Callback = nullptr);

	void FetchWorldChannels(
		TFunction<void(bool bSuccess, const TArray<FIU_ZoneChannelView>& Channels, const FString& ErrorMessage)> Callback);

	void RequestZoneTransfer(
		const FIU_CharacterSavePayload& Checkpoint,
		const FString& TargetZoneID,
		int32 TargetChannelID,
		TFunction<void(bool bSuccess, const FString& ServerAddr, const FString& TicketID, const FString& ErrorMessage)> Callback);

	void SettleWorldBossKill(
		const FString& BossID,
		int32 FightDurationMs,
		const FString& IdempotencyKey,
		const TArray<FIU_BossParticipantReport>& Reports,
		TFunction<void(bool bSuccess, const FIU_BossKillSettleSummary& Summary, const FString& ErrorMessage)> Callback);

	void FetchPityShopSnapshot(
		const FString& CharacterID,
		int64 FencingEpoch,
		TFunction<void(bool bSuccess, const FIU_PityShopSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void RedeemPityShopEntry(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& EntryID,
		const FString& IdempotencyKey,
		TFunction<void(bool bSuccess, const FIU_PityShopSnapshot& PitySnap, const FIU_InventorySnapshot& InvSnap, bool bHasInv, const FString& ErrorMessage)> Callback);

	void FetchQuestSnapshot(
		const FString& CharacterID,
		int64 FencingEpoch,
		TFunction<void(bool bSuccess, const FIU_QuestSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

	void ClaimQuestReward(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& QuestID,
		const FString& IdempotencyKey,
		TFunction<void(bool bSuccess, const FIU_QuestSnapshot& QuestSnap, const FIU_InventorySnapshot& InvSnap, bool bHasInv, const FString& ErrorMessage)> Callback);

	void RepeatQuest(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& QuestID,
		TFunction<void(bool bSuccess, const FIU_QuestSnapshot& Snapshot, const FString& ErrorMessage)> Callback);

private:
	void SendJsonRequest(
		const FString& Verb,
		const FString& EndpointPath,
		const FString& JsonBody,
		bool bUseBearerAuth,
		bool bUseServerHMAC,
		TFunction<void(int32 HttpCode, const FString& ResponseBody, bool bConnected)> Callback);

	static FString ExtractErrorFromJson(const FString& ResponseBody, int32 HttpCode);

	FString BackendBaseURL = TEXT("http://127.0.0.1:8080");
	FString InternalHMACSecret = TEXT("iu_step3_internal_server_hmac_2026");
	FString ServerInstanceID = TEXT("ue5-ded-7777");
	FString ServerZoneID = TEXT("zone.p01_west_hub");
	int32 ServerChannelID = 1;
	FString ServerPublicAddr = TEXT("127.0.0.1:7777");

	bool bBackendBridgeEnabled = false;
	bool bRequireBackendTicket = false;
	bool bAutoAuthDispatched = false;

	FString AutoAuthUsername;
	FString AutoAuthPassword = TEXT("Step3HeroPass!2026");
	FString AutoAuthCharacterName;
	FString AutoAuthRaceID = TEXT("race.zenith_warrior");

	FString ClientAccessToken;
	FString ClientRefreshToken;
	FString LoggedInUsername;
	TArray<FIU_BackendCharacterSummary> CachedCharacters;

	double LastBridgeLatencyMs = 0.0;
	int32 TotalBridgeCalls = 0;
	int32 FailedBridgeCalls = 0;
	TArray<double> BridgeLatencySamplesMs;
};
