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
#include "Combat/IU_CombatThreatSubsystem.h"
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
	PC->MarkAsLocalTestController();
	PC->Possess(Player);

	AIU_PracticeDummy* DummyFront = ScopeWorld.World->SpawnActor<AIU_PracticeDummy>(FVector(500.0, -60.0, 0.0), FRotator::ZeroRotator);
	AIU_PracticeDummy* DummyRight = ScopeWorld.World->SpawnActor<AIU_PracticeDummy>(FVector(500.0, 260.0, 0.0), FRotator::ZeroRotator);
	AIU_PracticeDummy* DummyFarOut = ScopeWorld.World->SpawnActor<AIU_PracticeDummy>(FVector(9000.0, 0.0, 0.0), FRotator::ZeroRotator);
	DummyFront->bAutoResetAfterDefeat = false;
	DummyRight->bAutoResetAfterDefeat = false;

	UIU_TargetingComponent* TargetComp = Player->GetTargetingComponent();
	TestNotNull(TEXT("TargetingComponent attached"), TargetComp);

	TestTrue(TEXT("ToggleLockOn acquires valid target"), TargetComp->ToggleLockOn());
	TestEqual(TEXT("Locked onto DummyFront"), TargetComp->GetLockedTarget(), static_cast<AActor*>(DummyFront));
	TestFalse(TEXT("Out-of-range DummyFarOut rejected by SetLockedTarget"), TargetComp->SetLockedTarget(DummyFarOut));

	TestTrue(TEXT("SwitchLockedTarget(+1) switches to right-side target"), TargetComp->SwitchLockedTarget(1.0f));
	TestEqual(TEXT("Now locked onto DummyRight"), TargetComp->GetLockedTarget(), static_cast<AActor*>(DummyRight));

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
	PC->MarkAsLocalTestController();
	PC->Possess(Player);
	Player->GetIUMovementComponent()->GravityScale = 0.0f;

	AIU_PracticeDummy* Dummy = ScopeWorld.World->SpawnActor<AIU_PracticeDummy>(FVector(135.0, 0.0, 0.0), FRotator::ZeroRotator);
	Dummy->GetAttributeComponent()->MaxHP = 300.0f;
	Dummy->GetAttributeComponent()->ResetAttributesToFull();

	UIU_CombatComponent* Combat = Player->GetCombatComponent();
	Combat->MeleeStepLungeSpeed = 0.0f; // Keep crisp fixed spacing in floorless test world
	Player->GetTargetingComponent()->SetLockedTarget(Dummy);

	// Hit 1 starts windup at t=0 (0 damage before windup impact frame!), then deals 15 damage at impact frame
	TestTrue(TEXT("Combo Hit 1 started"), Combat->RequestMeleeAttack());
	TestEqual(TEXT("State is Attacking"), Combat->GetCombatState(), EIU_CombatState::Attacking);
	TestEqual(TEXT("Combo step is 1"), Combat->GetCurrentComboStep(), 1);
	TestEqual(TEXT("No instant frame-0 damage during punch windup"), Dummy->GetAttributeComponent()->GetHP(), 300.0f);

	// Queue Hit 2 during Hit 1 windup
	TestTrue(TEXT("Queued Hit 2 cleanly during Hit 1"), Combat->RequestMeleeAttack());

	// Advance past Hit 1 windup (0.11s) -> Hit 1 deals 15 damage (285 HP)
	ScopeWorld.TickSimulation(0.14f, Player);
	TestEqual(TEXT("Dummy HP reduced by Hit 1 at impact frame (15 -> 285)"), Dummy->GetAttributeComponent()->GetHP(), 285.0f);

	// Advance past step 1 completion + Hit 2 impact frame -> Hit 2 deals 20 damage (265 HP)
	ScopeWorld.TickSimulation(Combat->AttackStepDuration + 0.04f, Player);
	TestEqual(TEXT("Chained into Combo Step 2"), Combat->GetCurrentComboStep(), 2);
	TestEqual(TEXT("Dummy HP reduced by Hit 2 (20 -> 265)"), Dummy->GetAttributeComponent()->GetHP(), 265.0f);

	// Queue Hit 3 and advance past step 2 completion + Hit 3 impact frame -> Hit 3 deals 30 damage (235 HP)
	TestTrue(TEXT("Queued Hit 3 cleanly"), Combat->RequestMeleeAttack());
	ScopeWorld.TickSimulation(Combat->AttackStepDuration + 0.06f, Player);
	TestEqual(TEXT("Chained into Combo Step 3"), Combat->GetCurrentComboStep(), 3);
	TestEqual(TEXT("Dummy HP reduced by Hit 3 (30 -> 235)"), Dummy->GetAttributeComponent()->GetHP(), 235.0f);

	ScopeWorld.TickSimulation(Combat->AttackStepDuration, Player);
	TestEqual(TEXT("Returned cleanly to Idle after 3-hit combo"), Combat->GetCombatState(), EIU_CombatState::Idle);
	TestEqual(TEXT("Combo counter reset to 0 after step 3"), Combat->GetCurrentComboStep(), 0);

	// Verify Aerial Combat: Enter Free 3D Flight at Z=600 and strike an airborne dummy!
	Player->SetActorLocation(FVector(0.0, 0.0, 600.0));
	Player->GetFlightComponent()->EnterFlight();
	AIU_PracticeDummy* SkyDummy = ScopeWorld.World->SpawnActor<AIU_PracticeDummy>(FVector(140.0, 0.0, 600.0), FRotator::ZeroRotator);
	Player->GetTargetingComponent()->SetLockedTarget(SkyDummy);

	TestTrue(TEXT("Player is Flying"), Player->GetFlightComponent()->IsFlying());
	TestTrue(TEXT("Aerial Melee Attack succeeds while Flying"), Combat->RequestMeleeAttack());
	ScopeWorld.TickSimulation(0.15f, Player);
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
	PC->MarkAsLocalTestController();
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

	TestNull(TEXT("Cooldown prevents instant spam Ki Blast"), Combat->RequestKiBlast());

	ScopeWorld.TickSimulation(0.35f, Player, Proj1);
	TestEqual(TEXT("Ki Blast dealt 20 damage to Dummy (5 HP remaining)"), Dummy->GetAttributeComponent()->GetHP(), 5.0f);

	PlayerAttr->TryConsumeKi(PlayerAttr->GetKi());
	PlayerAttr->PassiveKiRegenPerSecond = 0.0f;
	TestNull(TEXT("Cannot fire Ki Blast with 0 Ki"), Combat->RequestKiBlast());

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
	PC->MarkAsLocalTestController();
	PC->Possess(Player);
	Player->GetIUMovementComponent()->GravityScale = 0.0f;

	UIU_CombatComponent* Combat = Player->GetCombatComponent();
	UIU_DamageComponent* DmgComp = Player->GetDamageComponent();
	UIU_AttributeComponent* Attr = Player->GetAttributeComponent();

	const FVector OriginA(0.0, 0.0, 0.0);

	// 1. Standing right next to a NON-ATTACKING monster (150 cm away): MUST NOT trigger Perfect Evade or spawn Afterimage!
	AIU_PracticeDummy* Monster = ScopeWorld.World->SpawnActor<AIU_PracticeDummy>(FVector(150.0, 120.0, 0.0), FRotator::ZeroRotator);
	Combat->SetMobilityStyle(EIU_MobilityStepStyle::Dash);
	Player->SetActorLocation(OriginA);
	Player->SetActorRotation(FRotator::ZeroRotator);

	Player->DodgeInput();
	TestFalse(TEXT("Idle nearby monster MUST NOT trigger Perfect Evade"), Combat->IsLastDodgeCombatEvade());
	TestNull(TEXT("Idle nearby monster MUST NOT spawn Afterimage Ghost"), Combat->GetLastSpawnedAfterimage());

	ScopeWorld.TickSimulation(Combat->DodgeDuration + (1.0f / 60.0f), Player);
	const FVector DestB_EarthDash = Player->GetActorLocation();
	TestTrue(TEXT("Earthling Dash traveled full MobilityTravelDistance (~420 cm)"),
		FMath::IsNearlyEqual((float)DestB_EarthDash.X, Combat->MobilityTravelDistance, 2.0f));
	ScopeWorld.TickSimulation(Combat->DodgeCooldown, Player);

	// 2. Alien InstantBlink (no attack): arrives at EXACT SAME B with ZERO afterimage!
	Combat->SetMobilityStyle(EIU_MobilityStepStyle::InstantBlink);
	Player->SetActorLocation(OriginA);
	Player->SetActorRotation(FRotator::ZeroRotator);

	Player->DodgeInput();
	const FVector DestB_AlienBlink = Player->GetActorLocation();
	TestFalse(TEXT("Alien InstantBlink without incoming attack is NOT Perfect Evade"), Combat->IsLastDodgeCombatEvade());
	TestNull(TEXT("Alien InstantBlink without incoming attack spawns NO Afterimage"), Combat->GetLastSpawnedAfterimage());
	TestTrue(TEXT("Alien InstantBlink lands at EXACT SAME destination B as Earthling Dash"),
		FVector::Dist(DestB_AlienBlink, DestB_EarthDash) < 1.0f);
	ScopeWorld.TickSimulation(Combat->DodgeCooldown + 0.05f, Player);

	// 3. Monster winds up a 0.50s attack, but Player dodges TOO EARLY at t=0.05s (Remaining = 0.45s > 0.30s AND Gap > 20 cm):
	//    MUST execute normal Dash (NO Afterimage Ghost)!
	Combat->SetMobilityStyle(EIU_MobilityStepStyle::Dash);
	Player->SetActorLocation(OriginA);
	Player->SetActorRotation(FRotator::ZeroRotator);
	Monster->SetActorLocation(FVector(240.0, 0.0, 0.0));
	Monster->TriggerTelegraphedStrike(OriginA, 0.50f); // 0.50s windup; fist starts ~110 cm away from player skin

	ScopeWorld.TickSimulation(0.05f, Player); // t = 0.05s -> 0.45s remaining (> 0.30s) & fist gap > 20 cm
	Player->DodgeInput();
	TestFalse(TEXT("Dodging too early (0.45s before hit & >20cm gap) does NOT trigger Perfect Evade"), Combat->IsLastDodgeCombatEvade());
	TestNull(TEXT("Dodging too early spawns NO Afterimage Ghost"), Combat->GetLastSpawnedAfterimage());
	ScopeWorld.TickSimulation(0.60f, Player);

	// 4. Rule A (<= 0.30s Time-to-Impact): Monster winds up 0.50s attack; Player dodges at t=0.25s (0.25s remaining <= 0.30s)!
	//    MUST trigger Black Myth: Wukong Perfect Evade + Afterimage Ghost at A + exact same destination B!
	Player->SetActorLocation(OriginA);
	Player->SetActorRotation(FRotator::ZeroRotator);
	Monster->TriggerTelegraphedStrike(OriginA, 0.50f);

	ScopeWorld.TickSimulation(0.25f, Player); // t = 0.25s -> 0.25s remaining (<= 0.30s)!
	Player->DodgeInput();
	const FVector DestB_PerfectEvadeTime = Player->GetActorLocation();
	AIU_AfterimageGhost* GhostFromTimeRule = Combat->GetLastSpawnedAfterimage();

	TestTrue(TEXT("Dodging within <= 0.30s of damage triggers Perfect Evade"), Combat->IsLastDodgeCombatEvade());
	TestTrue(TEXT("Perfect Evade grants i-frames"), DmgComp->IsInvulnerable());
	TestNotNull(TEXT("Perfect Evade (<=0.30s rule) spawns Wukong Afterimage Ghost at A"), GhostFromTimeRule);
	if (GhostFromTimeRule)
	{
		TestTrue(TEXT("Afterimage Ghost stays frozen at exact origin A"),
			FVector::Dist(GhostFromTimeRule->GetActorLocation(), OriginA) < 1.0f);
	}
	TestTrue(TEXT("Perfect Evade arrives at EXACT SAME destination B as normal Dash/Blink"),
		FVector::Dist(DestB_PerfectEvadeTime, DestB_EarthDash) < 1.0f);
	TestEqual(TEXT("Player took 0 damage thanks to Perfect Evade"), Attr->GetHP(), 100.0f);

	ScopeWorld.TickSimulation(Combat->DodgeCooldown + 0.15f, Player);

	// 5. Rule B (< 20 cm Physical Hitbox Gap): Slow 1.20s attack still has 0.50s left (> 0.30s!),
	//    but its damaging hitbox is physically 12 cm (< 20 cm) from Player's capsule surface!
	Player->SetActorLocation(OriginA);
	Player->SetActorRotation(FRotator::ZeroRotator);
	UIU_CombatThreatSubsystem* ThreatSubsys = ScopeWorld.World->GetSubsystem<UIU_CombatThreatSubsystem>();
	// Player capsule radius = 40 cm; Hitbox radius = 30 cm; Center at X = 82 cm -> Surface gap = 82 - 40 - 30 = 12 cm (< 20 cm)!
	const int32 ClosePhysicalThreatId = ThreatSubsys->RegisterMeleeAttackWindup(
		Monster,
		FVector(82.0, 0.0, 0.0),
		FVector(82.0, 0.0, 0.0),
		30.0f,
		0.55f); // 0.55s > 0.30s time window, so ONLY the <20cm physical rule can trigger!

	Player->DodgeInput();
	TestTrue(TEXT("Physical hitbox gap (12 cm < 20 cm) triggers Perfect Evade even when time > 0.30s!"),
		Combat->IsLastDodgeCombatEvade());
	TestNotNull(TEXT("Physical < 20 cm Perfect Evade spawns Afterimage Ghost at A"), Combat->GetLastSpawnedAfterimage());
	ThreatSubsys->UnregisterMeleeThreat(ClosePhysicalThreatId);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M3_PvPPlayerVsPlayerPerfectEvadeTest,
	"InfiniteUniverse.Milestone3.PvPPlayerVsPlayerPerfectEvade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M3_PvPPlayerVsPlayerPerfectEvadeTest::RunTest(const FString& Parameters)
{
	IU_M3TestHelpers::FScopedCombatTestWorld ScopeWorld;

	// Spawn Rival Player 1 (Attacker) and Player 2 (Defender) facing each other
	AIU_PlayerCharacter* Player1 = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0, 0.0, 0.0), FRotator::ZeroRotator);
	AIU_PlayerCharacter* Player2 = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(150.0, 0.0, 0.0), FRotator(0.0, 180.0, 0.0));
	AIU_PlayerController* PC1 = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	AIU_PlayerController* PC2 = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	PC1->MarkAsLocalTestController();
	PC2->MarkAsLocalTestController();
	PC1->Possess(Player1);
	PC2->Possess(Player2);
	Player1->GetIUMovementComponent()->GravityScale = 0.0f;
	Player2->GetIUMovementComponent()->GravityScale = 0.0f;
	Player1->GetCombatComponent()->MeleeStepLungeSpeed = 0.0f;

	// 1. Standing face-to-face in PvP WITHOUT Player 1 attacking -> Player 2 RMB does NOT spawn Afterimage!
	const FVector P2DuelOrigin(150.0, 0.0, 0.0);
	Player2->DodgeInput();
	TestFalse(TEXT("PvP: Idle rival player does NOT trigger Perfect Evade"), Player2->GetCombatComponent()->IsLastDodgeCombatEvade());
	TestNull(TEXT("PvP: No Afterimage spawned when rival player is not attacking"), Player2->GetCombatComponent()->GetLastSpawnedAfterimage());

	ScopeWorld.TickSimulation(Player2->GetCombatComponent()->DodgeCooldown + 0.08f, Player2);
	Player2->SetActorLocation(P2DuelOrigin);
	Player2->SetActorRotation(FRotator(0.0, 180.0, 0.0));

	// 2. PvP Melee Attack: Player 1 throws Melee Combo Hit 1 -> Player 2 Perfect-Evades mid-windup!
	TestTrue(TEXT("PvP: Player 1 starts Melee Attack against Player 2"), Player1->GetCombatComponent()->RequestMeleeAttack());
	ScopeWorld.TickSimulation(0.04f, Player1); // Inside Player 1's 0.11s punch windup (<= 0.30s to hit!)

	Player2->DodgeInput();
	TestTrue(TEXT("PvP Melee: Player 2 triggers Perfect Evade against Player 1's punch!"),
		Player2->GetCombatComponent()->IsLastDodgeCombatEvade());
	TestNotNull(TEXT("PvP Melee: Player 2 leaves Wukong Afterimage Ghost at duel origin"),
		Player2->GetCombatComponent()->GetLastSpawnedAfterimage());

	// Advance past Player 1's punch impact frame -> Player 2 takes 0 damage!
	ScopeWorld.TickSimulation(0.55f, Player1);
	ScopeWorld.TickSimulation(0.05f, Player2);
	TestEqual(TEXT("PvP Melee: Player 2 took 0 damage after Perfect Evade"), Player2->GetAttributeComponent()->GetHP(), 100.0f);

	// 3. PvP Ki Blast Projectile: Player 1 fires Q Ki Blast at Player 2 -> Player 2 Perfect-Evades incoming blast!
	Player2->SetActorLocation(FVector(550.0, 0.0, 0.0));
	Player2->SetActorRotation(FRotator(0.0, 180.0, 0.0));
	Player1->GetTargetingComponent()->SetLockedTarget(Player2);

	AIU_KiProjectile* RivalBlast = Player1->GetCombatComponent()->RequestKiBlast();
	TestNotNull(TEXT("PvP KiBlast: Player 1 fired Ki Blast toward Player 2"), RivalBlast);

	// At 550 cm and speed 3000 cm/s, time-to-impact is ~0.14s (<= 0.30s)!
	Player2->DodgeInput();
	TestTrue(TEXT("PvP KiBlast: Player 2 triggers Perfect Evade against incoming Player 1 Ki Blast!"),
		Player2->GetCombatComponent()->IsLastDodgeCombatEvade());
	TestNotNull(TEXT("PvP KiBlast: Player 2 leaves Afterimage Ghost while evading Ki Blast"),
		Player2->GetCombatComponent()->GetLastSpawnedAfterimage());

	ScopeWorld.TickSimulation(0.25f, Player2, RivalBlast);
	TestEqual(TEXT("PvP KiBlast: Player 2 HP untouched at 100"), Player2->GetAttributeComponent()->GetHP(), 100.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
