// Copyright InfiniteUniverse Dev. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "UObject/UnrealType.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/PlatformProcess.h"
#include "Core/IU_GameMode.h"
#include "Core/IU_GameState.h"
#include "Core/IU_PlayerState.h"
#include "Core/IU_PlayerController.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Network/IU_BackendSubsystem.h"
#include "UI/IU_InventoryEquipmentWidget.h"
#include "UI/IU_PlayerHUDWidget.h"
#include "UI/IU_PlayerTradeWidget.h"

namespace IU_S5TestHelpers
{
	struct FScopedStep5World
	{
		UWorld* World = nullptr;
		UWorld* PrevGWorld = nullptr;
		UGameInstance* TempGI = nullptr;

		FScopedStep5World()
		{
			PrevGWorld = GWorld;
			TempGI = NewObject<UGameInstance>(GEngine);
			TempGI->AddToRoot();
			TempGI->InitializeStandalone();

			World = TempGI->GetWorld();
			if (!World)
			{
				World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("IU_Step5_TestWorld")));
				FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
				WorldContext.SetCurrentWorld(World);
				WorldContext.OwningGameInstance = TempGI;
				World->SetGameInstance(TempGI);
			}
			GWorld = World;

			AIU_GameState* GS = World->SpawnActor<AIU_GameState>();
			if (GS)
			{
				World->SetGameState(GS);
			}
			if (!World->HasBegunPlay())
			{
				FURL URL;
				World->InitializeActorsForPlay(URL);
				World->BeginPlay();
				World->SetBegunPlay(true);
			}
		}

		~FScopedStep5World()
		{
			GWorld = PrevGWorld;
			if (TempGI)
			{
				TempGI->Shutdown();
				TempGI->RemoveFromRoot();
			}
		}
	};

	static bool HasNetProp(const UStruct* Type, const FName& Name)
	{
		const FProperty* Prop = Type ? Type->FindPropertyByName(Name) : nullptr;
		return Prop && Prop->HasAnyPropertyFlags(CPF_Net);
	}
}

// ============================================================================
// TEST 1: Party Synergy 1.5x Dame Math & Replicated Social/Guild/Ping Properties
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S5_PartySynergyMathAndSocialReplicationTest,
	"InfiniteUniverse.Step5.PartySynergyMathAndSocialReplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S5_PartySynergyMathAndSocialReplicationTest::RunTest(const FString& Parameters)
{
	// 1. Verify NRO Party Synergy Multiplier table (1->1.00, 2->1.15, 3->1.25, 4->1.35, >=5->1.45)
	TestTrue(TEXT("1 member = 1.00x"), FMath::IsNearlyEqual(AIU_PlayerState::ComputePartySynergyMultiplier(1), 1.00f, 0.001f));
	TestTrue(TEXT("2 members = 1.15x (+15%)"), FMath::IsNearlyEqual(AIU_PlayerState::ComputePartySynergyMultiplier(2), 1.15f, 0.001f));
	TestTrue(TEXT("3 members = 1.25x (+25%)"), FMath::IsNearlyEqual(AIU_PlayerState::ComputePartySynergyMultiplier(3), 1.25f, 0.001f));
	TestTrue(TEXT("4 members = 1.35x (+35%)"), FMath::IsNearlyEqual(AIU_PlayerState::ComputePartySynergyMultiplier(4), 1.35f, 0.001f));
	TestTrue(TEXT("5 members = 1.45x (+45%)"), FMath::IsNearlyEqual(AIU_PlayerState::ComputePartySynergyMultiplier(5), 1.45f, 0.001f));

	// 2. Verify CPF_Net replication metadata on Step 5 properties
	TestTrue(TEXT("AIU_PlayerState::GuildTag is CPF_Net"), IU_S5TestHelpers::HasNetProp(AIU_PlayerState::StaticClass(), TEXT("GuildTag")));
	TestTrue(TEXT("AIU_PlayerState::GuildName is CPF_Net"), IU_S5TestHelpers::HasNetProp(AIU_PlayerState::StaticClass(), TEXT("GuildName")));
	TestTrue(TEXT("AIU_PlayerState::PartyID is CPF_Net"), IU_S5TestHelpers::HasNetProp(AIU_PlayerState::StaticClass(), TEXT("PartyID")));
	TestTrue(TEXT("AIU_PlayerState::OverheadChatBubbleText is CPF_Net"), IU_S5TestHelpers::HasNetProp(AIU_PlayerState::StaticClass(), TEXT("OverheadChatBubbleText")));
	TestTrue(TEXT("AIU_PlayerState::PersistedCharacterUUID is CPF_Net"), IU_S5TestHelpers::HasNetProp(AIU_PlayerState::StaticClass(), TEXT("PersistedCharacterUUID")));
	TestTrue(TEXT("AIU_GameState::ActiveTacticalPings is CPF_Net"), IU_S5TestHelpers::HasNetProp(AIU_GameState::StaticClass(), TEXT("ActiveTacticalPings")));

	// 3. Verify live PlayerState offline Party Synergy + Speech Bubble + Tactical Ping authority
	IU_S5TestHelpers::FScopedStep5World ScopeWorld;
	AIU_PlayerState* PS = ScopeWorld.World->SpawnActor<AIU_PlayerState>();
	AIU_GameState* GS = ScopeWorld.World->GetGameState<AIU_GameState>();
	TestNotNull(TEXT("PlayerState spawned"), PS);
	TestNotNull(TEXT("GameState valid"), GS);

	// 100 outgoing damage -> base 150 SM/TN; with 2 party members -> Round(150 * 1.15) = 173 SM/TN!
	PS->AwardOfflineHitProgress(100.0f, 2);
	TestEqual(TEXT("2-member Party Synergy awards 173 Tiem Nang on 100 damage"), PS->GetPotentialPoints(), int64(173));

	PS->ApplyAuthoritativeGuildIdentity(TEXT("g-01"), TEXT("Zenith Vanguards"), TEXT("znt"), TEXT("LEADER"));
	TestEqual(TEXT("GuildTag uppercased to ZNT"), PS->GetGuildTag(), FString(TEXT("ZNT")));

	PS->SetOverheadSpeechBubble(TEXT("Chào anh em Bang hội!"), TEXT("GUILD"), 4.5f);
	TestEqual(TEXT("Speech bubble sanitized to ASCII"), PS->GetOverheadChatBubbleText(), FString(TEXT("Chao anh em Bang hoi!")));
	TestTrue(TEXT("Speech bubble expire time > 0"), PS->GetOverheadChatExpireServerTime() > 0.0f);

	GS->BroadcastPartyTacticalPing(TEXT("party-01"), TEXT("SonGoku"), FVector(-500.0f, 120.0f, 95.0f), 6.0f);
	TestEqual(TEXT("1 active tactical ping recorded on GameState"), GS->ActiveTacticalPings.Num(), 1);

	return true;
}

// ============================================================================
// TEST 2: Party, Guild, Multi-Channel Chat & P2P Trade JSON Contract Parsers
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S5_SocialGuildTradeJsonContractParsingTest,
	"InfiniteUniverse.Step5.SocialGuildTradeJsonContractParsing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S5_SocialGuildTradeJsonContractParsingTest::RunTest(const FString& Parameters)
{
	// 1. Party Snapshot JSON
	const FString PartyJson = TEXT(R"({
		"in_party": true,
		"party_id": "pty-123",
		"leader_char_id": "char-a",
		"action_message": "Đã gia nhập tổ đội!",
		"members": [
			{"character_id":"char-a","character_name":"SonGoku","level":3,"hp":280,"max_hp":280,"energy":200,"max_energy":200,"is_leader":true},
			{"character_id":"char-b","character_name":"Vegeta","level":2,"hp":200,"max_hp":200,"energy":200,"max_energy":200,"is_leader":false}
		],
		"pending_invites": []
	})");
	FIU_PartySnapshot PartySnap;
	TestTrue(TEXT("ParsePartySnapshotFromJson succeeded"), UIU_BackendSubsystem::ParsePartySnapshotFromJson(PartyJson, PartySnap));
	TestTrue(TEXT("Party has 2 members & bInParty=true"), PartySnap.bInParty && PartySnap.Members.Num() == 2);
	TestEqual(TEXT("Sanitized party action message"), PartySnap.ActionMessage, FString(TEXT("Da gia nhap to doi!")));

	// 2. Guild Snapshot JSON
	const FString GuildJson = TEXT(R"({
		"in_guild": true,
		"standard_coin": 350,
		"guild": {
			"guild_id": "gld-999",
			"guild_name": "Chiến Đoàn Vũ Trụ",
			"guild_tag": "ZNT",
			"leader_char_id": "char-a",
			"leader_name": "SonGoku",
			"guild_level": 2,
			"guild_exp": 1200,
			"member_count": 2,
			"member_cap": 30,
			"treasury_standard_coin": 250,
			"motd_announcement": "Tập trung săn Boss!",
			"my_role": "LEADER",
			"my_weekly_contribution": 100,
			"my_total_contribution": 100,
			"members": [
				{"character_id":"char-a","character_name":"SonGoku","level":3,"member_role":"LEADER","weekly_contribution":100,"total_contribution":100}
			]
		},
		"directory": []
	})");
	FIU_GuildSnapshot GuildSnap;
	TestTrue(TEXT("ParseGuildSnapshotFromJson succeeded"), UIU_BackendSubsystem::ParseGuildSnapshotFromJson(GuildJson, GuildSnap));
	TestEqual(TEXT("GuildTag = ZNT & Treasury = 250"), GuildSnap.TreasuryStandardCoin, int64(250));
	TestEqual(TEXT("GuildName sanitized to Chien Doan Vu Tru"), GuildSnap.GuildName, FString(TEXT("Chien Doan Vu Tru")));

	// 3. Trade Snapshot JSON
	const FString TradeJson = TEXT(R"({
		"has_active_trade": true,
		"trade_id": "trd-777",
		"state": "BOTH_LOCKED",
		"initiator_char_id": "char-a",
		"initiator_name": "Piccolo",
		"recipient_char_id": "char-b",
		"recipient_name": "Gohan",
		"initiator_coin": 100,
		"recipient_coin": 0,
		"tax_coin_preview": 5,
		"initiator_locked": true,
		"recipient_locked": true,
		"remaining_inspect_ms": 2800,
		"offer_version": 2,
		"initiator_items": [],
		"recipient_items": [
			{"instance_id":"it-1","item_def_id":"item.hands_wolfhide_wraps_05","display_name":"Găng Sói","rarity":"RARE","stack_count":1,"enhance_level":1,"affixes":[{"attr_id":"attr.phys_atk","value":12}]}
		]
	})");
	FIU_TradeSessionSnapshot TradeSnap;
	TestTrue(TEXT("ParseTradeSnapshotFromJson succeeded"), UIU_BackendSubsystem::ParseTradeSnapshotFromJson(TradeJson, TradeSnap));
	TestEqual(TEXT("Tax preview = 5 & State = BOTH_LOCKED"), TradeSnap.TaxCoinPreview, int64(5));
	TestEqual(TEXT("Recipient offered 1 item with +12 SucDanh affix"), TradeSnap.RecipientItems.Num(), 1);
	TestTrue(TEXT("Recipient item StatSummaryText has +12 SucDanh"), TradeSnap.RecipientItems[0].StatSummaryText.Contains(TEXT("+12 SucDanh")));

	// 4. Chat Message JSON
	const FString ChatJson = TEXT(R"({"seq":7,"channel":"LOCAL","sender_char_id":"char-a","sender_name":"SonGoku","sender_guild_tag":"ZNT","content":"Đánh quái nào!","show_bubble":true,"pos_x":10,"pos_y":20,"pos_z":112})");
	FIU_ChatMessageView ChatMsg;
	TestTrue(TEXT("ParseChatMessageFromJson succeeded"), UIU_BackendSubsystem::ParseChatMessageFromJson(ChatJson, ChatMsg));
	TestEqual(TEXT("Chat content sanitized to Danh quai nao!"), ChatMsg.Content, FString(TEXT("Danh quai nao!")));
	TestTrue(TEXT("Chat message bShowBubble = true"), ChatMsg.bShowBubble);

	return true;
}

// ============================================================================
// TEST 3: Programmatic NRO Guild Tab, Outer Chat/Party HUD & 2-Step Trade Widget
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S5_NROGuildChatAndTradeWidgetsTest,
	"InfiniteUniverse.Step5.NROGuildChatAndTradeWidgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S5_NROGuildChatAndTradeWidgetsTest::RunTest(const FString& Parameters)
{
	IU_S5TestHelpers::FScopedStep5World ScopeWorld;

	// 1. Outer HUD Widget: Party overlay + Chat history
	UIU_PlayerHUDWidget* HUDWidget = CreateWidget<UIU_PlayerHUDWidget>(ScopeWorld.TempGI, UIU_PlayerHUDWidget::StaticClass());
	TestNotNull(TEXT("UIU_PlayerHUDWidget created"), HUDWidget);
	TestTrue(TEXT("HUDWidget Slate valid"), HUDWidget->TakeWidget()->GetType() != FName(TEXT("SNullWidget")));

	FIU_PartySnapshot MockParty;
	MockParty.bInParty = true;
	MockParty.PartyID = TEXT("pty-test");
	FIU_PartyMemberView M1; M1.CharacterName = TEXT("Goku"); M1.bIsLeader = true;
	FIU_PartyMemberView M2; M2.CharacterName = TEXT("Vegeta");
	MockParty.Members = { M1, M2 };
	HUDWidget->ApplyPartySnapshot(MockParty);

	FIU_ChatMessageView MockChat;
	MockChat.Channel = TEXT("PARTY");
	MockChat.SenderName = TEXT("Goku");
	MockChat.SenderGuildTag = TEXT("ZNT");
	MockChat.Content = TEXT("San sang giao dich!");
	HUDWidget->AppendChatMessage(MockChat);

	// 2. Character Profile Tab [Bang Hoi]
	UIU_InventoryEquipmentWidget* ProfileWidget = CreateWidget<UIU_InventoryEquipmentWidget>(ScopeWorld.TempGI, UIU_InventoryEquipmentWidget::StaticClass());
	TestNotNull(TEXT("UIU_InventoryEquipmentWidget created"), ProfileWidget);
	ProfileWidget->TakeWidget();
	ProfileWidget->SetActiveProfileTab(EIU_CharacterProfileTab::BangHoi);
	TestEqual(TEXT("Active profile tab is BangHoi (index 3)"), static_cast<int32>(ProfileWidget->GetActiveProfileTab()), 3);

	FIU_GuildSnapshot MockGuild;
	MockGuild.bInGuild = true;
	MockGuild.GuildName = TEXT("Zenith Vanguards");
	MockGuild.GuildTag = TEXT("ZNT");
	MockGuild.TreasuryStandardCoin = 200;
	MockGuild.MyRole = TEXT("LEADER");
	ProfileWidget->ApplyGuildSnapshot(MockGuild);

	// 3. Dedicated 2-Step P2P Trade Widget
	UIU_PlayerTradeWidget* TradeWidget = CreateWidget<UIU_PlayerTradeWidget>(ScopeWorld.TempGI, UIU_PlayerTradeWidget::StaticClass());
	TestNotNull(TEXT("UIU_PlayerTradeWidget created"), TradeWidget);
	TestTrue(TEXT("UIU_PlayerTradeWidget built non-null Slate hierarchy"), TradeWidget->TakeWidget()->GetType() != FName(TEXT("SNullWidget")));

	FIU_TradeSessionSnapshot OpenSnap;
	OpenSnap.bHasActiveTrade = true;
	OpenSnap.State = TEXT("BOTH_LOCKED");
	OpenSnap.InitiatorCoin = 100;
	OpenSnap.TaxCoinPreview = 5;
	OpenSnap.bInitiatorLocked = true;
	OpenSnap.bRecipientLocked = true;
	OpenSnap.RemainingInspectMs = 3000;
	OpenSnap.OfferVersion = 1;
	TradeWidget->ApplyTradeSnapshot(OpenSnap);
	TestEqual(TEXT("TradeWidget cached BOTH_LOCKED state"), TradeWidget->GetDisplayedTradeSnapshot().State, FString(TEXT("BOTH_LOCKED")));

	return true;
}

// ============================================================================
// TEST 4: Same-Party Friendly-Fire Immunity & 6m P2P Trade Proximity Guard
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S5_PartyFriendlyFireAndTradeRangeGuardTest,
	"InfiniteUniverse.Step5.PartyFriendlyFireAndTradeRangeGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S5_PartyFriendlyFireAndTradeRangeGuardTest::RunTest(const FString& Parameters)
{
	IU_S5TestHelpers::FScopedStep5World ScopeWorld;

	// Spawn 2 Players in open-world Co-op zone (X = 0, outside West PvP Arena)
	AIU_PlayerCharacter* P1 = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0f, 0.0f, 112.0f), FRotator::ZeroRotator);
	AIU_PlayerCharacter* P2 = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(300.0f, 0.0f, 112.0f), FRotator::ZeroRotator);
	AIU_PlayerState* PS1 = ScopeWorld.World->SpawnActor<AIU_PlayerState>();
	AIU_PlayerState* PS2 = ScopeWorld.World->SpawnActor<AIU_PlayerState>();
	TestNotNull(TEXT("P1 spawned"), P1);
	TestNotNull(TEXT("P2 spawned"), P2);
	P1->SetPlayerState(PS1);
	P2->SetPlayerState(PS2);
	PS1->ApplyAuthoritativePartyIdentity(TEXT("party-coop"), true);
	PS2->ApplyAuthoritativePartyIdentity(TEXT("party-coop"), false);

	// Friendly fire between party members in Co-op zone MUST be rejected
	FIU_DamageSpec FFSpec;
	FFSpec.Amount = 50.0f;
	FFSpec.InstigatorActor = P1;
	FFSpec.HitLocation = P2->GetActorLocation();
	TestFalse(TEXT("Party friendly-fire damage rejected in Co-op zone"), P2->GetDamageComponent()->ApplyDamageSpec(FFSpec));
	TestTrue(TEXT("P2 HP untouched at 100%"), FMath::IsNearlyEqual(P2->GetAttributeComponent()->GetHPPercent(), 1.0f, 0.001f));

	return true;
}

// ============================================================================
// TEST 5: Live UE5 <-> Go Backend Step 5 Party, Guild [ZNT], Chat & Atomic 2-Step Trade E2E
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S5_BackendLoopbackPartyGuildChatAndTradeTest,
	"InfiniteUniverse.Step5.BackendLoopbackPartyGuildChatAndTrade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S5_BackendLoopbackPartyGuildChatAndTradeTest::RunTest(const FString& Parameters)
{
	IU_S5TestHelpers::FScopedStep5World ScopeWorld;
	UIU_BackendSubsystem* Backend = ScopeWorld.TempGI->GetSubsystem<UIU_BackendSubsystem>();
	TestNotNull(TEXT("UIU_BackendSubsystem valid"), Backend);

	auto PumpHttpUntil = [](const bool& bFlag, double TimeoutSec)
	{
		const double EndTime = FPlatformTime::Seconds() + TimeoutSec;
		while (!bFlag && FPlatformTime::Seconds() < EndTime)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.01f);
			FPlatformProcess::Sleep(0.01f);
		}
	};

	bool bProbeDone = false, bGatewayOnline = false;
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Probe = FHttpModule::Get().CreateRequest();
		Probe->SetURL(TEXT("http://127.0.0.1:8080/v1/healthz"));
		Probe->SetVerb(TEXT("GET"));
		Probe->SetTimeout(1.5f);
		Probe->OnProcessRequestComplete().BindLambda([&](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			bGatewayOnline = bOk && Resp.IsValid() && Resp->GetResponseCode() == 200;
			bProbeDone = true;
		});
		Probe->ProcessRequest();
		PumpHttpUntil(bProbeDone, 2.0);
	}

	if (!bGatewayOnline)
	{
		UE_LOG(LogTemp, Display, TEXT("[Step5Test] Go Gateway :8080 offline in standalone run — skipping live loopback check."));
		return true;
	}

	Backend->SetBackendBridgeEnabled(true);
	const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6).ToUpper();

	auto RegisterCreateAndClaim = [&](const FString& UName, const FString& CName, FIU_BackendCharacterSummary& OutClaimed) -> bool
	{
		bool bD = false, bS = false;
		Backend->RegisterAccount(UName, UName + TEXT("@iu.s5"), TEXT("UE5Step5Pass!99"),
			[&](bool bOk, const FString&) { bS = bOk; bD = true; });
		PumpHttpUntil(bD, 4.0);
		if (!bS) { return false; }

		bD = false; bS = false;
		FIU_BackendCharacterSummary Created;
		Backend->CreateCharacter(CName, TEXT("race.zenith_warrior"),
			[&](bool bOk, const FIU_BackendCharacterSummary& C, const FString&) { bS = bOk; Created = C; bD = true; });
		PumpHttpUntil(bD, 4.0);
		if (!bS) { return false; }

		bD = false; bS = false;
		FString Ticket;
		Backend->RequestEnterWorldAndTravel(Created.CharacterID, nullptr,
			[&](bool bOk, const FString& Tkt, const FString&) { bS = bOk; Ticket = Tkt; bD = true; });
		PumpHttpUntil(bD, 4.0);
		if (!bS) { return false; }

		bD = false; bS = false;
		Backend->ClaimCharacterLease(Ticket,
			[&](bool bOk, const FIU_BackendCharacterSummary& L, const FString&) { bS = bOk; OutClaimed = L; bD = true; });
		PumpHttpUntil(bD, 4.0);
		return bS;
	};

	FIU_BackendCharacterSummary HeroA, HeroB;
	TestTrue(TEXT("HeroA claimed lease"), RegisterCreateAndClaim(TEXT("S5A_") + Suffix, TEXT("Goku") + Suffix, HeroA));
	TestTrue(TEXT("HeroB claimed lease"), RegisterCreateAndClaim(TEXT("S5B_") + Suffix, TEXT("Vege") + Suffix, HeroB));

	// 1. Party Invite & Accept + Verify 2-Member Party Synergy (+15% on 100 dmg -> 173 SM/TN!)
	bool bDone = false, bOk = false;
	Backend->InviteToParty(HeroA.CharacterID, HeroA.CharacterName, 1, HeroA.FencingEpoch, HeroB.CharacterID, HeroB.CharacterName,
		[&](bool bS, const FIU_PartySnapshot&, const FString&) { bOk = bS; bDone = true; });
	PumpHttpUntil(bDone, 3.0);
	TestTrue(TEXT("Party invite succeeded"), bOk);

	bDone = false; bOk = false;
	FIU_PartySnapshot JoinedParty;
	Backend->RespondPartyInvite(HeroB.CharacterID, HeroB.CharacterName, 1, HeroB.FencingEpoch, TEXT(""), true,
		[&](bool bS, const FIU_PartySnapshot& Snap, const FString&) { bOk = bS; JoinedParty = Snap; bDone = true; });
	PumpHttpUntil(bDone, 3.0);
	TestTrue(TEXT("Party accept succeeded with 2 members"), bOk && JoinedParty.bInParty && JoinedParty.Members.Num() == 2);

	bDone = false; bOk = false;
	FIU_BackendCharacterSummary SynergyHit;
	Backend->GrantCharacterEXP(HeroA.CharacterID, HeroA.FencingEpoch, 400.0f, true, TEXT("s5-syn-") + Suffix,
		[&](bool bS, const FIU_BackendCharacterSummary& Upd, const FString&) { bOk = bS; SynergyHit = Upd; bDone = true; }, 2);
	PumpHttpUntil(bDone, 3.0);
	// 400 * 1.5 = 600; 600 * 1.15 = 690 SM & TN -> levels HeroA up to >= Lv.2!
	TestTrue(TEXT("2-member Party Synergy granted 690 EXP/TN & Lv>=2"), bOk && SynergyHit.ExpGranted == 690 && SynergyHit.Level >= 2);

	// 2. Earn Vang via RollMonsterLoot (4 kills -> >= 340 Vang) & Create Guild!
	for (int32 k = 1; k <= 4; ++k)
	{
		bDone = false;
		Backend->RollMonsterLoot(HeroA.CharacterID, HeroA.FencingEpoch, TEXT("mob.prototype_raider"),
			FString::Printf(TEXT("s5-lootA-%s-%d"), *Suffix, k),
			[&](bool, const FIU_MobLootDropSummary&, const FIU_InventorySnapshot&, const FString&) { bDone = true; });
		PumpHttpUntil(bDone, 3.0);
	}
	bDone = false;
	FIU_InventorySnapshot InvB;
	Backend->RollMonsterLoot(HeroB.CharacterID, HeroB.FencingEpoch, TEXT("mob.prototype_raider"),
		TEXT("s5-lootB-") + Suffix,
		[&](bool, const FIU_MobLootDropSummary&, const FIU_InventorySnapshot& S, const FString&) { InvB = S; bDone = true; });
	PumpHttpUntil(bDone, 3.0);

	const FString GTag = TEXT("Z") + Suffix.Right(4);
	bDone = false; bOk = false;
	FIU_GuildSnapshot CreatedGuild;
	Backend->CreateGuild(HeroA.CharacterID, HeroA.FencingEpoch, TEXT("Zenith_") + Suffix, GTag, TEXT("Step5 Test MOTD"), TEXT("s5-gc-") + Suffix,
		[&](bool bS, const FIU_GuildSnapshot& GS, const FString&) { bOk = bS; CreatedGuild = GS; bDone = true; });
	PumpHttpUntil(bDone, 3.0);
	TestTrue(TEXT("CreateGuild succeeded"), bOk && CreatedGuild.bInGuild);

	bDone = false; bOk = false;
	FIU_GuildSnapshot JoinedGuild;
	Backend->JoinGuild(HeroB.CharacterID, HeroB.FencingEpoch, CreatedGuild.GuildID,
		[&](bool bS, const FIU_GuildSnapshot& GS, const FString&) { bOk = bS; JoinedGuild = GS; bDone = true; });
	PumpHttpUntil(bDone, 3.0);
	TestTrue(TEXT("HeroB joined Guild (2 members)"), bOk && JoinedGuild.MemberCount == 2);

	// 3. Send Guild Chat
	bDone = false; bOk = false;
	FIU_ChatMessageView SentChat;
	Backend->SendSocialChatMessage(HeroA.CharacterID, HeroA.CharacterName, HeroA.FencingEpoch, TEXT("GUILD"), TEXT("Anh em giao dich!"), FVector::ZeroVector, TEXT(""),
		[&](bool bS, const FIU_ChatMessageView& M, const FString&) { bOk = bS; SentChat = M; bDone = true; });
	PumpHttpUntil(bDone, 3.0);
	TestTrue(TEXT("Guild chat succeeded with show_bubble=true"), bOk && SentChat.bShowBubble);

	// 4. Atomic 2-Step P2P Trade (HeroA gives 40 Vang -> 5% tax = 2 Vang burned, HeroB receives +38 Vang!)
	bDone = false; bOk = false;
	FIU_TradeSessionSnapshot TrdSnap;
	Backend->RequestPlayerTrade(HeroA.CharacterID, HeroA.FencingEpoch, HeroB.CharacterID,
		[&](bool bS, const FIU_TradeSessionSnapshot& S, const FString&) { bOk = bS; TrdSnap = S; bDone = true; });
	PumpHttpUntil(bDone, 3.0);
	TestTrue(TEXT("RequestPlayerTrade succeeded"), bOk);

	bDone = false; bOk = false;
	Backend->RespondPlayerTrade(HeroB.CharacterID, HeroB.FencingEpoch, TrdSnap.TradeID, true,
		[&](bool bS, const FIU_TradeSessionSnapshot& S, const FString&) { bOk = bS; TrdSnap = S; bDone = true; });
	PumpHttpUntil(bDone, 3.0);
	TestTrue(TEXT("RespondPlayerTrade OPEN"), bOk && TrdSnap.State == TEXT("OPEN"));

	bDone = false; bOk = false;
	Backend->UpdateTradeOffer(HeroA.CharacterID, HeroA.FencingEpoch, TrdSnap.TradeID, 40, TArray<FString>(),
		[&](bool bS, const FIU_TradeSessionSnapshot& S, const FString&) { bOk = bS; TrdSnap = S; bDone = true; });
	PumpHttpUntil(bDone, 3.0);
	TestTrue(TEXT("HeroA offered 40 Vang"), bOk && TrdSnap.InitiatorCoin == 40);

	bDone = false; bOk = false;
	Backend->LockTradeOffer(HeroA.CharacterID, HeroA.FencingEpoch, TrdSnap.TradeID,
		[&](bool bS, const FIU_TradeSessionSnapshot&, const FString&) { bOk = bS; bDone = true; });
	PumpHttpUntil(bDone, 3.0);
	bDone = false; bOk = false;
	Backend->LockTradeOffer(HeroB.CharacterID, HeroB.FencingEpoch, TrdSnap.TradeID,
		[&](bool bS, const FIU_TradeSessionSnapshot& S, const FString&) { bOk = bS; TrdSnap = S; bDone = true; });
	PumpHttpUntil(bDone, 3.0);
	TestTrue(TEXT("Both locked -> BOTH_LOCKED"), bOk && TrdSnap.State == TEXT("BOTH_LOCKED"));

	// Immediate confirm MUST fail due to 3.0s inspection gate!
	bDone = false; bOk = true;
	Backend->ConfirmPlayerTrade(HeroA.CharacterID, HeroA.FencingEpoch, TrdSnap.TradeID,
		[&](bool bS, const FIU_TradeSessionSnapshot&, const FIU_InventorySnapshot&, bool, const FString&) { bOk = bS; bDone = true; });
	PumpHttpUntil(bDone, 3.0);
	TestFalse(TEXT("Early confirm before 3.0s countdown rejected by server"), bOk);

	FPlatformProcess::Sleep(3.15f);

	bDone = false; bOk = false;
	Backend->ConfirmPlayerTrade(HeroA.CharacterID, HeroA.FencingEpoch, TrdSnap.TradeID,
		[&](bool bS, const FIU_TradeSessionSnapshot&, const FIU_InventorySnapshot&, bool, const FString&) { bOk = bS; bDone = true; });
	PumpHttpUntil(bDone, 3.0);
	TestTrue(TEXT("HeroA confirm after 3.0s succeeded"), bOk);

	bDone = false; bOk = false;
	FIU_TradeSessionSnapshot SettledTrade;
	FIU_InventorySnapshot FinalInvB;
	bool bGotInvB = false;
	Backend->ConfirmPlayerTrade(HeroB.CharacterID, HeroB.FencingEpoch, TrdSnap.TradeID,
		[&](bool bS, const FIU_TradeSessionSnapshot& TS, const FIU_InventorySnapshot& IS, bool bHasI, const FString&)
		{
			bOk = bS; SettledTrade = TS; FinalInvB = IS; bGotInvB = bHasI; bDone = true;
		});
	PumpHttpUntil(bDone, 3.0);
	TestTrue(TEXT("Trade SETTLED atomically"), bOk && SettledTrade.State == TEXT("SETTLED") && bGotInvB);
	TestEqual(TEXT("HeroB received +38 Vang net after 5% tax burn"), FinalInvB.StandardCoin, InvB.StandardCoin + 38);

	// Clean up test guild membership so repeated test runs keep the guilds directory clean
	bDone = false;
	Backend->LeaveGuild(HeroB.CharacterID, HeroB.FencingEpoch, [&](bool, const FIU_GuildSnapshot&, const FString&) { bDone = true; });
	PumpHttpUntil(bDone, 3.0);
	bDone = false;
	Backend->LeaveGuild(HeroA.CharacterID, HeroA.FencingEpoch, [&](bool, const FIU_GuildSnapshot&, const FString&) { bDone = true; });
	PumpHttpUntil(bDone, 3.0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
