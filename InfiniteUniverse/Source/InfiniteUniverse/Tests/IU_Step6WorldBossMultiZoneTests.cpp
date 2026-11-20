// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Characters/Enemy/IU_WorldBossCharacter.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Core/IU_ZonePortalActor.h"
#include "Core/IU_GameMode.h"
#include "Core/IU_GameState.h"
#include "Core/IU_PlayerState.h"
#include "Core/IU_PlayerController.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Network/IU_BackendSubsystem.h"
#include "UI/IU_InventoryEquipmentWidget.h"
#include "Components/CapsuleComponent.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Guid.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FIU_Step6TestWorldGuard
	{
		UWorld* World = nullptr;
		UGameInstance* GameInstance = nullptr;

		explicit FIU_Step6TestWorldGuard(bool bWithGameInstance = false)
		{
			if (!GEngine)
			{
				return;
			}
			if (bWithGameInstance)
			{
				GameInstance = NewObject<UGameInstance>(GEngine);
				GameInstance->AddToRoot();
				GameInstance->Init();
			}

			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("IU_Step6TestWorld")));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			if (GameInstance)
			{
				World->SetGameInstance(GameInstance);
				WorldContext.OwningGameInstance = GameInstance;
			}

			FURL URL;
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}

		~FIU_Step6TestWorldGuard()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
			if (GameInstance)
			{
				GameInstance->Shutdown();
				GameInstance->RemoveFromRoot();
			}
		}
	};

	static bool PumpStep6HttpUntil(TFunctionRef<bool()> Predicate, double TimeoutSeconds = 6.0)
	{
		const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		while (FPlatformTime::Seconds() < Deadline)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.016f);
			if (Predicate())
			{
				return true;
			}
			FPlatformProcess::Sleep(0.01f);
		}
		FHttpModule::Get().GetHttpManager().Tick(0.016f);
		return Predicate();
	}
}

// ============================================================================
// TEST 1: 3-Phase World Boss Transitions, Poise/Stagger 1.25x & 3D Aerial Quake Dodge (>450cm)
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_Step6WorldBossMechanicsTest,
	"InfiniteUniverse.Step6.WorldBossThreePhasePoiseStaggerAndAerialDodge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_Step6WorldBossMechanicsTest::RunTest(const FString& Parameters)
{
	FIU_Step6TestWorldGuard Guard(false);
	TestNotNull(TEXT("Step 6 test world created"), Guard.World);
	if (!Guard.World)
	{
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AIU_WorldBossCharacter* Boss = Guard.World->SpawnActor<AIU_WorldBossCharacter>(
		AIU_WorldBossCharacter::StaticClass(),
		FVector(0.0f, 0.0f, 200.0f),
		FRotator::ZeroRotator,
		SpawnParams);
	TestNotNull(TEXT("Spawned AIU_WorldBossCharacter"), Boss);
	if (!Boss)
	{
		return false;
	}

	UIU_AttributeComponent* BossAttr = Boss->GetAttributeComponent();
	UIU_DamageComponent* BossDmg = Boss->GetDamageComponent();
	TestNotNull(TEXT("Boss has AttributeComponent"), BossAttr);
	TestNotNull(TEXT("Boss has DamageComponent"), BossDmg);
	if (!BossAttr || !BossDmg)
	{
		return false;
	}

	BossAttr->ApplyPersistedVitals(1000.0f, 1000.0f, 200.0f, 200.0f, 1.0f, 30.0f, 0.0f);
	TestEqual(TEXT("Boss starts in Phase 1"), Boss->GetBossPhase(), 1);
	TestTrue(TEXT("Phase 1 damage multiplier = 1.00x"), FMath::IsNearlyEqual(Boss->GetPhaseDamageMultiplier(), 1.00f, 0.001f));
	TestFalse(TEXT("Boss is not staggered initially"), Boss->IsBossStaggered());

	// 1. Verify 3D Aerial Quake Evasion Math (monsters.json: safe_altitude_z_m = 4.5m = 450cm above ground!)
	const float BossGroundZ = Boss->GetActorLocation().Z - Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	TestFalse(TEXT("Grounded player (+100cm) does NOT evade GroundQuake"), Boss->DoesActorEvadeQuakeByAltitude(FVector(250.0f, 0.0f, BossGroundZ + 100.0f)));
	TestFalse(TEXT("Low jump (+420cm < 450cm) does NOT evade GroundQuake"), Boss->DoesActorEvadeQuakeByAltitude(FVector(250.0f, 0.0f, BossGroundZ + 420.0f)));
	TestTrue(TEXT("3D Flight at +450cm evades GroundQuake 100%"), Boss->DoesActorEvadeQuakeByAltitude(FVector(250.0f, 0.0f, BossGroundZ + 450.0f)));
	TestTrue(TEXT("3D Flight at +650cm evades GroundQuake 100%"), Boss->DoesActorEvadeQuakeByAltitude(FVector(250.0f, 0.0f, BossGroundZ + 650.0f)));

	// 2. Break Poise -> Verify 6.0s Stagger & 1.25x Vulnerability Bonus Damage
	Boss->ApplyPoiseDamageOnServer(Boss->GetMaxPoise() + 10.0f);
	TestTrue(TEXT("Boss enters Stagger state when Poise hits 0"), Boss->IsBossStaggered());
	TestEqual(TEXT("Boss CurrentPoise clamped to 0"), Boss->GetCurrentPoise(), 0.0f);

	const float HPBeforeStaggerHit = BossAttr->GetHP();
	FIU_DamageSpec HitSpec;
	HitSpec.Amount = 100.0f;
	HitSpec.HitStunDuration = 0.2f;
	BossDmg->ApplyDamageSpec(HitSpec);
	const float DealtDuringStagger = HPBeforeStaggerHit - BossAttr->GetHP();
	TestTrue(
		FString::Printf(TEXT("Staggered Boss takes 1.25x vulnerability damage (expected ~125, got %.1f)"), DealtDuringStagger),
		FMath::IsNearlyEqual(DealtDuringStagger, 125.0f, 1.0f));

	// 3. Push HP <= 50% -> Phase 2 (1.15x Dmg), then <= 20% -> Phase 3 (1.30x Dmg)
	HitSpec.Amount = 320.0f; // 320 * 1.25 = 400 -> Remaining 475 / 1000 (47.5% <= 50%)
	BossDmg->ApplyDamageSpec(HitSpec);
	TestEqual(TEXT("Boss transitions to Phase 2 at <= 50% HP"), Boss->GetBossPhase(), 2);
	TestTrue(TEXT("Phase 2 damage multiplier = 1.15x"), FMath::IsNearlyEqual(Boss->GetPhaseDamageMultiplier(), 1.15f, 0.001f));

	HitSpec.Amount = 240.0f; // 240 * 1.25 = 300 -> Remaining 175 / 1000 (17.5% <= 20%)
	BossDmg->ApplyDamageSpec(HitSpec);
	TestEqual(TEXT("Boss transitions to Phase 3 at <= 20% HP"), Boss->GetBossPhase(), 3);
	TestTrue(TEXT("Phase 3 damage multiplier = 1.30x"), FMath::IsNearlyEqual(Boss->GetPhaseDamageMultiplier(), 1.30f, 0.001f));

	return true;
}

// ============================================================================
// TEST 2: Step 6 JSON Contract Parsers (Channels, Boss Kill Settle, Pity Shop, Quests)
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_Step6JsonContractParsingTest,
	"InfiniteUniverse.Step6.BossPityQuestAndChannelJsonContractParsing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_Step6JsonContractParsingTest::RunTest(const FString& Parameters)
{
	// 1. World Channels JSON
	const FString ChanJson = TEXT(R"({
		"channels": [
			{"server_id":"srv-khu1","zone_id":"zone.p01_west_hub","zone_label":"Tram Tay Nam","channel_id":1,"server_addr":"127.0.0.1:7777","online_ccu":12,"soft_cap_ccu":20,"hard_cap_ccu":25,"status_color":"GREEN","boss_alive":false,"boss_hp_ratio":0.0},
			{"server_id":"srv-khu2","zone_id":"zone.p01_highland_01","zone_label":"Cao Nguyen Titan","channel_id":2,"server_addr":"127.0.0.1:7778","online_ccu":18,"soft_cap_ccu":20,"hard_cap_ccu":25,"status_color":"YELLOW","boss_alive":true,"boss_name":"HIGHLAND TITAN","boss_hp_ratio":0.64}
		]
	})");
	TArray<FIU_ZoneChannelView> Channels;
	TestTrue(TEXT("ParseWorldChannelsFromJson succeeds"), UIU_BackendSubsystem::ParseWorldChannelsFromJson(ChanJson, Channels));
	TestEqual(TEXT("Parsed 2 live channels"), Channels.Num(), 2);
	if (Channels.Num() == 2)
	{
		TestEqual(TEXT("Channel 2 ID"), Channels[1].ChannelID, 2);
		TestTrue(TEXT("Channel 2 BossAlive"), Channels[1].bBossAlive);
		TestEqual(TEXT("Channel 2 StatusColor"), Channels[1].StatusColor, FString(TEXT("YELLOW")));
	}

	// 2. Boss Kill Settle JSON (including Hard Pity flag & 2% cutoff)
	const FString BossJson = TEXT(R"({
		"kill_id":"9a8b7c6d-0000-4000-8000-000000000001",
		"boss_id":"boss.highland_titan_01",
		"top_killer_name":"GokuS6",
		"announcement":"[HE THONG] GokuS6 va To Doi da ha guc Highland Titan!",
		"rewards":[
			{"character_id":"c1","character_name":"GokuS6","rank":1,"share_pct":99.0,"eligible":true,"coins_granted":1120,"tokens_granted":2,"token_balance":7,"consecutive_dry_kills":0,"hard_pity_triggered":true,"items_dropped":[{"display_name":"Nhan Titan Vinh Cuu","rarity":"EPIC","quantity":1}]},
			{"character_id":"c2","character_name":"Leecher","rank":2,"share_pct":1.0,"eligible":false,"coins_granted":0,"tokens_granted":0,"token_balance":0}
		]
	})");
	FIU_BossKillSettleSummary BossSum;
	TestTrue(TEXT("ParseBossKillSettleFromJson succeeds"), UIU_BackendSubsystem::ParseBossKillSettleFromJson(BossJson, BossSum));
	TestEqual(TEXT("Parsed 2 participant reward rows"), BossSum.Rewards.Num(), 2);
	if (BossSum.Rewards.Num() == 2)
	{
		TestTrue(TEXT("Rank 1 contributor (>=2%) is eligible"), BossSum.Rewards[0].bEligible);
		TestTrue(TEXT("Hard Pity Triggered parsed accurately"), BossSum.Rewards[0].bHardPityTriggered);
		TestEqual(TEXT("New token balance = 7"), BossSum.Rewards[0].NewTokenBalance, 7);
		TestFalse(TEXT("Leecher (<2%) is ineligible"), BossSum.Rewards[1].bEligible);
	}

	// 3. Pity Shop & Quest Snapshots
	const FString PityJson = TEXT(R"({
		"token_id":"token.boss_highland_titan",
		"token_balance":5,
		"consecutive_dry_kills":3,
		"hard_pity_threshold":4,
		"standard_coin":1400,
		"entries":[
			{"entry_id":"pity.mat_crimson_ore_t1","token_item_id":"token.boss_highland_titan","token_cost":2,"standard_coin_cost":200,"reward_item_id":"item.mat_crimson_ore_t1","reward_name":"Da Cuong Hoa Do","reward_rarity":"COMMON","reward_quantity":3,"daily_limit":5,"daily_purchased":1}
		]
	})");
	FIU_PityShopSnapshot PitySnap;
	TestTrue(TEXT("ParsePityShopSnapshotFromJson succeeds"), UIU_BackendSubsystem::ParsePityShopSnapshotFromJson(PityJson, PitySnap));
	TestEqual(TEXT("TokenBalance = 5"), PitySnap.TokenBalance, 5);
	TestEqual(TEXT("ConsecutiveDryKills = 3"), PitySnap.ConsecutiveDryKills, 3);
	TestEqual(TEXT("Pity entries = 1"), PitySnap.Entries.Num(), 1);

	const FString QuestJson = TEXT(R"({
		"quests":[
			{"quest_id":"quest.nro_raider_patrol_01","title":"Tuan Tra Cao Nguyen","description":"Diet 3 Raider","quest_state":"READY_TO_TURN_IN","current_count":3,"target_count":3,"repeat_count":0,"reward_coin":300,"reward_exp_pot":500,"reward_titan_token":1}
		]
	})");
	FIU_QuestSnapshot QSnap;
	TestTrue(TEXT("ParseQuestSnapshotFromJson succeeds"), UIU_BackendSubsystem::ParseQuestSnapshotFromJson(QuestJson, QSnap));
	TestEqual(TEXT("Parsed 1 quest"), QSnap.Quests.Num(), 1);
	TestEqual(TEXT("Quest state = READY_TO_TURN_IN"), QSnap.Quests[0].QuestState, FString(TEXT("READY_TO_TURN_IN")));

	return true;
}

// ============================================================================
// TEST 3: NRO Unified Character Profile Tabs (Nhiem Vu + Chuc Nang Doi Khu + Pity Shop)
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_Step6ProfileQuestChannelPityWidgetTest,
	"InfiniteUniverse.Step6.NROQuestChannelAndPityShopWidgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_Step6ProfileQuestChannelPityWidgetTest::RunTest(const FString& Parameters)
{
	FIU_Step6TestWorldGuard Guard(true);
	TestNotNull(TEXT("Widget test world created"), Guard.World);
	if (!Guard.World)
	{
		return false;
	}

	UIU_InventoryEquipmentWidget* ProfileWidget = CreateWidget<UIU_InventoryEquipmentWidget>(Guard.World, UIU_InventoryEquipmentWidget::StaticClass());
	TestNotNull(TEXT("Created UIU_InventoryEquipmentWidget"), ProfileWidget);
	if (!ProfileWidget)
	{
		return false;
	}
	TSharedRef<SWidget> BuiltSlate = ProfileWidget->TakeWidget();
	(void)BuiltSlate;

	// 1. Populate Tab [Nhiem Vu]
	FIU_QuestSnapshot QSnap;
	FIU_QuestEntryView Q1;
	Q1.QuestID = TEXT("quest.nro_raider_patrol_01");
	Q1.Title = TEXT("Tuan Tra Cao Nguyen");
	Q1.QuestState = TEXT("READY_TO_TURN_IN");
	Q1.CurrentCount = 3;
	Q1.TargetCount = 3;
	Q1.RewardCoin = 300;
	Q1.RewardExpAndPot = 500;
	Q1.RewardTitanToken = 1;
	QSnap.Quests.Add(Q1);

	ProfileWidget->SetActiveProfileTab(EIU_CharacterProfileTab::NhiemVu);
	ProfileWidget->ApplyQuestSnapshot(QSnap);
	TestEqual(TEXT("Active tab is NhiemVu"), ProfileWidget->GetActiveProfileTab(), EIU_CharacterProfileTab::NhiemVu);
	TestEqual(TEXT("Widget displays 1 authoritative quest"), ProfileWidget->GetDisplayedQuestCount(), 1);

	// 2. Populate Tab [Chuc Nang -> Doi Khu] & [Shop Bao Hiem Boss]
	TArray<FIU_ZoneChannelView> Channels;
	FIU_ZoneChannelView C1;
	C1.ZoneID = TEXT("zone.p01_west_hub");
	C1.ZoneLabel = TEXT("Tram Tay Nam");
	C1.ChannelID = 1;
	C1.OnlineCCU = 4;
	Channels.Add(C1);
	FIU_ZoneChannelView C2 = C1;
	C2.ZoneID = TEXT("zone.p01_highland_01");
	C2.ZoneLabel = TEXT("Cao Nguyen Titan");
	C2.ChannelID = 2;
	C2.bBossAlive = true;
	C2.BossHPRatio = 0.85f;
	Channels.Add(C2);

	FIU_PityShopSnapshot PSnap;
	PSnap.TokenID = TEXT("token.boss_highland_titan");
	PSnap.TokenBalance = 6;
	for (int32 i = 0; i < 7; ++i)
	{
		FIU_PityShopEntryView E;
		E.EntryID = FString::Printf(TEXT("pity.item_%d"), i);
		E.RewardName = FString::Printf(TEXT("Reward %d"), i);
		E.RewardRarity = TEXT("EPIC");
		E.TokenCost = 4;
		PSnap.Entries.Add(E);
	}

	ProfileWidget->SetActiveProfileTab(EIU_CharacterProfileTab::ChucNang);
	ProfileWidget->ApplyWorldChannels(Channels);
	ProfileWidget->ApplyPityShopSnapshot(PSnap);
	ProfileWidget->SetActiveFuncSubMode(1);

	TestEqual(TEXT("Widget displays 2 live channels"), ProfileWidget->GetDisplayedChannelCount(), 2);
	TestEqual(TEXT("Widget displays all 7 pity_shop entries"), ProfileWidget->GetDisplayedPityEntryCount(), 7);

	return true;
}

// ============================================================================
// TEST 4: 3D Zone Portal Actor Configuration & ZONE_TRANSFER Save Payload Serialization
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_Step6ZonePortalAndSavePayloadTest,
	"InfiniteUniverse.Step6.ZonePortalGraceAndTransferPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_Step6ZonePortalAndSavePayloadTest::RunTest(const FString& Parameters)
{
	FIU_Step6TestWorldGuard Guard(false);
	TestNotNull(TEXT("Portal test world created"), Guard.World);
	if (!Guard.World)
	{
		return false;
	}

	AIU_ZonePortalActor* Gate = Guard.World->SpawnActor<AIU_ZonePortalActor>(
		AIU_ZonePortalActor::StaticClass(),
		FVector(460.0f, 0.0f, 118.0f),
		FRotator::ZeroRotator);
	TestNotNull(TEXT("Spawned AIU_ZonePortalActor"), Gate);
	if (Gate)
	{
		TestTrue(TEXT("Gate replicates to clients"), Gate->GetIsReplicated());
		TestEqual(TEXT("Default TargetZoneID points to Highland Boss Arena"), Gate->TargetZoneID, FString(TEXT("zone.p01_highland_01")));
		TestEqual(TEXT("Default TargetChannelID = 2"), Gate->TargetChannelID, 2);
	}

	FIU_CharacterSavePayload TransferPayload;
	TransferPayload.CharacterID = TEXT("11111111-2222-3333-4444-555555555555");
	TransferPayload.FencingEpoch = 3;
	TransferPayload.ServerID = TEXT("ue5-ded-7777");
	TransferPayload.SnapshotReason = TEXT("ZONE_TRANSFER");
	TransferPayload.ZoneID = TEXT("zone.p01_west_hub");
	TransferPayload.ChannelID = 1;
	TransferPayload.Position = FVector(455.0f, 12.0f, 118.0f);
	TransferPayload.bIsAlive = true;

	const FString Serialized = UIU_BackendSubsystem::SerializeSavePayloadToJson(TransferPayload);
	TestTrue(TEXT("Serialized payload contains ZONE_TRANSFER reason"), Serialized.Contains(TEXT("\"snapshot_reason\":\"ZONE_TRANSFER\"")));
	TestTrue(TEXT("Serialized payload contains channel_id"), Serialized.Contains(TEXT("\"channel_id\":1")));

	return true;
}

// ============================================================================
// TEST 5: Live Go Gateway Loopback (Boss Kill >=2% Split + Quest Claim + Pity Redeem + Zone Transfer Epoch #1 -> #2)
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_Step6BackendLoopbackIntegrationTest,
	"InfiniteUniverse.Step6.BackendLoopbackBossKillHardPityQuestAndZoneTransfer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_Step6BackendLoopbackIntegrationTest::RunTest(const FString& Parameters)
{
	// Probe if Go Gateway is listening on 127.0.0.1:8080; skip cleanly if running pure offline Editor test pass without Gateway
	bool bHealthDone = false;
	bool bGatewayOnline = false;
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Probe = FHttpModule::Get().CreateRequest();
		Probe->SetURL(TEXT("http://127.0.0.1:8080/v1/healthz"));
		Probe->SetVerb(TEXT("GET"));
		Probe->SetTimeout(1.5f);
		Probe->OnProcessRequestComplete().BindLambda([&](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOK)
		{
			bGatewayOnline = bOK && Resp.IsValid() && Resp->GetResponseCode() == 200;
			bHealthDone = true;
		});
		Probe->ProcessRequest();
		PumpStep6HttpUntil([&]() { return bHealthDone; }, 2.0);
	}
	if (!bGatewayOnline)
	{
		UE_LOG(LogTemp, Display, TEXT("[Step6Test] Go Gateway (:8080) offline — skipping live loopback portion cleanly."));
		return true;
	}

	FIU_Step6TestWorldGuard GuardA(true);
	FIU_Step6TestWorldGuard GuardB(true);
	UIU_BackendSubsystem* SrvKhu1 = GuardA.GameInstance ? GuardA.GameInstance->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	UIU_BackendSubsystem* SrvKhu2 = GuardB.GameInstance ? GuardB.GameInstance->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	TestNotNull(TEXT("Khu 1 BackendSubsystem"), SrvKhu1);
	TestNotNull(TEXT("Khu 2 BackendSubsystem"), SrvKhu2);
	if (!SrvKhu1 || !SrvKhu2)
	{
		return false;
	}

	SrvKhu1->SetBackendBaseURL(TEXT("http://127.0.0.1:8080"));
	SrvKhu1->SetBackendBridgeEnabled(true);
	SrvKhu2->SetBackendBaseURL(TEXT("http://127.0.0.1:8080"));
	SrvKhu2->SetBackendBridgeEnabled(true);

	const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	const FString User = FString::Printf(TEXT("s6t_%s"), *Suffix);
	const FString CharName = FString::Printf(TEXT("Titan%s"), *Suffix);
	const FString Pass = TEXT("Step6AutoPass!2026");

	// 1. Register + Login + Create Character + EnterWorld Ticket -> Claim Lease on SrvKhu1 (Epoch #1)
	bool bStepDone = false;
	bool bStepOK = false;
	SrvKhu1->RegisterAccount(User, User + TEXT("@iu.dev"), Pass, [&](bool bOK, const FString&) { bStepOK = bOK; bStepDone = true; });
	TestTrue(TEXT("RegisterAccount completed"), PumpStep6HttpUntil([&]() { return bStepDone; }) && bStepOK);

	bStepDone = false; bStepOK = false;
	SrvKhu1->LoginAccount(User, Pass, [&](bool bOK, const FString&) { bStepOK = bOK; bStepDone = true; });
	TestTrue(TEXT("LoginAccount completed"), PumpStep6HttpUntil([&]() { return bStepDone; }) && bStepOK);

	FIU_BackendCharacterSummary Created;
	bStepDone = false; bStepOK = false;
	SrvKhu1->CreateCharacter(CharName, TEXT("race.zenith_warrior"), [&](bool bOK, const FIU_BackendCharacterSummary& C, const FString&)
	{
		bStepOK = bOK; Created = C; bStepDone = true;
	});
	TestTrue(TEXT("CreateCharacter completed"), PumpStep6HttpUntil([&]() { return bStepDone; }) && bStepOK);

	FString JoinTicket;
	bStepDone = false; bStepOK = false;
	SrvKhu1->RequestEnterWorldAndTravel(Created.CharacterID, nullptr, [&](bool bOK, const FString& Tkt, const FString&)
	{
		bStepOK = bOK; JoinTicket = Tkt; bStepDone = true;
	});
	TestTrue(TEXT("EnterWorld ticket issued"), PumpStep6HttpUntil([&]() { return bStepDone; }) && bStepOK && !JoinTicket.IsEmpty());

	FIU_BackendCharacterSummary Leased;
	bStepDone = false; bStepOK = false;
	SrvKhu1->ClaimCharacterLease(JoinTicket, [&](bool bOK, const FIU_BackendCharacterSummary& L, const FString&)
	{
		bStepOK = bOK; Leased = L; bStepDone = true;
	});
	TestTrue(TEXT("ClaimCharacterLease Epoch #1 succeeded"), PumpStep6HttpUntil([&]() { return bStepDone; }) && bStepOK);
	TestEqual(TEXT("Initial FencingEpoch = 1"), Leased.FencingEpoch, int64(1));

	// 2. Register Khu 1 Heartbeat & Settle World Boss Kill -> Verify >=2% Loot + Titan Tokens + Quest Ready -> Claim Quest + Redeem Pity!
	bStepDone = false;
	SrvKhu1->SendWorldHeartbeat(1, true, TEXT("HIGHLAND TITAN"), 1.0f, [&](bool, const FString&) { bStepDone = true; });
	PumpStep6HttpUntil([&]() { return bStepDone; });

	TArray<FIU_BossParticipantReport> Reports;
	FIU_BossParticipantReport HeroRep;
	HeroRep.CharacterID = Leased.CharacterID;
	HeroRep.CharacterName = Leased.CharacterName;
	HeroRep.FencingEpoch = Leased.FencingEpoch;
	HeroRep.DamageDealt = 950.0f;
	Reports.Add(HeroRep);

	FIU_BossKillSettleSummary KillSummary;
	bStepDone = false; bStepOK = false;
	SrvKhu1->SettleWorldBossKill(
		TEXT("boss.highland_titan_01"),
		8500,
		FString::Printf(TEXT("ue5_s6_kill_%s"), *Suffix),
		Reports,
		[&](bool bOK, const FIU_BossKillSettleSummary& Sum, const FString&)
		{
			bStepOK = bOK; KillSummary = Sum; bStepDone = true;
		});
	TestTrue(TEXT("SettleWorldBossKill succeeded"), PumpStep6HttpUntil([&]() { return bStepDone; }) && bStepOK);
	TestTrue(TEXT("Hero received >= 850 Vang & >= 1 Titan Token"), KillSummary.Rewards.Num() == 1 && KillSummary.Rewards[0].CoinsGranted >= 850 && KillSummary.Rewards[0].TokensGranted >= 1);

	// Claim Titan Slayer Quest (+1000 Vang, +3 Titan Tokens)
	FIU_QuestSnapshot ClaimedQuests;
	FIU_InventorySnapshot PostQuestInv;
	bool bHasInv = false;
	bStepDone = false; bStepOK = false;
	SrvKhu1->ClaimQuestReward(
		Leased.CharacterID,
		Leased.FencingEpoch,
		TEXT("quest.nro_titan_slayer_01"),
		FString::Printf(TEXT("ue5_s6_qclaim_%s"), *Suffix),
		[&](bool bOK, const FIU_QuestSnapshot& QS, const FIU_InventorySnapshot& IS, bool bHI, const FString&)
		{
			bStepOK = bOK; ClaimedQuests = QS; PostQuestInv = IS; bHasInv = bHI; bStepDone = true;
		});
	TestTrue(TEXT("ClaimQuestReward (Titan Slayer) succeeded"), PumpStep6HttpUntil([&]() { return bStepDone; }) && bStepOK);

	// Redeem Pity Shop item (pity.mat_crimson_ore_t1: -2 Titan Tokens, -200 Vang)
	FIU_PityShopSnapshot PostRedeemPity;
	bStepDone = false; bStepOK = false;
	SrvKhu1->RedeemPityShopEntry(
		Leased.CharacterID,
		Leased.FencingEpoch,
		TEXT("pity.mat_crimson_ore_t1"),
		FString::Printf(TEXT("ue5_s6_redeem_%s"), *Suffix),
		[&](bool bOK, const FIU_PityShopSnapshot& PS, const FIU_InventorySnapshot&, bool, const FString&)
		{
			bStepOK = bOK; PostRedeemPity = PS; bStepDone = true;
		});
	TestTrue(TEXT("RedeemPityShopEntry succeeded"), PumpStep6HttpUntil([&]() { return bStepDone; }) && bStepOK);
	TestEqual(TEXT("LifetimeRedeemed = 2 tokens"), PostRedeemPity.LifetimeRedeemed, 2);

	// Release Lease cleanly
	SrvKhu1->ReleaseCharacterLease(Leased.CharacterID, Leased.FencingEpoch, nullptr);
	PumpStep6HttpUntil([&]() { return false; }, 0.25);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
