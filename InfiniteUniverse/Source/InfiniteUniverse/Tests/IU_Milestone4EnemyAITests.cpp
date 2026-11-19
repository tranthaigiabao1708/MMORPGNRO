// Copyright InfiniteUniverse Dev. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
#include "AI/IU_EnemyAIController.h"
#include "AI/IU_EnemySpawner.h"
#include "Core/IU_PlayerController.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Combat/IU_CombatComponent.h"
#include "Combat/IU_TargetingComponent.h"
#include "Combat/IU_CombatThreatSubsystem.h"

namespace IU_M4TestHelpers
{
	struct FScopedEnemyAITestWorld
	{
		UWorld* World = nullptr;
		UWorld* PrevGWorld = nullptr;

		FScopedEnemyAITestWorld()
		{
			PrevGWorld = GWorld;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("IU_M4_EnemyAITestWorld")));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			GWorld = World;
			FURL URL;
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
			World->SetBegunPlay(true);

			// Spawn a wide walkable arena floor (top surface at Z = +50 cm)
			if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
			{
				AStaticMeshActor* FloorActor = World->SpawnActor<AStaticMeshActor>(FVector(0.0, 0.0, 0.0), FRotator::ZeroRotator);
				if (FloorActor && FloorActor->GetStaticMeshComponent())
				{
					FloorActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
					FloorActor->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
					FloorActor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
					FloorActor->SetActorScale3D(FVector(100.0, 100.0, 1.0));
				}
			}
		}

		void TickSimulation(
			float TotalSeconds,
			AIU_PlayerCharacter* Player = nullptr,
			AIU_EnemyCharacter* Enemy = nullptr,
			float StepDelta = 1.0f / 60.0f)
		{
			const int32 Steps = FMath::Max(1, FMath::RoundToInt(TotalSeconds / StepDelta));
			for (int32 i = 0; i < Steps; ++i)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, StepDelta);
			}
		}

		~FScopedEnemyAITestWorld()
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

// ============================================================================
// TEST 1: Idle -> Detect -> Chase & Leash Disengage (P4-01, P4-02, P4-03, P4-04)
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M4_IdleDetectAndChasePlayerTest,
	"InfiniteUniverse.Milestone4.IdleDetectAndChasePlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M4_IdleDetectAndChasePlayerTest::RunTest(const FString& Parameters)
{
	IU_M4TestHelpers::FScopedEnemyAITestWorld ScopeWorld;

	AIU_EnemyCharacter* Enemy = ScopeWorld.World->SpawnActor<AIU_EnemyCharacter>(FVector(0.0, 0.0, 145.0), FRotator::ZeroRotator);
	AIU_PlayerCharacter* Player = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(1800.0, 0.0, 145.0), FRotator::ZeroRotator);
	AIU_PlayerController* PC = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	PC->MarkAsLocalTestController();
	PC->Possess(Player);

	TestNotNull(TEXT("Enemy spawned"), Enemy);
	TestNotNull(TEXT("Enemy AIController possessed automatically"), Enemy->GetEnemyAIController());

	Enemy->DetectionRange = 1000.0f;
	Enemy->LeashDisengageRange = 1600.0f;
	Enemy->DetectAlertDuration = 0.20f;
	Enemy->ChaseMoveSpeed = 500.0f;

	// 1. Player at 1800 cm (> DetectionRange 1000 cm) -> Enemy stays strictly in Idle
	ScopeWorld.TickSimulation(0.50f, Player, Enemy);
	TestEqual(TEXT("Enemy remains Idle when player is outside DetectionRange"), Enemy->GetEnemyAIState(), EIU_EnemyAIState::Idle);
	TestTrue(TEXT("Enemy does not wander away while Idle"), FMath::Abs(Enemy->GetActorLocation().X) < 5.0);

	// 2. Move Player to 800 cm (inside DetectionRange 1000 cm) -> Idle -> Detect
	Player->SetActorLocation(FVector(800.0, 0.0, 145.0));
	ScopeWorld.TickSimulation(0.08f, Player, Enemy);
	TestEqual(TEXT("Enemy enters Detect state upon spotting player"), Enemy->GetEnemyAIState(), EIU_EnemyAIState::Detect);

	// 3. After DetectAlertDuration (0.20s) -> Detect -> Chase and closes distance!
	ScopeWorld.TickSimulation(0.65f, Player, Enemy);
	TestEqual(TEXT("Enemy transitions to Chase state"), Enemy->GetEnemyAIState(), EIU_EnemyAIState::Chase);
	TestTrue(TEXT("Enemy moved forward along +X chasing player"), Enemy->GetActorLocation().X > 120.0);

	// 4. Teleport Player beyond LeashDisengageRange (3000 cm) -> Enemy disengages to Idle
	Player->SetActorLocation(FVector(3000.0, 0.0, 145.0));
	ScopeWorld.TickSimulation(0.10f, Player, Enemy);
	TestEqual(TEXT("Enemy disengages back to Idle when player exceeds LeashDisengageRange"), Enemy->GetEnemyAIState(), EIU_EnemyAIState::Idle);

	return true;
}

// ============================================================================
// TEST 2: Attack Windup, Player Damage, Recover & Perfect Evade Threat (P4-05)
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M4_AttackDamageRecoverAndEvadeIntegrationTest,
	"InfiniteUniverse.Milestone4.AttackPlayerDamageAndRecoverLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M4_AttackDamageRecoverAndEvadeIntegrationTest::RunTest(const FString& Parameters)
{
	IU_M4TestHelpers::FScopedEnemyAITestWorld ScopeWorld;

	AIU_EnemyCharacter* Enemy = ScopeWorld.World->SpawnActor<AIU_EnemyCharacter>(FVector(0.0, 0.0, 145.0), FRotator::ZeroRotator);
	AIU_PlayerCharacter* Player = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(380.0, 0.0, 145.0), FRotator(0.0, 180.0, 0.0));
	AIU_PlayerController* PC = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	PC->MarkAsLocalTestController();
	PC->Possess(Player);

	Enemy->DetectionRange = 1000.0f;
	Enemy->DetectAlertDuration = 0.10f;
	Enemy->AttackRange = 210.0f;
	Enemy->AttackWindupDuration = 0.45f;
	Enemy->AttackRecoveryDuration = 0.60f;
	Enemy->AttackDamage = 20.0f;

	UIU_AttributeComponent* PlayerAttr = Player->GetAttributeComponent();
	const float InitialPlayerHP = PlayerAttr->GetHP();

	// Step frame-by-frame (max 1.0s) until Enemy chases from 380 cm into <= 210 cm and enters Attack windup
	float ElapsedWait = 0.0f;
	while (ElapsedWait < 1.0f && Enemy->GetEnemyAIState() != EIU_EnemyAIState::Attack)
	{
		ScopeWorld.TickSimulation(1.0f / 60.0f, Player, Enemy);
		ElapsedWait += (1.0f / 60.0f);
	}
	TestEqual(TEXT("Enemy chased into AttackRange and entered Attack state"), Enemy->GetEnemyAIState(), EIU_EnemyAIState::Attack);
	TestTrue(TEXT("Enemy is actively winding up telegraphed strike"), Enemy->IsWindingUpAttack());

	// Advance 0.22s into the 0.45s windup (0.23s remaining <= 0.30s window) and verify UIU_CombatThreatSubsystem threat!
	ScopeWorld.TickSimulation(0.22f, Player, Enemy);
	UIU_CombatThreatSubsystem* ThreatSubsys = ScopeWorld.World->GetSubsystem<UIU_CombatThreatSubsystem>();
	TestNotNull(TEXT("CombatThreatSubsystem active"), ThreatSubsys);
	TestTrue(TEXT("Enemy AI windup registers imminent threat for Player Perfect Evade"),
		ThreatSubsys && ThreatSubsys->HasImminentThreatAgainstActor(Player));

	// Advance 0.30s past impact without dodging -> Player takes AttackDamage (20) and Enemy enters Recover!
	ScopeWorld.TickSimulation(0.30f, Player, Enemy);
	TestTrue(TEXT("Player received Enemy AI attack damage"),
		FMath::IsNearlyEqual(PlayerAttr->GetHP(), InitialPlayerHP - 20.0f, 0.5f));
	TestEqual(TEXT("Enemy transitioned cleanly from Attack to Recover"), Enemy->GetEnemyAIState(), EIU_EnemyAIState::Recover);

	// After AttackRecoveryDuration (0.60s), Enemy resumes combat (Attack or Chase)
	ScopeWorld.TickSimulation(0.70f, Player, Enemy);
	TestTrue(TEXT("Enemy exits Recover back into active combat"),
		Enemy->GetEnemyAIState() == EIU_EnemyAIState::Attack || Enemy->GetEnemyAIState() == EIU_EnemyAIState::Chase);

	return true;
}

// ============================================================================
// TEST 3: Hit Reaction Interrupt, Death State & Post-Death Safety (P4-06, P4-07)
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M4_HitReactionInterruptAndDeathTest,
	"InfiniteUniverse.Milestone4.HitReactionInterruptAndDeathStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M4_HitReactionInterruptAndDeathTest::RunTest(const FString& Parameters)
{
	IU_M4TestHelpers::FScopedEnemyAITestWorld ScopeWorld;

	AIU_EnemyCharacter* Enemy = ScopeWorld.World->SpawnActor<AIU_EnemyCharacter>(FVector(200.0, 0.0, 145.0), FRotator(0.0, 180.0, 0.0));
	AIU_PlayerCharacter* Player = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0, 0.0, 145.0), FRotator::ZeroRotator);
	AIU_PlayerController* PC = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	PC->MarkAsLocalTestController();
	PC->Possess(Player);

	Enemy->DetectAlertDuration = 0.05f;
	Enemy->AttackWindupDuration = 0.50f;

	// Lock-On to the living Enemy
	UIU_TargetingComponent* PlayerTargetComp = Player->GetTargetingComponent();
	TestTrue(TEXT("Player locks onto living AIU_EnemyCharacter"), PlayerTargetComp->ToggleLockOn());
	TestEqual(TEXT("Locked target is Enemy"), PlayerTargetComp->GetLockedTarget(), Cast<AActor>(Enemy));

	// Wait until Enemy starts winding up an attack (at t = 0.15s)
	ScopeWorld.TickSimulation(0.15f, Player, Enemy);
	TestEqual(TEXT("Enemy entered Attack windup"), Enemy->GetEnemyAIState(), EIU_EnemyAIState::Attack);

	const float PlayerHPBeforeInterrupt = Player->GetAttributeComponent()->GetHP();

	// Strike Enemy mid-windup -> must interrupt Attack into HitReaction and deal ZERO damage to Player!
	FIU_DamageSpec HitSpec;
	HitSpec.Amount = 40.0f;
	HitSpec.InstigatorActor = Player;
	HitSpec.DamageCauser = Player;
	HitSpec.HitDirection = FVector(1.0f, 0.0f, 0.0f);
	HitSpec.HitStunDuration = 0.30f;

	TestTrue(TEXT("Enemy accepted player hit"), Enemy->GetDamageComponent()->ApplyDamageSpec(HitSpec));
	TestEqual(TEXT("Enemy transitioned immediately to HitReaction"), Enemy->GetEnemyAIState(), EIU_EnemyAIState::HitReaction);
	TestFalse(TEXT("Enemy active windup was cancelled"), Enemy->IsWindingUpAttack());

	ScopeWorld.TickSimulation(0.60f, Player, Enemy);
	TestEqual(TEXT("Interrupted Enemy attack dealt zero damage to Player"), Player->GetAttributeComponent()->GetHP(), PlayerHPBeforeInterrupt);

	// Deal lethal damage (remaining 80 HP) -> Enemy transitions to Dead, releases Lock-On, and never attacks again
	HitSpec.Amount = 200.0f;
	TestTrue(TEXT("Lethal blow applied to Enemy"), Enemy->GetDamageComponent()->ApplyDamageSpec(HitSpec));
	TestTrue(TEXT("Enemy IsDead() is true"), Enemy->IsDead());
	TestEqual(TEXT("Enemy AI state is Dead"), Enemy->GetEnemyAIState(), EIU_EnemyAIState::Dead);
	TestFalse(TEXT("Player Lock-On auto-released upon Enemy death"), PlayerTargetComp->IsLockedOn());
	TestEqual(TEXT("Dead Enemy capsule collision disabled"),
		Enemy->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

	// Verify dead enemy rejects post-death damage and never attacks Player even after 1.5s
	TestFalse(TEXT("Dead Enemy rejects additional damage"), Enemy->GetDamageComponent()->ApplyDamageSpec(HitSpec));
	ScopeWorld.TickSimulation(1.50f, Player, Enemy);
	TestEqual(TEXT("Dead Enemy never deals damage after death"), Player->GetAttributeComponent()->GetHP(), PlayerHPBeforeInterrupt);
	TestEqual(TEXT("Dead Enemy remains in Dead state"), Enemy->GetEnemyAIState(), EIU_EnemyAIState::Dead);

	return true;
}

// ============================================================================
// TEST 4: AIU_EnemySpawner Auto-Respawn & State Reset (P4-08)
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M4_EnemySpawnerAutoRespawnTest,
	"InfiniteUniverse.Milestone4.EnemySpawnerAutoRespawnAndReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M4_EnemySpawnerAutoRespawnTest::RunTest(const FString& Parameters)
{
	IU_M4TestHelpers::FScopedEnemyAITestWorld ScopeWorld;

	const FVector SpawnLoc(-600.0, 250.0, 145.0);
	AIU_EnemySpawner* Spawner = ScopeWorld.World->SpawnActor<AIU_EnemySpawner>(SpawnLoc, FRotator::ZeroRotator);
	TestNotNull(TEXT("AIU_EnemySpawner created"), Spawner);

	Spawner->RespawnDelaySeconds = 1.0f;
	AIU_EnemyCharacter* ManagedEnemy = Spawner->GetSpawnedEnemy();
	if (!ManagedEnemy)
	{
		ManagedEnemy = Spawner->RespawnEnemyNow();
	}
	TestNotNull(TEXT("Spawner spawned initial AIU_EnemyCharacter"), ManagedEnemy);
	TestEqual(TEXT("Initial spawned Enemy is Idle"), ManagedEnemy->GetEnemyAIState(), EIU_EnemyAIState::Idle);

	// Move Enemy away slightly and defeat it
	ManagedEnemy->SetActorLocation(SpawnLoc + FVector(300.0, 0.0, 0.0));
	FIU_DamageSpec LethalSpec;
	LethalSpec.Amount = 500.0f;
	LethalSpec.HitStunDuration = 0.0f;
	TestTrue(TEXT("Defeated spawned Enemy"), ManagedEnemy->GetDamageComponent()->ApplyDamageSpec(LethalSpec));
	TestTrue(TEXT("Enemy is Dead"), ManagedEnemy->IsDead());
	TestTrue(TEXT("Spawner scheduled auto-respawn timer"), Spawner->IsRespawnPending());

	// Advance past RespawnDelaySeconds (1.0s) -> Enemy resets cleanly at Spawner origin!
	ScopeWorld.TickSimulation(1.15f, nullptr, ManagedEnemy);

	AIU_EnemyCharacter* RespawnedEnemy = Spawner->GetSpawnedEnemy();
	TestNotNull(TEXT("Respawned Enemy valid"), RespawnedEnemy);
	TestFalse(TEXT("Respawned Enemy is alive"), RespawnedEnemy->IsDead());
	TestEqual(TEXT("Respawned Enemy HP restored to MaxHP"),
		RespawnedEnemy->GetAttributeComponent()->GetHP(), RespawnedEnemy->GetAttributeComponent()->GetMaxHP());
	TestEqual(TEXT("Respawned Enemy AI state reset to Idle"), RespawnedEnemy->GetEnemyAIState(), EIU_EnemyAIState::Idle);
	TestEqual(TEXT("Respawned Enemy collision restored"),
		RespawnedEnemy->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
	TestTrue(TEXT("Respawned Enemy returned to Spawner location"),
		FVector::Dist2D(RespawnedEnemy->GetActorLocation(), SpawnLoc) < 5.0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
