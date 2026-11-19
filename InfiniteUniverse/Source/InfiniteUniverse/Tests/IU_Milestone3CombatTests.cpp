// Copyright InfiniteUniverse Dev. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Core/IU_PlayerController.h"
#include "Movement/IU_MovementComponent.h"
#include "Movement/IU_FlightComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Combat/IU_TargetingComponent.h"
#include "Combat/IU_CombatComponent.h"
#include "Combat/IU_KiProjectile.h"
#include "Combat/IU_AfterimageGhost.h"
#include "Combat/IU_PracticeDummy.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputCoreTypes.h"

namespace IU_M3TestHelpers
{
	struct FScopedCombatTestWorld
	{
		UWorld* World = nullptr;
		UWorld* PrevGWorld = nullptr;

		FScopedCombatTestWorld()
		{
			PrevGWorld = GWorld;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("IU_M3_CombatTestWorld")));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			GWorld = World;
			FURL URL;
			World->InitializeActorsForPlay(URL);
			World->SetBegunPlay(true);
		}

		void TickSimulation(float TotalSeconds, AIU_PlayerCharacter* PlayerChar = nullptr, AIU_KiProjectile* Proj = nullptr, float StepDelta = 1.0f / 60.0f)
		{
			const int32 Steps = FMath::Max(1, FMath::RoundToInt(TotalSeconds / StepDelta));
			for (int32 i = 0; i < Steps; ++i)
			{
				++GFrameCounter;
				World->GetTimerManager().Tick(StepDelta);
				World->Tick(LEVELTICK_All, StepDelta);

				if (PlayerChar)
				{
					if (UIU_CombatComponent* CC = PlayerChar->GetCombatComponent())
					{
						if (CC->IsComponentTickEnabled())
						{
							CC->TickComponent(StepDelta, LEVELTICK_All, nullptr);
						}
					}
					if (UIU_TargetingComponent* TC = PlayerChar->GetTargetingComponent())
					{
						if (TC->IsComponentTickEnabled())
						{
							TC->TickComponent(StepDelta, LEVELTICK_All, nullptr);
						}
					}
					if (UIU_MovementComponent* MC = PlayerChar->GetIUMovementComponent())
					{
						MC->bRunPhysicsWithNoController = true;
						MC->TickComponent(StepDelta, LEVELTICK_All, nullptr);
					}
				}
				if (IsValid(Proj))
				{
					if (UProjectileMovementComponent* PMC = Proj->GetProjectileMovement())
					{
						PMC->TickComponent(StepDelta, LEVELTICK_All, nullptr);
					}
				}
			}
		}

		~FScopedCombatTestWorld()
		{
			GWorld = PrevGWorld;
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M3_LockOnAcquireSwitchAndAutoReleaseTest,
	"InfiniteUniverse.Milestone3.LockOnAcquireSwitchAndAutoRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M3_LockOnAcquireSwitchAndAutoReleaseTest::RunTest(const FString& Parameters)
{
	IU_M3TestHelpers::FScopedCombatTestWorld ScopeWorld;

	AIU_PlayerCharacter* Player = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	AIU_PlayerController* PC = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	PC->Possess(Player);

	AIU_PracticeDummy* DummyFront = ScopeWorld.World->SpawnActor<AIU_PracticeDummy>(FVector(500.0, -60.0, 0.0), FRotator::ZeroRotator);
	AIU_PracticeDummy* DummyRight = ScopeWorld.World->SpawnActor<AIU_PracticeDummy>(FVector(500.0, 260.0, 0.0), FRotator::ZeroRotator);
	AIU_PracticeDummy* DummyFarOut = ScopeWorld.World->SpawnActor<AIU_PracticeDummy>(FVector(9000.0, 0.0, 0.0), FRotator::ZeroRotator);
	DummyFront->bAutoResetAfterDefeat = false;
	DummyRight->bAutoResetAfterDefeat = false;

	UIU_TargetingComponent* TargetComp = Player->GetTargetingComponent();
	TestNotNull(TEXT("TargetingComponent attached"), TargetComp);

	// 1. Toggle Lock-On acquires nearest aligned in-range dummy (ignores DummyFarOut)
	TestTrue(TEXT("ToggleLockOn acquires valid target"), TargetComp->ToggleLockOn());
	TestEqual(TEXT("Locked onto DummyFront"), TargetComp->GetLockedTarget(), static_cast<AActor*>(DummyFront));
	TestFalse(TEXT("Out-of-range DummyFarOut rejected by SetLockedTarget"), TargetComp->SetLockedTarget(DummyFarOut));

	// 2. SwitchTarget(+1) switches cleanly to DummyRight
	TestTrue(TEXT("SwitchLockedTarget(+1) switches to right-side target"), TargetComp->SwitchLockedTarget(1.0f));
	TestEqual(TEXT("Now locked onto DummyRight"), TargetComp->GetLockedTarget(), static_cast<AActor*>(DummyRight));

	// 3. Defeating locked target MUST automatically release Lock-On!
	FIU_DamageSpec LethalSpec;
	LethalSpec.Amount = 500.0f;
	LethalSpec.InstigatorActor = Player;
	DummyRight->GetDamageComponent()->ApplyDamageSpec(LethalSpec);

	TestTrue(TEXT("DummyRight is now dead"), DummyRight->GetAttributeComponent()->IsDead());
	TestFalse(TEXT("Lock-On auto-released immediately when target died"), TargetComp->IsLockedOn());
	TestFalse(TEXT("Cannot re-lock onto dead target"), TargetComp->SetLockedTarget(DummyRight));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M3_MeleeThreeHitComboGroundAndAirTest,
	"InfiniteUniverse.Milestone3.MeleeThreeHitComboGroundAndAir",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M3_MeleeThreeHitComboGroundAndAirTest::RunTest(const FString& Parameters)
{
	IU_M3TestHelpers::FScopedCombatTestWorld ScopeWorld;

	AIU_PlayerCharacter* Player = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	AIU_PlayerController* PC = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	PC->Possess(Player);

	AIU_PracticeDummy* Dummy = ScopeWorld.World->SpawnActor<AIU_PracticeDummy>(FVector(130.0, 0.0, 0.0), FRotator::ZeroRotator);
	Dummy->GetAttributeComponent()->MaxHP = 300.0f;
	Dummy->GetAttributeComponent()->ResetAttributesToFull();

	UIU_CombatComponent* Combat = Player->GetCombatComponent();
	Player->GetTargetingComponent()->SetLockedTarget(Dummy);

	// Hit 1
	TestTrue(TEXT("Combo Hit 1 started"), Combat->RequestMeleeAttack());
	TestEqual(TEXT("State is Attacking"), Combat->GetCombatState(), EIU_CombatState::Attacking);
	TestEqual(TEXT("Combo step is 1"), Combat->GetCurrentComboStep(), 1);
	TestEqual(TEXT("Dummy HP reduced by Hit 1 (15)"), Dummy->GetAttributeComponent()->GetHP(), 285.0f);

	// Queue Hit 2 while Attacking (must NOT immediately apply Hit 2 damage before step 1 timer finishes!)
	TestTrue(TEXT("Queued Hit 2 cleanly during Hit 1"), Combat->RequestMeleeAttack());
	TestEqual(TEXT("No overlapping instant damage before step 1 finishes"), Dummy->GetAttributeComponent()->GetHP(), 285.0f);

	// Advance past step 1 duration -> auto-chains into Hit 2!
	ScopeWorld.TickSimulation(Combat->AttackStepDuration + 0.03f, Player);
	TestEqual(TEXT("Chained into Combo Step 2"), Combat->GetCurrentComboStep(), 2);
	TestEqual(TEXT("Dummy HP reduced by Hit 2 (20 -> 265)"), Dummy->GetAttributeComponent()->GetHP(), 265.0f);

	// Queue Hit 3 and advance -> chains into Hit 3 (30 dmg -> 235), then finishes back to Idle!
	TestTrue(TEXT("Queued Hit 3 cleanly"), Combat->RequestMeleeAttack());
	ScopeWorld.TickSimulation(Combat->AttackStepDuration + 0.03f, Player);
	TestEqual(TEXT("Chained into Combo Step 3"), Combat->GetCurrentComboStep(), 3);
	TestEqual(TEXT("Dummy HP reduced by Hit 3 (30 -> 235)"), Dummy->GetAttributeComponent()->GetHP(), 235.0f);

	ScopeWorld.TickSimulation(Combat->AttackStepDuration + 0.05f, Player);
	TestEqual(TEXT("Returned cleanly to Idle after 3-hit combo"), Combat->GetCombatState(), EIU_CombatState::Idle);
	TestEqual(TEXT("Combo counter reset to 0 after step 3"), Combat->GetCurrentComboStep(), 0);

	// Verify Aerial Combat: Enter Free 3D Flight at Z=600 and strike an airborne dummy!
	Player->SetActorLocation(FVector(0.0, 0.0, 600.0));
	Player->GetFlightComponent()->EnterFlight();
	AIU_PracticeDummy* SkyDummy = ScopeWorld.World->SpawnActor<AIU_PracticeDummy>(FVector(140.0, 0.0, 600.0), FRotator::ZeroRotator);
	Player->GetTargetingComponent()->SetLockedTarget(SkyDummy);

	TestTrue(TEXT("Player is Flying"), Player->GetFlightComponent()->IsFlying());
	TestTrue(TEXT("Aerial Melee Attack succeeds while Flying"), Combat->RequestMeleeAttack());
	TestEqual(TEXT("Airborne dummy received melee damage (85 HP left)"), SkyDummy->GetAttributeComponent()->GetHP(), 85.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M3_KiBlastDamageAndDeadGuardTest,
	"InfiniteUniverse.Milestone3.KiBlastDamageAndDeadGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M3_KiBlastDamageAndDeadGuardTest::RunTest(const FString& Parameters)
{
	IU_M3TestHelpers::FScopedCombatTestWorld ScopeWorld;

	AIU_PlayerCharacter* Player = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	AIU_PlayerController* PC = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	PC->Possess(Player);

	AIU_PracticeDummy* Dummy = ScopeWorld.World->SpawnActor<AIU_PracticeDummy>(FVector(450.0, 0.0, 0.0), FRotator::ZeroRotator);
	Dummy->bAutoResetAfterDefeat = false;
	Dummy->GetAttributeComponent()->MaxHP = 25.0f;
	Dummy->GetAttributeComponent()->ResetAttributesToFull();

	UIU_AttributeComponent* PlayerAttr = Player->GetAttributeComponent();
	UIU_CombatComponent* Combat = Player->GetCombatComponent();
	Player->GetTargetingComponent()->SetLockedTarget(Dummy);

	const float InitialKi = PlayerAttr->GetKi();
	AIU_KiProjectile* Proj1 = Combat->RequestKiBlast();
	TestNotNull(TEXT("Ki Blast projectile spawned"), Proj1);
	TestEqual(TEXT("Ki deducted accurately by KiBlastKiCost"), PlayerAttr->GetKi(), InitialKi - Combat->KiBlastKiCost);

	// Immediate second shot must be blocked by cooldown
	TestNull(TEXT("Cooldown prevents instant spam Ki Blast"), Combat->RequestKiBlast());

	// Fly projectile into Dummy -> deals 20 damage (25 -> 5 HP) and triggers Hit Reaction!
	ScopeWorld.TickSimulation(0.35f, Player, Proj1);
	TestEqual(TEXT("Ki Blast dealt 20 damage to Dummy (5 HP remaining)"), Dummy->GetAttributeComponent()->GetHP(), 5.0f);

	// Wait for cooldown, then drain player Ki to 0 -> RequestKiBlast must fail when insufficient Ki!
	PlayerAttr->TryConsumeKi(PlayerAttr->GetKi());
	PlayerAttr->PassiveKiRegenPerSecond = 0.0f;
	TestNull(TEXT("Cannot fire Ki Blast with 0 Ki"), Combat->RequestKiBlast());

	// Restore Ki, fire lethal Ki Blast -> Dummy reaches 0 HP (Dead), then verify dead target ignores extra damage!
	PlayerAttr->RestoreKi(50.0f);
	AIU_KiProjectile* Proj2 = Combat->RequestKiBlast();
	TestNotNull(TEXT("Second Ki Blast spawned after Ki restore"), Proj2);
	ScopeWorld.TickSimulation(0.35f, Player, Proj2);

	TestTrue(TEXT("Dummy defeated at 0 HP"), Dummy->GetAttributeComponent()->IsDead());
	TestEqual(TEXT("Dummy HP clamped cleanly at 0"), Dummy->GetAttributeComponent()->GetHP(), 0.0f);

	FIU_DamageSpec ExtraSpec;
	ExtraSpec.Amount = 50.0f;
	ExtraSpec.InstigatorActor = Player;
	TestFalse(TEXT("Dead target strictly rejects additional damage"), Dummy->GetDamageComponent()->ApplyDamageSpec(ExtraSpec));
	TestEqual(TEXT("Dead target HP remains 0"), Dummy->GetAttributeComponent()->GetHP(), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M3_DodgeStateAndInvulnerabilityTest,
	"InfiniteUniverse.Milestone3.DodgeStateAndInvulnerability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M3_DodgeStateAndInvulnerabilityTest::RunTest(const FString& Parameters)
{
	// 0. Verify Q -> KiBlast and RightMouseButton -> Dodge/Dash/Evade mapping in IMC_Player
	if (UInputMappingContext* DiskIMC = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Player.IMC_Player")))
	{
		bool bFoundQKiBlast = false;
		bool bFoundRMBDodge = false;
		for (const FEnhancedActionKeyMapping& Mapping : DiskIMC->GetMappings())
		{
			if (Mapping.Action && Mapping.Action->GetName() == TEXT("IA_KiBlast") && Mapping.Key == EKeys::Q)
			{
				bFoundQKiBlast = true;
			}
			if (Mapping.Action && Mapping.Action->GetName() == TEXT("IA_Dodge") && Mapping.Key == EKeys::RightMouseButton)
			{
				bFoundRMBDodge = true;
			}
		}
		TestTrue(TEXT("IMC_Player binds Q to IA_KiBlast"), bFoundQKiBlast);
		TestTrue(TEXT("IMC_Player binds RightMouseButton to IA_Dodge"), bFoundRMBDodge);
	}

	IU_M3TestHelpers::FScopedCombatTestWorld ScopeWorld;

	AIU_PlayerCharacter* Player = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	AIU_PlayerController* PC = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	PC->Possess(Player);

	UIU_CombatComponent* Combat = Player->GetCombatComponent();
	UIU_DamageComponent* DmgComp = Player->GetDamageComponent();
	UIU_AttributeComponent* Attr = Player->GetAttributeComponent();

	const FVector OriginA(0.0, 0.0, 0.0);

	// 1. Earthling Dash ("Lướt kiểu người Trái Đất" - no enemy nearby): continuous A -> B_Dash over DodgeDuration
	Combat->SetMobilityStyle(EIU_MobilityStepStyle::Dash);
	Player->SetActorLocation(OriginA);
	Player->SetActorRotation(FRotator::ZeroRotator);

	Player->DodgeInput();
	TestEqual(TEXT("CombatState is Dodging during Earthling Dash"), Combat->GetCombatState(), EIU_CombatState::Dodging);
	TestFalse(TEXT("No enemy nearby -> not Mirage Evade"), Combat->IsLastDodgeCombatEvade());
	TestFalse(TEXT("Earthling Dash streams continuously (not instant frame-0 blink)"), Combat->DidLastDodgeBlinkInstantly());
	TestNull(TEXT("No afterimage spawned during normal Earthling Dash"), Combat->GetLastSpawnedAfterimage());

	// Tick right to the end of DodgeDuration (0.20s) to sample exact arrival destination B_Dash (before post-dash gravity fall)
	ScopeWorld.TickSimulation(Combat->DodgeDuration + (1.0f / 60.0f), Player);
	const FVector DestB_EarthDash = Player->GetActorLocation();
	TestEqual(TEXT("Returned to Idle after Earthling Dash"), Combat->GetCombatState(), EIU_CombatState::Idle);
	TestTrue(TEXT("Earthling Dash traveled full MobilityTravelDistance (~420 cm)"),
		FMath::IsNearlyEqual((float)DestB_EarthDash.X, Combat->MobilityTravelDistance, 2.0f));

	// Wait remaining cooldown
	ScopeWorld.TickSimulation(Combat->DodgeCooldown, Player);

	// 2. Alien InstantBlink ("Dịch chuyển tức thời kiểu người Hành Tinh" - no enemy nearby): arrives at IDENTICAL B!
	Combat->SetMobilityStyle(EIU_MobilityStepStyle::InstantBlink);
	Player->SetActorLocation(OriginA);
	Player->SetActorRotation(FRotator::ZeroRotator);

	Player->DodgeInput();
	const FVector DestB_AlienBlink = Player->GetActorLocation();
	TestTrue(TEXT("Alien InstantBlink relocates on frame 0"), Combat->DidLastDodgeBlinkInstantly());
	TestNull(TEXT("No afterimage spawned during non-combat Alien InstantBlink"), Combat->GetLastSpawnedAfterimage());
	TestTrue(TEXT("Alien InstantBlink lands at EXACT SAME destination B as Earthling Dash"),
		FVector::Dist(DestB_AlienBlink, DestB_EarthDash) < 1.0f);

	ScopeWorld.TickSimulation(Combat->DodgeCooldown + 0.08f, Player);

	// 3. Black Myth: Wukong style Mirage Evade ("Né khi có quái"):
	//    Spawns translucent Afterimage Ghost right at Origin A, activates i-frames, AND lands at EXACT SAME B!
	Combat->SetMobilityStyle(EIU_MobilityStepStyle::Dash);
	Player->SetActorLocation(OriginA);
	Player->SetActorRotation(FRotator::ZeroRotator);
	AIU_PracticeDummy* Attacker = ScopeWorld.World->SpawnActor<AIU_PracticeDummy>(FVector(200.0, 120.0, 0.0), FRotator::ZeroRotator);

	Player->DodgeInput();
	const FVector DestB_MirageEvade = Player->GetActorLocation();
	AIU_AfterimageGhost* MirageGhost = Combat->GetLastSpawnedAfterimage();

	TestEqual(TEXT("CombatState is Dodging during Wukong Mirage Evade"), Combat->GetCombatState(), EIU_CombatState::Dodging);
	TestTrue(TEXT("Nearby enemy auto-detected -> Mirage Evade active"), Combat->IsLastDodgeCombatEvade());
	TestTrue(TEXT("Player is invulnerable during Mirage Evade"), DmgComp->IsInvulnerable());
	TestNotNull(TEXT("Wukong Afterimage Ghost decoy spawned at origin A"), MirageGhost);
	if (MirageGhost)
	{
		TestTrue(TEXT("Afterimage Ghost remains frozen at exact origin A"),
			FVector::Dist(MirageGhost->GetActorLocation(), OriginA) < 1.0f);
	}
	TestTrue(TEXT("Mirage Evade arrives at EXACT SAME destination B as free-roam Dash/Blink!"),
		FVector::Dist(DestB_MirageEvade, DestB_EarthDash) < 1.0f);

	// Incoming attack during Mirage Evade must be completely negated (0 HP lost)
	FIU_DamageSpec IncomingHit;
	IncomingHit.Amount = 35.0f;
	IncomingHit.InstigatorActor = Attacker;
	TestFalse(TEXT("Damage rejected while Evading"), DmgComp->ApplyDamageSpec(IncomingHit));
	TestEqual(TEXT("Player HP unchanged at 100"), Attr->GetHP(), 100.0f);

	// Advance past DodgeDuration -> returns cleanly to Idle & vulnerable again
	ScopeWorld.TickSimulation(Combat->DodgeDuration + 0.05f, Player);
	TestEqual(TEXT("Dodge finished cleanly back to Idle"), Combat->GetCombatState(), EIU_CombatState::Idle);
	TestFalse(TEXT("Invulnerability cleared after Dodge"), DmgComp->IsInvulnerable());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
