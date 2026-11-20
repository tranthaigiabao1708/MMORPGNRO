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

namespace IU_S4TestHelpers
{
	struct FScopedStep4World
	{
		UWorld* World = nullptr;
		UWorld* PrevGWorld = nullptr;
		UGameInstance* TempGI = nullptr;

		FScopedStep4World()
		{
			PrevGWorld = GWorld;
			TempGI = NewObject<UGameInstance>(GEngine);
			TempGI->AddToRoot();
			TempGI->InitializeStandalone();

			World = TempGI->GetWorld();
			if (!World)
			{
				World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("IU_Step4_TestWorld")));
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

		~FScopedStep4World()
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
// TEST 1: Armor Mitigation Formula, CDO Zero-Regression & Replicated Economy Fields
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S4_DefenseMitigationAndZeroRegressionTest,
	"InfiniteUniverse.Step4.DefenseMitigationAndZeroRegression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S4_DefenseMitigationAndZeroRegressionTest::RunTest(const FString& Parameters)
{
	// 1. Default CDO Defense MUST be 0.0f so Step 1-3 unit test dummies take exact unmitigated damage
	const UIU_AttributeComponent* AttrCDO = GetDefault<UIU_AttributeComponent>();
	TestNotNull(TEXT("UIU_AttributeComponent CDO valid"), AttrCDO);
	TestTrue(TEXT("CDO Defense defaults to 0.0f for zero regression"), FMath::IsNearlyZero(AttrCDO->GetDefense()));

	// 2. Armor Mitigation math: FinalDmg = Max(1.0, RawDmg * (100 / (100 + Def)))
	TestTrue(
		TEXT("Defense=0 leaves 50.0 damage unmitigated (50.0)"),
		FMath::IsNearlyEqual(UIU_AttributeComponent::ComputeMitigatedDamage(50.0f, 0.0f), 50.0f, 0.01f));
	TestTrue(
		TEXT("Defense=25 reduces 50.0 damage to 40.0 (-20%)"),
		FMath::IsNearlyEqual(UIU_AttributeComponent::ComputeMitigatedDamage(50.0f, 25.0f), 40.0f, 0.01f));
	TestTrue(
		TEXT("Defense=100 reduces 80.0 damage to 40.0 (-50%)"),
		FMath::IsNearlyEqual(UIU_AttributeComponent::ComputeMitigatedDamage(80.0f, 100.0f), 40.0f, 0.01f));
	TestTrue(
		TEXT("Extreme Defense=99999 clamps minimum damage floor to 1.0"),
		FMath::IsNearlyEqual(UIU_AttributeComponent::ComputeMitigatedDamage(10.0f, 99999.0f), 1.0f, 0.01f));

	// 3. Verify CPF_Net replication metadata on Step 4 properties
	TestTrue(TEXT("UIU_AttributeComponent::Defense is CPF_Net replicated"),
		IU_S4TestHelpers::HasNetProp(UIU_AttributeComponent::StaticClass(), TEXT("Defense")));
	TestTrue(TEXT("AIU_PlayerState::StandardCoin is CPF_Net replicated"),
		IU_S4TestHelpers::HasNetProp(AIU_PlayerState::StaticClass(), TEXT("StandardCoin")));
	TestTrue(TEXT("AIU_PlayerState::ElixirCount is CPF_Net replicated"),
		IU_S4TestHelpers::HasNetProp(AIU_PlayerState::StaticClass(), TEXT("ElixirCount")));
	TestTrue(TEXT("AIU_PlayerState::CrimsonOreCount is CPF_Net replicated"),
		IU_S4TestHelpers::HasNetProp(AIU_PlayerState::StaticClass(), TEXT("CrimsonOreCount")));
	TestTrue(TEXT("AIU_PlayerState::EquippedItemCount is CPF_Net replicated"),
		IU_S4TestHelpers::HasNetProp(AIU_PlayerState::StaticClass(), TEXT("EquippedItemCount")));

	// 4. Verify live UIU_DamageComponent applies armor mitigation when target Defense > 0
	IU_S4TestHelpers::FScopedStep4World ScopeWorld;
	AIU_PlayerCharacter* Defender = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	AIU_EnemyCharacter* Attacker = ScopeWorld.World->SpawnActor<AIU_EnemyCharacter>(FVector(200.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	TestNotNull(TEXT("Defender spawned"), Defender);
	TestNotNull(TEXT("Attacker spawned"), Attacker);

	Defender->GetAttributeComponent()->ApplyPersistedVitals(280.0f, 280.0f, 200.0f, 200.0f, 1.0f, 100.0f, 100.0f);
	FIU_DamageSpec Spec;
	Spec.Amount = 40.0f;
	Spec.InstigatorActor = Attacker;
	Spec.HitLocation = Defender->GetActorLocation();
	const bool bApplied = Defender->GetDamageComponent()->ApplyDamageSpec(Spec);
	TestTrue(TEXT("ApplyDamageSpec succeeded against armored player"), bApplied);
	TestTrue(
		TEXT("40.0 raw damage vs 100 Defense reduced HP from 280 -> 260 (-20.0 mitigated)"),
		FMath::IsNearlyEqual(Defender->GetAttributeComponent()->GetHP(), 260.0f, 0.05f));

	return true;
}

// ============================================================================
// TEST 2: Vietnamese ASCII Sanitizer & Inventory/Loot JSON Contract Deserialization
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S4_InventoryAndLootJsonSerializationTest,
	"InfiniteUniverse.Step4.InventoryAndLootJsonSerialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S4_InventoryAndLootJsonSerializationTest::RunTest(const FString& Parameters)
{
	const FString CleanTunic = UIU_BackendSubsystem::SanitizeVietnameseForSlateASCII(TEXT("Áo Giáp Tân Binh"));
	const FString CleanElixir = UIU_BackendSubsystem::SanitizeVietnameseForSlateASCII(TEXT("Đậu Thần Hồi Phục Nhỏ"));
	TestEqual(TEXT("Sanitized Vietnamese armor title to Slate-safe ASCII"), CleanTunic, FString(TEXT("Ao Giap Tan Binh")));
	TestEqual(TEXT("Sanitized Vietnamese Dau Than title to Slate-safe ASCII"), CleanElixir, FString(TEXT("Dau Than Hoi Phuc Nho")));

	const FString SampleLootResp = TEXT(R"({
		"loot": {
			"character_id": "11111111-2222-3333-4444-555555555555",
			"source_entity_id": "mob.prototype_raider",
			"coins_granted": 58,
			"standard_coin": 258,
			"bag_full_skipped": false,
			"items_dropped": [
				{"instance_id":"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee","item_def_id":"item.hands_recruit_gloves_01","display_name":"Găng Tay Tân Binh","rarity":"UNCOMMON","quantity":1}
			]
		},
		"inventory": {
			"character_id": "11111111-2222-3333-4444-555555555555",
			"level": 3,
			"standard_coin": 258,
			"hp": 265,
			"max_hp": 280,
			"energy": 200,
			"max_energy": 200,
			"attack_power": 112,
			"attack_power_multiplier": 1.12,
			"defense": 32,
			"used_bag_slots": 2,
			"max_bag_slots": 48,
			"elixir_count": 3,
			"crimson_ore_count": 5,
			"action_result": "Cuong hoa +2 thanh cong!",
			"enhance_succeeded": true,
			"bag_items": [
				{
					"instance_id": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
					"item_def_id": "item.hands_recruit_gloves_01",
					"display_name": "Găng Tay Tân Binh",
					"category": "EQUIPMENT",
					"equip_slot": "slot.hands",
					"rarity": "UNCOMMON",
					"required_level": 1,
					"bag_slot_index": 0,
					"stack_count": 1,
					"max_stack": 1,
					"enhance_level": 0,
					"max_enhance_level": 8,
					"sell_price_coin": 20,
					"base_stats": {"attr.phys_atk": 8, "attr.def": 4},
					"affixes": [{"affix_id": "affix.flat_hp", "attr_id": "attr.max_hp", "tier": 1, "value": 30}]
				}
			],
			"equipped_items": [
				{
					"instance_id": "99999999-8888-7777-6666-555555555555",
					"item_def_id": "item.body_recruit_tunic_01",
					"display_name": "Áo Giáp Tân Binh",
					"category": "EQUIPMENT",
					"equip_slot": "slot.body",
					"equipped_slot_id": "slot.body",
					"rarity": "COMMON",
					"enhance_level": 2,
					"max_enhance_level": 5,
					"next_enhance_coin": 250,
					"base_stats": {"attr.max_hp": 88, "attr.def": 9}
				}
			]
		}
	})");

	FIU_MobLootDropSummary ParsedLoot;
	FIU_InventorySnapshot ParsedSnap;
	const bool bParsed = UIU_BackendSubsystem::ParseMobLootResponseFromJson(SampleLootResp, ParsedLoot, ParsedSnap);
	TestTrue(TEXT("ParseMobLootResponseFromJson succeeded"), bParsed);
	TestEqual(TEXT("Parsed CoinsGranted = 58"), ParsedLoot.CoinsGranted, int64(58));
	TestEqual(TEXT("Parsed 1 dropped item label"), ParsedLoot.DroppedItemLabels.Num(), 1);
	TestEqual(TEXT("Parsed StandardCoin = 258"), ParsedSnap.StandardCoin, int64(258));
	TestTrue(TEXT("Parsed Defense = 32.0"), FMath::IsNearlyEqual(ParsedSnap.Defense, 32.0f, 0.01f));
	TestEqual(TEXT("Parsed 1 bag item & 1 equipped item"), ParsedSnap.BagItems.Num() + ParsedSnap.EquippedItems.Num(), 2);
	TestTrue(TEXT("Bag item StatSummaryText includes base + affix"),
		ParsedSnap.BagItems[0].StatSummaryText.Contains(TEXT("+8 SucDanh")) &&
		ParsedSnap.BagItems[0].StatSummaryText.Contains(TEXT("[Af]+30 HP")));
	TestEqual(TEXT("Equipped tunic EnhanceLevel = 2"), ParsedSnap.EquippedItems[0].EnhanceLevel, 2);

	return true;
}

// ============================================================================
// TEST 3: UIU_InventoryEquipmentWidget Programmatic Tree & 3.0s Consumable Cooldown
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S4_ConsumableCooldownAndInventoryWidgetTest,
	"InfiniteUniverse.Step4.ConsumableCooldownAndInventoryWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S4_ConsumableCooldownAndInventoryWidgetTest::RunTest(const FString& Parameters)
{
	IU_S4TestHelpers::FScopedStep4World ScopeWorld;

	// 1. Construct programmatic UIU_InventoryEquipmentWidget & populate snapshot
	UIU_InventoryEquipmentWidget* InvWidget = CreateWidget<UIU_InventoryEquipmentWidget>(
		ScopeWorld.TempGI,
		UIU_InventoryEquipmentWidget::StaticClass());
	TestNotNull(TEXT("UIU_InventoryEquipmentWidget created"), InvWidget);
	TSharedRef<SWidget> BuiltSlate = InvWidget->TakeWidget();
	TestTrue(TEXT("UIU_InventoryEquipmentWidget built non-null Slate hierarchy"),
		BuiltSlate->GetType() != FName(TEXT("SNullWidget")));

	FIU_InventorySnapshot MockSnap;
	MockSnap.CharacterID = TEXT("test-char");
	MockSnap.StandardCoin = 500;
	MockSnap.ElixirCount = 3;
	MockSnap.CrimsonOreCount = 4;
	MockSnap.UsedBagSlots = 1;
	FIU_InventoryItemView MockItem;
	MockItem.InstanceID = TEXT("inst-01");
	MockItem.ItemDefID = TEXT("item.body_recruit_tunic_01");
	MockItem.DisplayName = TEXT("Ao Giap Tan Binh");
	MockItem.Category = TEXT("EQUIPMENT");
	MockItem.EquipSlot = TEXT("slot.body");
	MockItem.Rarity = TEXT("COMMON");
	MockItem.StatSummaryText = TEXT("+80 HP, +8 Giap");
	MockSnap.BagItems.Add(MockItem);
	InvWidget->ApplyInventorySnapshot(MockSnap);

	// 2. Verify Server-Authoritative 3.0s Consumable Cooldown on AIU_PlayerController
	AIU_PlayerController* PC = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	AIU_PlayerState* PS = ScopeWorld.World->SpawnActor<AIU_PlayerState>();
	AIU_PlayerCharacter* Pawn = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>();
	TestNotNull(TEXT("Controller spawned"), PC);
	TestNotNull(TEXT("PlayerState spawned"), PS);
	TestNotNull(TEXT("PlayerCharacter spawned"), Pawn);

	PC->PlayerState = PS;
	Pawn->SetPlayerState(PS);
	PC->Possess(Pawn);
	PS->ApplyAuthoritativeEconomySummary(100, 3, 2, 0);

	float RemCD = 0.0f;
	TestTrue(TEXT("Consumable initially off cooldown"), PC->CanUseConsumableOnServer(RemCD));

	// Trigger Server_RequestUseConsumable -> stamps LastConsumableUseServerTime at current WorldTime
	PC->Server_RequestUseConsumable(TEXT(""));
	TestFalse(TEXT("Immediate 2nd consumable request is blocked by 3.0s server cooldown"), PC->CanUseConsumableOnServer(RemCD));
	TestTrue(TEXT("Remaining cooldown is > 2.5s"), RemCD > 2.5f && RemCD <= 3.01f);

	return true;
}

// ============================================================================
// TEST 4: Equipment AttackPower Synergy With Step 3 Per-Hit 1.5x Dame Progression
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S4_EquipmentStatSynergyWithPerHitRewardTest,
	"InfiniteUniverse.Step4.EquipmentStatSynergyWithPerHitReward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S4_EquipmentStatSynergyWithPerHitRewardTest::RunTest(const FString& Parameters)
{
	IU_S4TestHelpers::FScopedStep4World ScopeWorld;
	AIU_PlayerCharacter* Hero = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	AIU_PlayerState* PS = ScopeWorld.World->SpawnActor<AIU_PlayerState>();
	TestNotNull(TEXT("Hero spawned"), Hero);
	TestNotNull(TEXT("PlayerState spawned"), PS);
	Hero->SetPlayerState(PS);

	// Baseline: 100 Sức đánh (x1.00) -> 20 base melee deals 20 dmg -> 1.5 * 20 = 30 SM & 30 TN
	Hero->GetAttributeComponent()->ApplyPersistedVitals(200.0f, 200.0f, 200.0f, 200.0f, 1.0f, 100.0f, 20.0f);
	const float BaseHitDmg = 20.0f * Hero->GetAttributeComponent()->GetAttackPowerMultiplier();
	PS->AwardOfflineHitProgress(BaseHitDmg);
	TestEqual(TEXT("Baseline 100 Atk -> 20 dmg -> +30 Suc Manh & +30 Tiem Nang"), PS->GetPotentialPoints(), int64(30));

	// Equip Weapon (+30 Sức đánh -> Total 130 Sức đánh, x1.30) -> 20 * 1.30 = 26 dmg -> 1.5 * 26 = 39 SM & 39 TN!
	Hero->GetAttributeComponent()->ApplyPersistedVitals(280.0f, 280.0f, 200.0f, 200.0f, 1.30f, 130.0f, 28.0f);
	const float EquippedHitDmg = 20.0f * Hero->GetAttributeComponent()->GetAttackPowerMultiplier();
	TestTrue(TEXT("Equipped weapon scaled outgoing hit from 20 -> 26"), FMath::IsNearlyEqual(EquippedHitDmg, 26.0f, 0.01f));

	PS->AwardOfflineHitProgress(EquippedHitDmg);
	TestEqual(TEXT("Equipped 130 Atk -> 26 dmg -> +39 SM & +39 TN (30 + 39 = 69 total)"), PS->GetPotentialPoints(), int64(69));

	return true;
}

// ============================================================================
// TEST 5: Live UE5 <-> Go Backend Step 4 Full Inventory, Loot, Enhance & Ledger E2E
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S4_BackendLoopbackInventoryLootEnhanceRoundTripTest,
	"InfiniteUniverse.Step4.BackendLoopbackInventoryLootEnhanceRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S4_BackendLoopbackInventoryLootEnhanceRoundTripTest::RunTest(const FString& Parameters)
{
	IU_S4TestHelpers::FScopedStep4World ScopeWorld;
	UIU_BackendSubsystem* Backend = ScopeWorld.TempGI->GetSubsystem<UIU_BackendSubsystem>();
	TestNotNull(TEXT("UIU_BackendSubsystem valid"), Backend);

	bool bProbeDone = false;
	bool bGatewayOnline = false;
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

		const double Deadline = FPlatformTime::Seconds() + 2.0;
		while (!bProbeDone && FPlatformTime::Seconds() < Deadline)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.01f);
			FPlatformProcess::Sleep(0.01f);
		}
	}

	if (!bGatewayOnline)
	{
		UE_LOG(LogTemp, Display, TEXT("[Step4Test] Go Gateway :8080 offline in standalone run — skipping live loopback check."));
		return true;
	}

	Backend->SetBackendBridgeEnabled(true);
	const FString UniqueSuffix = FString::Printf(TEXT("%lld"), FDateTime::UtcNow().GetTicks() % 100000000ll);
	const FString TestUser = FString::Printf(TEXT("S4Auto_%s"), *UniqueSuffix);
	const FString TestChar = FString::Printf(TEXT("S4Hero_%s"), *UniqueSuffix.Right(7));

	auto PumpHttpUntil = [](const bool& bFlag, double TimeoutSec)
	{
		const double EndTime = FPlatformTime::Seconds() + TimeoutSec;
		while (!bFlag && FPlatformTime::Seconds() < EndTime)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.01f);
			FPlatformProcess::Sleep(0.01f);
		}
	};

	bool bDone = false, bOk = false;
	Backend->RegisterAccount(TestUser, TestUser + TEXT("@iu.test"), TEXT("UE5Step4Secret!99"),
		[&](bool bS, const FString&) { bOk = bS; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("RegisterAccount succeeded"), bOk);

	bDone = false; bOk = false;
	FIU_BackendCharacterSummary Created;
	Backend->CreateCharacter(TestChar, TEXT("race.zenith_warrior"),
		[&](bool bS, const FIU_BackendCharacterSummary& C, const FString&) { bOk = bS; Created = C; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("CreateCharacter succeeded with Base Defense=20"), bOk && FMath::IsNearlyEqual(Created.Defense, 20.0f, 0.1f));

	bDone = false; bOk = false;
	FString Ticket;
	Backend->RequestEnterWorldAndTravel(Created.CharacterID, nullptr,
		[&](bool bS, const FString& T, const FString&) { bOk = bS; Ticket = T; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("RequestEnterWorld issued ticket"), bOk && !Ticket.IsEmpty());

	bDone = false; bOk = false;
	FIU_BackendCharacterSummary Claimed;
	Backend->ClaimCharacterLease(Ticket,
		[&](bool bS, const FIU_BackendCharacterSummary& S, const FString&) { bOk = bS; Claimed = S; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("ClaimCharacterLease epoch=1"), bOk && Claimed.FencingEpoch == 1);

	// 1. Fetch Starter Kit Inventory
	bDone = false; bOk = false;
	FIU_InventorySnapshot InitSnap;
	Backend->FetchCharacterInventory(Claimed.CharacterID, Claimed.FencingEpoch,
		[&](bool bS, const FIU_InventorySnapshot& S, const FString&) { bOk = bS; InitSnap = S; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("Starter Kit contains 3 bag stacks (Tunic, 3x Elixir, 4x Crimson Ore)"),
		bOk && InitSnap.BagItems.Num() == 3 && InitSnap.ElixirCount == 3 && InitSnap.CrimsonOreCount == 4);

	FString TunicInstID, OreInstID;
	int64 OreSellPrice = 0;
	for (const FIU_InventoryItemView& It : InitSnap.BagItems)
	{
		if (It.ItemDefID == TEXT("item.body_recruit_tunic_01")) { TunicInstID = It.InstanceID; }
		if (It.ItemDefID == TEXT("item.mat_crimson_ore_t1")) { OreInstID = It.InstanceID; OreSellPrice = It.SellPriceCoin; }
	}
	TestTrue(TEXT("Located Starter Tunic & Crimson Ore instances"), !TunicInstID.IsEmpty() && !OreInstID.IsEmpty() && OreSellPrice > 0);

	// 2. Equip Starter Tunic -> MaxHP 200->280, Defense 20->28
	bDone = false; bOk = false;
	FIU_InventorySnapshot EquipSnap;
	Backend->EquipInventoryItem(Claimed.CharacterID, Claimed.FencingEpoch, TunicInstID,
		[&](bool bS, const FIU_InventorySnapshot& S, const FString&) { bOk = bS; EquipSnap = S; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("EquipInventoryItem compiled MaxHP=280 & Defense=28"),
		bOk && EquipSnap.EquippedItems.Num() == 1 &&
		FMath::IsNearlyEqual(EquipSnap.MaxHP, 280.0f, 0.1f) &&
		FMath::IsNearlyEqual(EquipSnap.Defense, 28.0f, 0.1f));

	// 3. Roll CSPRNG Monster Loot x4 -> accumulate >= 340 Vàng ([85..135] per Prototype Raider kill)
	FIU_InventorySnapshot LootSnap;
	for (int32 k = 0; k < 4; ++k)
	{
		bDone = false; bOk = false;
		FIU_MobLootDropSummary DropRes;
		Backend->RollMonsterLoot(
			Claimed.CharacterID,
			Claimed.FencingEpoch,
			TEXT("mob.prototype_raider"),
			FString::Printf(TEXT("s4_ue5_loot_%s_%d"), *UniqueSuffix, k),
			[&](bool bS, const FIU_MobLootDropSummary& L, const FIU_InventorySnapshot& S, const FString&)
			{
				bOk = bS; DropRes = L; LootSnap = S; bDone = true;
			});
		PumpHttpUntil(bDone, 5.0);
		TestTrue(TEXT("RollMonsterLoot granted CSPRNG Vang [85..135]"), bOk && DropRes.CoinsGranted >= 85 && DropRes.CoinsGranted <= 135);
	}
	TestTrue(TEXT("Wallet accumulated >= 340 Vang after 4 Raider kills"), LootSnap.StandardCoin >= 340);

	// 4. Enhance Equipped Tunic (+0 -> +1, costs 100 Vàng + 1 Ore)
	bDone = false; bOk = false;
	FIU_InventorySnapshot EnhSnap;
	Backend->EnhanceInventoryEquipment(
		Claimed.CharacterID,
		Claimed.FencingEpoch,
		TunicInstID,
		TEXT("s4_ue5_enh_") + UniqueSuffix,
		[&](bool bS, const FIU_InventorySnapshot& S, const FString&) { bOk = bS; EnhSnap = S; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("EnhanceInventoryEquipment executed ACID deduction (-100 Vang, -1 Ore)"),
		bOk && EnhSnap.bHasEnhanceOutcome && EnhSnap.StandardCoin == LootSnap.StandardCoin - 100);

	// 5. Use Dau Than Consumable -> ElixirCount 3 -> 2 (or N -> N-1 if extra dropped) & HP restored toward MaxHP
	const int32 PrevElixirs = EnhSnap.ElixirCount;
	bDone = false; bOk = false;
	FIU_InventorySnapshot UseSnap;
	Backend->UseInventoryConsumable(Claimed.CharacterID, Claimed.FencingEpoch, TEXT(""),
		[&](bool bS, const FIU_InventorySnapshot& S, const FString&) { bOk = bS; UseSnap = S; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("UseInventoryConsumable decremented ElixirCount by 1 & healed HP to MaxHP"),
		bOk && UseSnap.ElixirCount == PrevElixirs - 1 && FMath::IsNearlyEqual(UseSnap.HP, UseSnap.MaxHP, 0.1f));

	// 6. Item Lock Protection: locking Ore blocks SellItem, unlocking allows SellItem (+OreSellPrice Vàng)
	bDone = false; bOk = false;
	FIU_InventorySnapshot LockSnap;
	Backend->ToggleInventoryItemLock(Claimed.CharacterID, Claimed.FencingEpoch, OreInstID, true,
		[&](bool bS, const FIU_InventorySnapshot& S, const FString&) { bOk = bS; LockSnap = S; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("ToggleInventoryItemLock(true) succeeded"), bOk);

	bDone = false; bOk = false;
	Backend->SellInventoryItem(Claimed.CharacterID, Claimed.FencingEpoch, OreInstID, 1, TEXT("s4_sell_blocked_") + UniqueSuffix,
		[&](bool bS, const FIU_InventorySnapshot&, const FString&) { bOk = bS; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestFalse(TEXT("Selling a locked item is authoritatively REJECTED"), bOk);

	bDone = false; bOk = false;
	Backend->ToggleInventoryItemLock(Claimed.CharacterID, Claimed.FencingEpoch, OreInstID, false,
		[&](bool bS, const FIU_InventorySnapshot&, const FString&) { bOk = bS; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("ToggleInventoryItemLock(false) succeeded"), bOk);

	const int64 CoinBeforeSell = UseSnap.StandardCoin;
	bDone = false; bOk = false;
	FIU_InventorySnapshot SellSnap;
	Backend->SellInventoryItem(Claimed.CharacterID, Claimed.FencingEpoch, OreInstID, 1, TEXT("s4_sell_ok_") + UniqueSuffix,
		[&](bool bS, const FIU_InventorySnapshot& S, const FString&) { bOk = bS; SellSnap = S; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("SellInventoryItem credited +OreSellPrice Vang via currency_ledger"), bOk && SellSnap.StandardCoin == CoinBeforeSell + OreSellPrice);

	Backend->ReleaseCharacterLease(Claimed.CharacterID, Claimed.FencingEpoch, nullptr);
	FHttpModule::Get().GetHttpManager().Flush(EHttpFlushReason::Default);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
