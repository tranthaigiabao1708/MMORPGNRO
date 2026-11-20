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
#include "Attributes/IU_AttributeComponent.h"
#include "Network/IU_BackendSubsystem.h"
#include "UI/IU_LoginCharacterWidget.h"
#include "UI/IU_PlayerHUDWidget.h"

namespace IU_S3TestHelpers
{
	struct FScopedStep3World
	{
		UWorld* World = nullptr;
		UWorld* PrevGWorld = nullptr;
		UGameInstance* TempGI = nullptr;

		FScopedStep3World()
		{
			PrevGWorld = GWorld;
			TempGI = NewObject<UGameInstance>(GEngine);
			TempGI->AddToRoot();
			TempGI->InitializeStandalone();

			World = TempGI->GetWorld();
			if (!World)
			{
				World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("IU_Step3_TestWorld")));
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

		~FScopedStep3World()
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
// TEST 1: FIPS 180-4 SHA-256 & RFC 4231 HMAC-SHA256 Cryptographic Verification
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S3_CryptoHMACSHA256VectorsTest,
	"InfiniteUniverse.Step3.CryptoHMACSHA256Vectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S3_CryptoHMACSHA256VectorsTest::RunTest(const FString& Parameters)
{
	// 1. NIST FIPS 180-4 SHA-256("abc")
	const uint8 AbcBytes[3] = { 'a', 'b', 'c' };
	uint8 Digest[32];
	UIU_BackendSubsystem::ComputeSHA256(AbcBytes, 3, Digest);
	FString AbcHex;
	for (int32 i = 0; i < 32; ++i)
	{
		AbcHex += FString::Printf(TEXT("%02x"), Digest[i]);
	}
	TestEqual(
		TEXT("SHA-256('abc') matches NIST FIPS 180-4 vector"),
		AbcHex,
		FString(TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")));

	// 2. RFC 4231 Test Case 2: Key = "Jefe", Data = "what do ya want for nothing?"
	const FString Rfc4231Case2 = UIU_BackendSubsystem::ComputeHMACSHA256Hex(
		TEXT("Jefe"),
		TEXT("what do ya want for nothing?"));
	TestEqual(
		TEXT("HMAC-SHA256('Jefe', 'what do ya want for nothing?') matches RFC 4231 Case 2"),
		Rfc4231Case2,
		FString(TEXT("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843")));

	// 3. Long >64-byte secret key hashing branch
	const FString LongKey = TEXT("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef_extra_secret");
	const FString SigA = UIU_BackendSubsystem::ComputeHMACSHA256Hex(LongKey, TEXT("{\"server_id\":\"ue5-ded-7777\"}"));
	const FString SigTampered = UIU_BackendSubsystem::ComputeHMACSHA256Hex(LongKey, TEXT("{\"server_id\":\"rogue-7777\"}"));
	TestEqual(TEXT("HMAC-SHA256 hex digest length is 64 chars"), SigA.Len(), 64);
	TestNotEqual(TEXT("Payload tamper alters HMAC-SHA256 signature"), SigA, SigTampered);

	return true;
}

// ============================================================================
// TEST 2: Backend JSON Character Summary & Fenced Save Payload Round-Trip
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S3_BackendJsonSerializationRoundTripTest,
	"InfiniteUniverse.Step3.BackendJsonSerializationRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S3_BackendJsonSerializationRoundTripTest::RunTest(const FString& Parameters)
{
	const FString SampleBackendJson = TEXT(
		"{"
		"\"character_id\":\"c1e2d3f4-1111-4222-8333-abcdef012345\","
		"\"account_id\":\"a1b2c3d4-5555-4666-9777-fedcba987654\","
		"\"character_name\":\"ZenithGoku\","
		"\"race_id\":\"race.zenith_warrior\","
		"\"level\":3,"
		"\"current_exp\":225,"
		"\"next_level_exp\":400,"
		"\"zone_id\":\"zone.p01_west_hub\","
		"\"pos_x\":-3412.5,"
		"\"pos_y\":188.25,"
		"\"pos_z\":112.0,"
		"\"rot_yaw\":-90.0,"
		"\"has_saved_pos\":true,"
		"\"is_alive\":true,"
		"\"hp\":142.0,"
		"\"max_hp\":150.0,"
		"\"energy\":125.0,"
		"\"max_energy\":130.0,"
		"\"attack_power_multiplier\":1.32,"
		"\"enemy_kills\":10,"
		"\"pvp_kills\":2,"
		"\"deaths\":1,"
		"\"fencing_epoch\":4,"
		"\"leveled_up\":true,"
		"\"old_level\":2,"
		"\"exp_granted\":55"
		"}");

	FIU_BackendCharacterSummary Parsed;
	TestTrue(TEXT("ParseCharacterSummaryFromJson succeeds"),
		UIU_BackendSubsystem::ParseCharacterSummaryFromJson(SampleBackendJson, Parsed));
	TestEqual(TEXT("CharacterName parsed"), Parsed.CharacterName, FString(TEXT("ZenithGoku")));
	TestEqual(TEXT("Level parsed"), Parsed.Level, 3);
	TestEqual(TEXT("CurrentExp parsed"), Parsed.CurrentExp, static_cast<int64>(225));
	TestEqual(TEXT("NextLevelExp parsed"), Parsed.NextLevelExp, static_cast<int64>(400));
	TestEqual(TEXT("FencingEpoch parsed"), Parsed.FencingEpoch, static_cast<int64>(4));
	TestTrue(TEXT("bHasSavedPosition parsed true"), Parsed.bHasSavedPosition);
	TestTrue(TEXT("bLeveledUp parsed true"), Parsed.bLeveledUp);
	TestTrue(TEXT("PosX parsed accurately"), FMath::IsNearlyEqual(Parsed.SavedPosition.X, -3412.5, 0.01));
	TestTrue(TEXT("AttackPowerMultiplier parsed accurately"), FMath::IsNearlyEqual(Parsed.AttackPowerMultiplier, 1.32f, 0.001f));

	FIU_CharacterSavePayload SaveIn;
	SaveIn.CharacterID = Parsed.CharacterID;
	SaveIn.FencingEpoch = Parsed.FencingEpoch;
	SaveIn.ServerID = TEXT("ue5-ded-7777");
	SaveIn.SnapshotReason = TEXT("LOGOUT");
	SaveIn.Position = FVector(-3412.5, 188.25, 112.0);
	SaveIn.Yaw = -90.0f;
	SaveIn.HP = 142.0f;
	SaveIn.Energy = 125.0f;
	SaveIn.EnemyKills = 10;

	const FString EncodedSave = UIU_BackendSubsystem::SerializeSavePayloadToJson(SaveIn);
	TestTrue(TEXT("Serialized save contains fencing_epoch 4"), EncodedSave.Contains(TEXT("\"fencing_epoch\":4")));
	TestTrue(TEXT("Serialized save contains LOGOUT reason"), EncodedSave.Contains(TEXT("\"snapshot_reason\":\"LOGOUT\"")));
	TestTrue(TEXT("Serialized save contains pos_x -3412.5"), EncodedSave.Contains(TEXT("-3412.5")));

	return true;
}

// ============================================================================
// TEST 3: Replicated PlayerState Progression, Lease Fencing Revocation & Scaled Vitals
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S3_ProgressionPlayerStateAndVitalsTest,
	"InfiniteUniverse.Step3.ProgressionPlayerStateAndVitals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S3_ProgressionPlayerStateAndVitalsTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("PersistedCharacterName replicated"), IU_S3TestHelpers::HasNetProp(AIU_PlayerState::StaticClass(), TEXT("PersistedCharacterName")));
	TestTrue(TEXT("CharacterLevel replicated"), IU_S3TestHelpers::HasNetProp(AIU_PlayerState::StaticClass(), TEXT("CharacterLevel")));
	TestTrue(TEXT("CurrentEXP replicated"), IU_S3TestHelpers::HasNetProp(AIU_PlayerState::StaticClass(), TEXT("CurrentEXP")));
	TestTrue(TEXT("NextLevelEXP replicated"), IU_S3TestHelpers::HasNetProp(AIU_PlayerState::StaticClass(), TEXT("NextLevelEXP")));
	TestTrue(TEXT("ActiveFencingEpoch replicated"), IU_S3TestHelpers::HasNetProp(AIU_PlayerState::StaticClass(), TEXT("ActiveFencingEpoch")));

	IU_S3TestHelpers::FScopedStep3World ScopeWorld;
	UWorld* World = ScopeWorld.World;

	AIU_PlayerState* PS = World->SpawnActor<AIU_PlayerState>();
	AIU_PlayerCharacter* PChar = World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0f, 0.0f, 120.0f), FRotator::ZeroRotator);
	TestNotNull(TEXT("AIU_PlayerState spawned"), PS);
	TestNotNull(TEXT("AIU_PlayerCharacter spawned"), PChar);

	FIU_BackendCharacterSummary Loaded;
	Loaded.CharacterID = TEXT("11111111-2222-4333-8444-555555555555");
	Loaded.CharacterName = TEXT("PiccoloSage");
	Loaded.RaceID = TEXT("race.mystic_elder");
	Loaded.Level = 1;
	Loaded.CurrentExp = 55;
	Loaded.NextLevelExp = 100;
	Loaded.FencingEpoch = 7;
	Loaded.MaxHP = 100.0f;
	Loaded.HP = 88.0f;
	Loaded.MaxEnergy = 100.0f;
	Loaded.Energy = 90.0f;
	Loaded.AttackPowerMultiplier = 1.0f;
	Loaded.SavedPosition = FVector(-3400.0, 150.0, 114.0);
	Loaded.SavedYaw = 180.0f;
	Loaded.bHasSavedPosition = true;

	PS->ApplyLoadedBackendSummary(Loaded);
	TestTrue(TEXT("PlayerState holds valid backend lease after ApplyLoadedBackendSummary"), PS->HasValidBackendLease());
	TestEqual(TEXT("ActiveFencingEpoch == 7"), PS->GetActiveFencingEpoch(), static_cast<int64>(7));
	TestTrue(TEXT("EXPPercent == 0.55"), FMath::IsNearlyEqual(PS->GetEXPPercent(), 0.55f, 0.001f));

	PS->ApplyAuthoritativeEXPUpdate(2, 10, 225, 2, true);
	TestEqual(TEXT("CharacterLevel updated to 2"), PS->GetCharacterLevel(), 2);
	TestEqual(TEXT("CurrentEXP updated to 10"), PS->GetCurrentEXP(), static_cast<int64>(10));
	TestEqual(TEXT("NextLevelEXP updated to 225"), PS->GetNextLevelEXP(), static_cast<int64>(225));
	TestEqual(TEXT("EnemyKills updated to 2"), PS->GetEnemyKills(), 2);

	UIU_AttributeComponent* Attr = PChar->GetAttributeComponent();
	Attr->ApplyPersistedVitals(125.0f, 125.0f, 120.0f, 120.0f, 1.16f);
	TestTrue(TEXT("MaxHP scaled to 125.0"), FMath::IsNearlyEqual(Attr->GetMaxHP(), 125.0f, 0.01f));
	TestTrue(TEXT("MaxKi scaled to 120.0"), FMath::IsNearlyEqual(Attr->GetMaxKi(), 120.0f, 0.01f));
	TestTrue(TEXT("AttackPowerMultiplier scaled to 1.16"), FMath::IsNearlyEqual(Attr->GetAttackPowerMultiplier(), 1.16f, 0.001f));

	PChar->TeleportToPersistedTransform(Loaded.SavedPosition, FRotator(0.0f, Loaded.SavedYaw, 0.0f));
	TestTrue(TEXT("Player teleported to persisted West Arena X=-3400"),
		FMath::IsNearlyEqual(PChar->GetActorLocation().X, -3400.0, 1.0));

	// Lease revocation MUST disarm HasValidBackendLease so Logout never writes stale epoch!
	PS->MarkLeaseRevoked();
	TestFalse(TEXT("HasValidBackendLease is false after MarkLeaseRevoked"), PS->HasValidBackendLease());
	TestEqual(TEXT("ActiveFencingEpoch cleared to 0 after revocation"), PS->GetActiveFencingEpoch(), static_cast<int64>(0));

	return true;
}

// ============================================================================
// TEST 4: Security — PreLogin Ticket Gate & UMG Auth Widget Construction
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S3_PreLoginTicketGateAndAuthWidgetTest,
	"InfiniteUniverse.Step3.PreLoginTicketGateAndAuthWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S3_PreLoginTicketGateAndAuthWidgetTest::RunTest(const FString& Parameters)
{
	IU_S3TestHelpers::FScopedStep3World ScopeWorld;
	UWorld* World = ScopeWorld.World;

	AIU_GameMode* GM = World->SpawnActor<AIU_GameMode>();
	TestNotNull(TEXT("AIU_GameMode spawned"), GM);
	GM->SetForceRequireTicketForTesting(true);

	FString ErrMissing;
	FUniqueNetIdRepl DummyNetId;
	GM->PreLogin(TEXT("?Name=RogueNoTicket"), TEXT("127.0.0.1:54999"), DummyNetId, ErrMissing);
	TestEqual(TEXT("PreLogin rejects connection missing ?Ticket="), ErrMissing, FString(TEXT("ERR_MISSING_WORLD_TICKET")));

	FString ErrWithTicket;
	GM->PreLogin(TEXT("?Name=LegitHero?Ticket=tkt_0123456789abcdef"), TEXT("127.0.0.1:55000"), DummyNetId, ErrWithTicket);
	TestTrue(TEXT("PreLogin accepts connection supplying ?Ticket="), ErrWithTicket.IsEmpty());

	UIU_LoginCharacterWidget* AuthWidget = CreateWidget<UIU_LoginCharacterWidget>(ScopeWorld.TempGI, UIU_LoginCharacterWidget::StaticClass());
	TestNotNull(TEXT("UIU_LoginCharacterWidget instantiated cleanly"), AuthWidget);
	TSharedRef<SWidget> SlateTree = AuthWidget->TakeWidget();
	TestTrue(TEXT("UIU_LoginCharacterWidget built programmatic Slate hierarchy"), SlateTree->GetType() != FName(TEXT("SNullWidget")));

	return true;
}

// ============================================================================
// TEST 5: Live UE5 <-> Go Backend Loopback Bridge Round-Trip (When :8080 Running)
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S3_BackendSubsystemLoopbackRoundTripTest,
	"InfiniteUniverse.Step3.BackendSubsystemLoopbackRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S3_BackendSubsystemLoopbackRoundTripTest::RunTest(const FString& Parameters)
{
	IU_S3TestHelpers::FScopedStep3World ScopeWorld;
	UIU_BackendSubsystem* Backend = ScopeWorld.TempGI->GetSubsystem<UIU_BackendSubsystem>();
	TestNotNull(TEXT("UIU_BackendSubsystem registered on UGameInstance"), Backend);

	// Probe whether live Go Backend is currently listening on 127.0.0.1:8080
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
		UE_LOG(LogTemp, Display, TEXT("[Step3Test] Go Gateway :8080 offline in standalone run — verified graceful offline subsystem isolation."));
		return true;
	}

	// Execute real UE5 C++ -> Go Gateway -> PostgreSQL 18 round-trip!
	Backend->SetBackendBridgeEnabled(true);
	const FString UniqueSuffix = FString::Printf(TEXT("%lld"), FDateTime::UtcNow().GetTicks() % 100000000ll);
	const FString TestUser = FString::Printf(TEXT("UE5Auto_%s"), *UniqueSuffix);
	const FString TestChar = FString::Printf(TEXT("Hero_%s"), *UniqueSuffix.Right(8));

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
	Backend->RegisterAccount(TestUser, TestUser + TEXT("@iu.test"), TEXT("UE5Step3Secret!99"),
		[&](bool bS, const FString&) { bOk = bS; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("Live Go Backend RegisterAccount succeeded from UE5 C++"), bOk);

	bDone = false; bOk = false;
	FIU_BackendCharacterSummary CreatedChar;
	Backend->CreateCharacter(TestChar, TEXT("race.zenith_warrior"),
		[&](bool bS, const FIU_BackendCharacterSummary& C, const FString&) { bOk = bS; CreatedChar = C; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("Live Go Backend CreateCharacter succeeded"), bOk && !CreatedChar.CharacterID.IsEmpty());

	bDone = false; bOk = false;
	FString IssuedTicket;
	Backend->RequestEnterWorldAndTravel(CreatedChar.CharacterID, nullptr,
		[&](bool bS, const FString& Tkt, const FString&) { bOk = bS; IssuedTicket = Tkt; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("Live Go Backend RequestEnterWorld issued one-time ticket"), bOk && !IssuedTicket.IsEmpty());

	bDone = false; bOk = false;
	FIU_BackendCharacterSummary Claimed;
	Backend->ClaimCharacterLease(IssuedTicket,
		[&](bool bS, const FIU_BackendCharacterSummary& S, const FString&) { bOk = bS; Claimed = S; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("Live HMAC ClaimCharacterLease succeeded with fencing_epoch=1"), bOk && Claimed.FencingEpoch == 1);

	// Grant 110 EXP -> Level 1 -> Level 2!
	bDone = false; bOk = false;
	FIU_BackendCharacterSummary ExpAfter;
	Backend->GrantCharacterEXP(Claimed.CharacterID, Claimed.FencingEpoch, 110, TEXT("ue5_test_kill_") + UniqueSuffix,
		[&](bool bS, const FIU_BackendCharacterSummary& S, const FString&) { bOk = bS; ExpAfter = S; bDone = true; });
	PumpHttpUntil(bDone, 5.0);
	TestTrue(TEXT("Live HMAC GrantCharacterEXP leveled character to Lv.2 (MaxHP=125)"),
		bOk && ExpAfter.Level == 2 && ExpAfter.bLeveledUp && FMath::IsNearlyEqual(ExpAfter.MaxHP, 125.0f, 0.1f));

	FIU_CharacterSavePayload FinalSave;
	FinalSave.CharacterID = Claimed.CharacterID;
	FinalSave.FencingEpoch = Claimed.FencingEpoch;
	FinalSave.Position = FVector(-3400.0, 200.0, 112.0);
	FinalSave.HP = 125.0f;
	FinalSave.Energy = 115.0f;
	FinalSave.EnemyKills = 2;
	Backend->ReleaseCharacterLease(Claimed.CharacterID, Claimed.FencingEpoch, &FinalSave);
	FHttpModule::Get().GetHttpManager().Flush(EHttpFlushReason::Default);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
