// Copyright InfiniteUniverse Dev. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include <limits>
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "UObject/UnrealType.h"
#include "Core/IU_GameMode.h"
#include "Core/IU_GameState.h"
#include "Core/IU_PlayerState.h"
#include "Core/IU_PlayerController.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
#include "AI/IU_EnemyAIController.h"
#include "AI/IU_EnemySpawner.h"
#include "Movement/IU_MovementComponent.h"
#include "Movement/IU_FlightComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Combat/IU_CombatComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Combat/IU_TargetingComponent.h"
#include "Combat/IU_CombatThreatSubsystem.h"
#include "Combat/IU_KiProjectile.h"
#include "Combat/IU_AfterimageGhost.h"

namespace IU_S2TestHelpers
{
	struct FScopedStep2NetWorld
	{
		UWorld* World = nullptr;
		UWorld* PrevGWorld = nullptr;

		explicit FScopedStep2NetWorld(bool bSpawnGameModeAndState = true)
		{
			PrevGWorld = GWorld;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("IU_Step2_NetAuthorityWorld")));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			GWorld = World;

			FURL URL;
			if (bSpawnGameModeAndState)
			{
				AIU_GameMode* GM = World->SpawnActor<AIU_GameMode>();
				AIU_GameState* GS = World->SpawnActor<AIU_GameState>();
				if (GM && GS)
				{
					World->SetGameState(GS);
				}
			}

			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		void TickWorld(float TotalSeconds, float StepDelta = 1.0f / 60.0f)
		{
			const int32 Steps = FMath::Max(1, FMath::RoundToInt(TotalSeconds / StepDelta));
			for (int32 i = 0; i < Steps; ++i)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, StepDelta);
			}
		}

		~FScopedStep2NetWorld()
		{
			GWorld = PrevGWorld;
			if (World)
			{
				World->SetBegunPlay(false);
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static bool HasNetReplicatedProperty(const UStruct* StructType, const FName& PropName)
	{
		if (!StructType)
		{
			return false;
		}
		const FProperty* Prop = StructType->FindPropertyByName(PropName);
		return Prop && Prop->HasAnyPropertyFlags(CPF_Net);
	}
}

// ============================================================================
// TEST 1: Multi-Player Non-Overlapping Spawn, Co-op PvE Friendly-Fire Block & Arena PvP
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S2_GameModeMultiSpawnAndArenaPvPRulesTest,
	"InfiniteUniverse.Step2.GameModeMultiSpawnAndArenaPvPRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S2_GameModeMultiSpawnAndArenaPvPRulesTest::RunTest(const FString& Parameters)
{
	IU_S2TestHelpers::FScopedStep2NetWorld ScopeWorld(/*bSpawnGameModeAndState=*/true);
	UWorld* World = ScopeWorld.World;
	AIU_GameState* GS = World->GetGameState<AIU_GameState>();
	AIU_GameMode* GM = World->SpawnActor<AIU_GameMode>();
	TestNotNull(TEXT("AIU_GameState active in world"), GS);
	TestNotNull(TEXT("AIU_GameMode spawned"), GM);

	APlayerStart* RootStart = World->SpawnActor<APlayerStart>(FVector(0.0f, 0.0f, 110.0f), FRotator::ZeroRotator);
	TestNotNull(TEXT("PlayerStart spawned at Co-op Courtyard (0, 0, 110)"), RootStart);

	TArray<AIU_PlayerController*> Controllers;
	TArray<AIU_PlayerCharacter*> Players;
	TArray<AIU_PlayerState*> States;

	for (int32 i = 0; i < 3; ++i)
	{
		AIU_PlayerController* PC = World->SpawnActor<AIU_PlayerController>();
		PC->MarkAsLocalTestController();
		AIU_PlayerState* PS = World->SpawnActor<AIU_PlayerState>();
		PC->PlayerState = PS;

		const FTransform SpawnTM = GM->AllocateMultiplayerSpawnTransform(PC);
		AIU_PlayerCharacter* Char = World->SpawnActor<AIU_PlayerCharacter>(SpawnTM.GetLocation(), SpawnTM.GetRotation().Rotator());
		Char->SetPlayerState(PS);
		PC->Possess(Char);
		Char->GetIUMovementComponent()->GravityScale = 0.0f;

		Controllers.Add(PC);
		Players.Add(Char);
		States.Add(PS);
	}

	// 1. Verify all 3 spawned players sit >= 120 cm apart (no capsule overlap on simultaneous login!)
	TestTrue(TEXT("Player 0 and Player 1 spawn non-overlapping (> 120 cm)"),
		FVector::Dist2D(Players[0]->GetActorLocation(), Players[1]->GetActorLocation()) > 120.0f);
	TestTrue(TEXT("Player 1 and Player 2 spawn non-overlapping (> 120 cm)"),
		FVector::Dist2D(Players[1]->GetActorLocation(), Players[2]->GetActorLocation()) > 120.0f);
	TestTrue(TEXT("Player 0 and Player 2 spawn non-overlapping (> 120 cm)"),
		FVector::Dist2D(Players[0]->GetActorLocation(), Players[2]->GetActorLocation()) > 120.0f);

	// 2. Verify Co-op PvE Friendly-Fire Block at Courtyard (X ~ 0, outside West Martial Arena)
	TestFalse(TEXT("Players in Co-op Courtyard are NOT allowed to spar by default"),
		GS->ArePlayersAllowedToSpar(Players[0], Players[1]));

	FIU_DamageSpec CoopHit;
	CoopHit.Amount = 35.0f;
	CoopHit.InstigatorActor = Players[0];
	CoopHit.DamageCauser = Players[0];
	CoopHit.HitLocation = Players[1]->GetActorLocation();
	TestFalse(TEXT("Server UIU_DamageComponent blocks friendly fire in Co-op PvE sector"),
		Players[1]->GetDamageComponent()->ApplyDamageSpec(CoopHit));
	TestEqual(TEXT("Co-op teammate HP remains 100.0 after blocked friendly fire"),
		Players[1]->GetAttributeComponent()->GetHP(), 100.0f);

	// Lock-On must also skip friendly Co-op allies outside the Arena
	Players[0]->SetActorRotation((Players[1]->GetActorLocation() - Players[0]->GetActorLocation()).Rotation());
	Controllers[0]->SetControlRotation(Players[0]->GetActorRotation());
	TestFalse(TEXT("UIU_TargetingComponent refuses to lock onto Co-op ally outside Martial Arena"),
		Players[0]->GetTargetingComponent()->ToggleLockOn());

	// 3. Move Player 0 and Player 1 inside West Martial Sparring Arena (-2600, 0, 100) -> Full PvP Enabled!
	Players[0]->SetActorLocation(FVector(-2600.0f, -150.0f, 100.0f));
	Players[1]->SetActorLocation(FVector(-2600.0f, 150.0f, 100.0f));
	TestTrue(TEXT("Both players inside West Martial Arena ARE allowed to spar"),
		GS->ArePlayersAllowedToSpar(Players[0], Players[1]));

	Controllers[0]->SetControlRotation((Players[1]->GetActorLocation() - Players[0]->GetActorLocation()).Rotation());
	TestTrue(TEXT("Lock-On acquires Rival Player inside West Martial Arena"),
		Players[0]->GetTargetingComponent()->ToggleLockOn());
	TestEqual(TEXT("Locked target is Rival Player 1"),
		Players[0]->GetTargetingComponent()->GetLockedTarget(), static_cast<AActor*>(Players[1]));

	FIU_DamageSpec ArenaLethalHit;
	ArenaLethalHit.Amount = 120.0f;
	ArenaLethalHit.InstigatorActor = Players[0];
	ArenaLethalHit.DamageCauser = Players[0];
	ArenaLethalHit.HitLocation = Players[1]->GetActorLocation();
	TestTrue(TEXT("Authoritative PvP damage applies inside West Martial Arena"),
		Players[1]->GetDamageComponent()->ApplyDamageSpec(ArenaLethalHit));
	TestTrue(TEXT("Rival Player 1 defeated at 0 HP"), Players[1]->GetAttributeComponent()->IsDead());
	TestEqual(TEXT("Attacker AIU_PlayerState::PvPKills incremented to 1"), States[0]->GetPvPKills(), 1);
	TestEqual(TEXT("Defeated Rival AIU_PlayerState::Deaths incremented to 1"), States[1]->GetDeaths(), 1);

	// 4. Verify Global PvP Toggle (F4) allows PvP anywhere when enabled, and blocks again when disabled
	GS->SetAllowGlobalPvPSparring(true);
	TestTrue(TEXT("Global PvP toggle allows sparring even outside Arena (Player 0 vs Player 2)"),
		GS->ArePlayersAllowedToSpar(Players[0], Players[2]));
	GS->SetAllowGlobalPvPSparring(false);
	TestFalse(TEXT("Disabling Global PvP restores Co-op protection outside Arena"),
		GS->ArePlayersAllowedToSpar(Players[0], Players[2]));

	return true;
}

// ============================================================================
// TEST 2: FSavedMove_IUCharacter 3D Flight Bit-Packing & Server Anti-Speedhack Clamp
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S2_SavedMoveFlightFlagsAndAntiSpeedhackTest,
	"InfiniteUniverse.Step2.SavedMoveFlightFlagsAndAntiSpeedhack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S2_SavedMoveFlightFlagsAndAntiSpeedhackTest::RunTest(const FString& Parameters)
{
	IU_S2TestHelpers::FScopedStep2NetWorld ScopeWorld(/*bSpawnGameModeAndState=*/false);
	UWorld* World = ScopeWorld.World;

	AIU_PlayerCharacter* ClientChar = World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0f, 0.0f, 300.0f), FRotator::ZeroRotator);
	AIU_PlayerCharacter* ServerChar = World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0f, 500.0f, 300.0f), FRotator::ZeroRotator);
	AIU_PlayerController* ServerPC = World->SpawnActor<AIU_PlayerController>();
	ServerPC->MarkAsLocalTestController();
	ServerPC->Possess(ServerChar);

	UIU_MovementComponent* ClientMove = ClientChar->GetIUMovementComponent();
	UIU_FlightComponent* ClientFlight = ClientChar->GetFlightComponent();
	UIU_MovementComponent* ServerMove = ServerChar->GetIUMovementComponent();
	UIU_FlightComponent* ServerFlight = ServerChar->GetFlightComponent();

	FNetworkPredictionData_Client* PredData = ClientMove->GetPredictionData_Client();
	TestNotNull(TEXT("UIU_MovementComponent::GetPredictionData_Client allocated FNetworkPredictionData_Client_IUCharacter"), PredData);

	// Put ClientChar into Free 3D Flight + Boost + Ascend
	ClientFlight->EnterFlight();
	ClientFlight->SetFlightDashActive(true);
	ClientFlight->HandleJumpOrAscendStarted();
	ClientMove->SetSprinting(true);

	FSavedMove_IUCharacter SavedFlightMove;
	SavedFlightMove.SetMoveFor(ClientChar, 1.0f / 60.0f, FVector(1.0f, 0.0f, 0.4f), *static_cast<FNetworkPredictionData_Client_Character*>(PredData));
	const uint8 PackedFlags = SavedFlightMove.GetCompressedFlags();

	TestTrue(TEXT("FLAG_Custom_0 (Sprint/FlightDash) packed"), (PackedFlags & FSavedMove_Character::FLAG_Custom_0) != 0);
	TestTrue(TEXT("FLAG_Custom_1 (CMOVE_FreeFlight) packed"), (PackedFlags & FSavedMove_Character::FLAG_Custom_1) != 0);
	TestTrue(TEXT("FLAG_Custom_2 (Ascend) packed"), (PackedFlags & FSavedMove_Character::FLAG_Custom_2) != 0);
	TestFalse(TEXT("FLAG_Custom_3 (Descend) cleared"), (PackedFlags & FSavedMove_Character::FLAG_Custom_3) != 0);

	// Apply compressed flags directly to ServerChar via UpdateFromCompressedFlags (simulating ServerMove receipt!)
	struct FServerFlagUnpacker : public UIU_MovementComponent
	{
		static void Unpack(UIU_MovementComponent* Target, uint8 Flags)
		{
			static_cast<FServerFlagUnpacker*>(Target)->UpdateFromCompressedFlags(Flags);
		}
	};

	FServerFlagUnpacker::Unpack(ServerMove, PackedFlags);
	TestTrue(TEXT("ServerChar entered CMOVE_FreeFlight via compressed FSavedMove flags"), ServerMove->IsInCustomFreeFlight());
	TestTrue(TEXT("ServerChar activated FlightComponent::IsFlying()"), ServerFlight->IsFlying());
	TestTrue(TEXT("ServerChar unpacked AscendHeld == true"), ServerFlight->IsAscendHeld());
	TestFalse(TEXT("ServerChar unpacked DescendHeld == false"), ServerFlight->IsDescendHeld());

	// Test Anti-Speedhack Envelope Clamp on Server PhysCustom: inject spoofed 99,999 cm/s velocity!
	ServerMove->bRunPhysicsWithNoController = true;
	ServerMove->Velocity = FVector(99999.0f, 0.0f, 0.0f);
	ServerMove->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);

	const float MaxLegalEnvelope = (ServerFlight->FlightSpeed * ServerFlight->FlightDashMultiplier) + ServerFlight->VerticalFlightSpeed + 400.0f;
	TestTrue(TEXT("Server clamped spoofed 99,999 cm/s flight velocity within legal dash speed envelope"),
		ServerMove->Velocity.Size() <= MaxLegalEnvelope);

	// Now clear flight flag -> verify ServerChar exits CMOVE_FreeFlight cleanly!
	FServerFlagUnpacker::Unpack(ServerMove, 0);
	TestFalse(TEXT("Clearing FLAG_Custom_1 exits CMOVE_FreeFlight on Server"), ServerMove->IsInCustomFreeFlight());

	return true;
}

// ============================================================================
// TEST 3: Server-Authoritative Multi-Player Enemy AI Closest Aggro & Failover
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S2_MultiPlayerEnemyAggroSwitchingTest,
	"InfiniteUniverse.Step2.MultiPlayerEnemyAggroAndFailover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S2_MultiPlayerEnemyAggroSwitchingTest::RunTest(const FString& Parameters)
{
	IU_S2TestHelpers::FScopedStep2NetWorld ScopeWorld(/*bSpawnGameModeAndState=*/true);
	UWorld* World = ScopeWorld.World;

	// Spawn PlayerFar FIRST (so PlayerController[0] is farther away at 950 cm!)
	AIU_PlayerController* PCFar = World->SpawnActor<AIU_PlayerController>();
	AIU_PlayerCharacter* PlayerFar = World->SpawnActor<AIU_PlayerCharacter>(FVector(950.0f, 0.0f, 95.0f), FRotator::ZeroRotator);
	PCFar->Possess(PlayerFar);
	PlayerFar->GetIUMovementComponent()->GravityScale = 0.0f;

	// Spawn PlayerNear SECOND (at 380 cm from Enemy!)
	AIU_PlayerController* PCNear = World->SpawnActor<AIU_PlayerController>();
	AIU_PlayerCharacter* PlayerNear = World->SpawnActor<AIU_PlayerCharacter>(FVector(380.0f, 0.0f, 95.0f), FRotator::ZeroRotator);
	PCNear->Possess(PlayerNear);
	PlayerNear->GetIUMovementComponent()->GravityScale = 0.0f;

	AIU_EnemyCharacter* Enemy = World->SpawnActor<AIU_EnemyCharacter>(FVector(0.0f, 0.0f, 95.0f), FRotator::ZeroRotator);
	Enemy->GetCharacterMovement()->GravityScale = 0.0f;
	AIU_EnemyAIController* EnemyAI = Enemy->GetEnemyAIController();
	TestNotNull(TEXT("Enemy possessed by AIU_EnemyAIController on Authority"), EnemyAI);

	ScopeWorld.TickWorld(0.35f);
	TestEqual(TEXT("Multi-player AI acquired NEAREST player (PlayerNear, not PlayerController[0])"),
		EnemyAI->GetTargetActor(), static_cast<AActor*>(PlayerNear));
	TestTrue(TEXT("Enemy ReplicatedAIState updated out of Idle"),
		Enemy->GetEnemyAIState() == EIU_EnemyAIState::Chase || Enemy->GetEnemyAIState() == EIU_EnemyAIState::Detect);

	// Defeat PlayerNear -> verify Enemy AI seamlessly switches aggro to surviving PlayerFar without dropping to Idle!
	PlayerNear->GetAttributeComponent()->ModifyHP(-999.0f);
	TestTrue(TEXT("PlayerNear is dead"), PlayerNear->GetAttributeComponent()->IsDead());

	ScopeWorld.TickWorld(0.15f);
	TestEqual(TEXT("Enemy AI seamlessly switched aggro to surviving Co-op teammate PlayerFar"),
		EnemyAI->GetTargetActor(), static_cast<AActor*>(PlayerFar));
	TestEqual(TEXT("Enemy remained in Chase state pursuing PlayerFar"),
		Enemy->GetEnemyAIState(), EIU_EnemyAIState::Chase);

	return true;
}

// ============================================================================
// TEST 4: Replication Descriptors Audit & Ping-Compensated 2-Player PvP Mirage Evade
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S2_ReplicationAndPingCompensatedEvadeTest,
	"InfiniteUniverse.Step2.ReplicationDescriptorsAndPingCompensatedPvPEvade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S2_ReplicationAndPingCompensatedEvadeTest::RunTest(const FString& Parameters)
{
	using namespace IU_S2TestHelpers;

	// 1. Audit CDO Replication Flags & CPF_Net Properties across all Step 2 Gameplay Classes
	TestTrue(TEXT("AIU_PlayerCharacter replicates"), GetDefault<AIU_PlayerCharacter>()->GetIsReplicated());
	TestTrue(TEXT("AIU_EnemyCharacter replicates"), GetDefault<AIU_EnemyCharacter>()->GetIsReplicated());
	TestTrue(TEXT("AIU_KiProjectile replicates"), GetDefault<AIU_KiProjectile>()->GetIsReplicated());
	TestFalse(TEXT("AIU_EnemySpawner does NOT replicate to clients"), GetDefault<AIU_EnemySpawner>()->GetIsReplicated());
	TestFalse(TEXT("AIU_EnemySpawner bNetLoadOnClient is false"), GetDefault<AIU_EnemySpawner>()->bNetLoadOnClient);

	TestTrue(TEXT("AIU_GameState::bAllowGlobalPvPSparring is CPF_Net"),
		HasNetReplicatedProperty(AIU_GameState::StaticClass(), TEXT("bAllowGlobalPvPSparring")));
	TestTrue(TEXT("AIU_PlayerState::EnemyKills is CPF_Net"),
		HasNetReplicatedProperty(AIU_PlayerState::StaticClass(), TEXT("EnemyKills")));
	TestTrue(TEXT("AIU_PlayerState::PvPKills is CPF_Net"),
		HasNetReplicatedProperty(AIU_PlayerState::StaticClass(), TEXT("PvPKills")));
	TestTrue(TEXT("UIU_AttributeComponent::CurrentHP is CPF_Net"),
		HasNetReplicatedProperty(UIU_AttributeComponent::StaticClass(), TEXT("CurrentHP")));
	TestTrue(TEXT("UIU_AttributeComponent::CurrentKi is CPF_Net"),
		HasNetReplicatedProperty(UIU_AttributeComponent::StaticClass(), TEXT("CurrentKi")));
	TestTrue(TEXT("UIU_AttributeComponent::bIsDead is CPF_Net"),
		HasNetReplicatedProperty(UIU_AttributeComponent::StaticClass(), TEXT("bIsDead")));
	TestTrue(TEXT("UIU_CombatComponent::CurrentCombatState is CPF_Net"),
		HasNetReplicatedProperty(UIU_CombatComponent::StaticClass(), TEXT("CurrentCombatState")));
	TestTrue(TEXT("UIU_EnemyCharacter::bWindingUpAttack is CPF_Net"),
		HasNetReplicatedProperty(AIU_EnemyCharacter::StaticClass(), TEXT("bWindingUpAttack")));
	TestTrue(TEXT("AIU_KiProjectile::ReplicatedLaunchVelocity is CPF_Net"),
		HasNetReplicatedProperty(AIU_KiProjectile::StaticClass(), TEXT("ReplicatedLaunchVelocity")));

	// 2. Verify Ping-Compensated 2-Player PvP Mirage Perfect Evade inside West Martial Arena!
	FScopedStep2NetWorld ScopeWorld(/*bSpawnGameModeAndState=*/true);
	UWorld* World = ScopeWorld.World;

	AIU_PlayerCharacter* Attacker = World->SpawnActor<AIU_PlayerCharacter>(FVector(-2600.0f, 0.0f, 100.0f), FRotator::ZeroRotator);
	AIU_PlayerCharacter* Defender = World->SpawnActor<AIU_PlayerCharacter>(FVector(-2440.0f, 0.0f, 100.0f), FRotator(0.0f, 180.0f, 0.0f));
	AIU_PlayerState* DefenderPS = World->SpawnActor<AIU_PlayerState>();
	DefenderPS->ExactPing = 180.0f; // Simulate 180 ms WAN RTT -> +90 ms server ping compensation!
	DefenderPS->SetCompressedPing(45);
	Defender->SetPlayerState(DefenderPS);

	Attacker->GetIUMovementComponent()->GravityScale = 0.0f;
	Defender->GetIUMovementComponent()->GravityScale = 0.0f;

	// Register a PvP melee threat that is 0.36s from impact (> 0.30s base window, <= 0.39s with 90 ms half-RTT compensation!)
	UIU_CombatThreatSubsystem* ThreatSubsys = World->GetSubsystem<UIU_CombatThreatSubsystem>();
	TestNotNull(TEXT("UIU_CombatThreatSubsystem active"), ThreatSubsys);
	ThreatSubsys->RegisterMeleeAttackWindup(
		Attacker,
		Attacker->GetActorLocation() + FVector(45.0f, 0.0f, 0.0f),
		Defender->GetActorLocation(),
		80.0f,
		0.36f);

	TestTrue(TEXT("Defender RequestDodge succeeded under 180 ms simulated WAN RTT"),
		Defender->GetCombatComponent()->RequestDodge(FVector(0.0f, 1.0f, 0.0f)));
	TestTrue(TEXT("Bounded Server Ping Compensation upgraded PvP dodge to Mirage Perfect Evade"),
		Defender->GetCombatComponent()->IsLastDodgeCombatEvade());
	TestNotNull(TEXT("UPoseableMeshComponent AIU_AfterimageGhost spawned at Origin A"),
		Defender->GetCombatComponent()->GetLastSpawnedAfterimage());

	return true;
}

// ============================================================================
// TEST 5 (AUDIT): Simultaneous 4-Client Attacks on 1 Enemy — Single Death & Single Respawn
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S2Audit_SimultaneousFourPlayerEnemyAttackTest,
	"InfiniteUniverse.Step2Audit.SimultaneousFourPlayerEnemyAttackSingleDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S2Audit_SimultaneousFourPlayerEnemyAttackTest::RunTest(const FString& Parameters)
{
	IU_S2TestHelpers::FScopedStep2NetWorld ScopeWorld(/*bSpawnGameModeAndState=*/true);
	UWorld* World = ScopeWorld.World;

	AIU_EnemySpawner* Spawner = World->SpawnActor<AIU_EnemySpawner>(FVector(-1850.0f, 0.0f, 98.0f), FRotator::ZeroRotator);
	Spawner->RespawnDelaySeconds = 0.50f;
	AIU_EnemyCharacter* Enemy = Spawner->RespawnEnemyNow();
	TestNotNull(TEXT("Enemy spawned by Server AIU_EnemySpawner"), Enemy);
	Enemy->GetAttributeComponent()->MaxHP = 100.0f;
	Enemy->GetAttributeComponent()->ResetAttributesToFull();

	TArray<AIU_PlayerCharacter*> Raiders;
	TArray<AIU_PlayerState*> RaiderStates;
	const FVector Offsets[4] = {
		FVector(140.0f, 0.0f, 0.0f),
		FVector(-140.0f, 0.0f, 0.0f),
		FVector(0.0f, 140.0f, 0.0f),
		FVector(0.0f, -140.0f, 0.0f)
	};

	for (int32 i = 0; i < 4; ++i)
	{
		AIU_PlayerCharacter* P = World->SpawnActor<AIU_PlayerCharacter>(Enemy->GetActorLocation() + Offsets[i], (-Offsets[i]).Rotation());
		AIU_PlayerState* PS = World->SpawnActor<AIU_PlayerState>();
		P->SetPlayerState(PS);
		Raiders.Add(P);
		RaiderStates.Add(PS);
	}

	// All 4 players apply 35 damage on the EXACT SAME SERVER FRAME (4 * 35 = 140 > 100 MaxHP!)
	int32 AcceptedHits = 0;
	for (int32 i = 0; i < 4; ++i)
	{
		FIU_DamageSpec Hit;
		Hit.Amount = 35.0f;
		Hit.InstigatorActor = Raiders[i];
		Hit.DamageCauser = Raiders[i];
		Hit.HitLocation = Enemy->GetActorLocation();
		Hit.HitDirection = (-Offsets[i]).GetSafeNormal();
		if (Enemy->GetDamageComponent()->ApplyDamageSpec(Hit))
		{
			++AcceptedHits;
		}
	}

	// Exactly 3 hits (35 + 35 + 30 = 100) bring Enemy to 0 HP; the 4th simultaneous hit MUST be rejected cleanly!
	TestEqual(TEXT("Exactly 3 simultaneous hits applied before 0 HP death gate closed"), AcceptedHits, 3);
	TestEqual(TEXT("Enemy HP clamped to exact 0.0 (never negative)"), Enemy->GetAttributeComponent()->GetHP(), 0.0f);
	TestTrue(TEXT("Enemy transitioned to Dead state"), Enemy->IsDead());

	int32 TotalAwardedKills = 0;
	for (const AIU_PlayerState* PS : RaiderStates)
	{
		TotalAwardedKills += PS->GetEnemyKills();
	}
	TestEqual(TEXT("Exactly 1 finishing blow EnemyKill awarded across all 4 simultaneous attackers (no double-kill race)"),
		TotalAwardedKills, 1);

	// Tick past 0.50s respawn delay -> verify single clean respawn to 100 HP
	ScopeWorld.TickWorld(0.65f);
	TestFalse(TEXT("Enemy respawned cleanly after simultaneous 4-player takedown"), Enemy->IsDead());
	TestEqual(TEXT("Respawned Enemy restored to full 100 HP"), Enemy->GetAttributeComponent()->GetHP(), 100.0f);

	return true;
}

// ============================================================================
// TEST 6 (AUDIT): Invalid Damage Requests, NaN/Inf RPC Payloads & Exploit Validation
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_S2Audit_InvalidDamageAndExploitRejectionTest,
	"InfiniteUniverse.Step2Audit.InvalidDamageAndClientExploitRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_S2Audit_InvalidDamageAndExploitRejectionTest::RunTest(const FString& Parameters)
{
	IU_S2TestHelpers::FScopedStep2NetWorld ScopeWorld(/*bSpawnGameModeAndState=*/true);
	UWorld* World = ScopeWorld.World;

	AIU_PlayerCharacter* Attacker = World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0f, 0.0f, 110.0f), FRotator::ZeroRotator);
	AIU_PlayerCharacter* Victim = World->SpawnActor<AIU_PlayerCharacter>(FVector(200.0f, 0.0f, 110.0f), FRotator::ZeroRotator);
	AIU_EnemyCharacter* FarEnemy = World->SpawnActor<AIU_EnemyCharacter>(FVector(9500.0f, 0.0f, 110.0f), FRotator::ZeroRotator);

	Victim->GetAttributeComponent()->ModifyHP(-20.0f); // Victim at 80/100 HP
	TestEqual(TEXT("Victim baseline HP is 80"), Victim->GetAttributeComponent()->GetHP(), 80.0f);

	// 1. Negative damage exploit (attempting to heal via negative damage spec)
	FIU_DamageSpec NegSpec;
	NegSpec.Amount = -500.0f;
	NegSpec.InstigatorActor = FarEnemy;
	TestFalse(TEXT("Negative damage spec rejected"), Victim->GetDamageComponent()->ApplyDamageSpec(NegSpec));
	TestEqual(TEXT("Victim HP unchanged after negative damage attempt"), Victim->GetAttributeComponent()->GetHP(), 80.0f);

	// 2. NaN and +Infinity damage exploit
	FIU_DamageSpec NaNSpec;
	NaNSpec.Amount = std::numeric_limits<float>::quiet_NaN();
	NaNSpec.InstigatorActor = FarEnemy;
	TestFalse(TEXT("NaN damage spec rejected"), Victim->GetDamageComponent()->ApplyDamageSpec(NaNSpec));

	FIU_DamageSpec InfSpec;
	InfSpec.Amount = std::numeric_limits<float>::infinity();
	InfSpec.InstigatorActor = FarEnemy;
	TestFalse(TEXT("+Infinity damage spec rejected"), Victim->GetDamageComponent()->ApplyDamageSpec(InfSpec));
	TestEqual(TEXT("Victim HP still finite at 80.0"), Victim->GetAttributeComponent()->GetHP(), 80.0f);

	// 3. Self-inflicted damage exploit
	FIU_DamageSpec SelfSpec;
	SelfSpec.Amount = 25.0f;
	SelfSpec.InstigatorActor = Victim;
	TestFalse(TEXT("Self-damage spec rejected"), Victim->GetDamageComponent()->ApplyDamageSpec(SelfSpec));

	// 4. Co-op friendly-fire damage outside Martial Arena
	FIU_DamageSpec FriendlyFireSpec;
	FriendlyFireSpec.Amount = 50.0f;
	FriendlyFireSpec.InstigatorActor = Attacker;
	TestFalse(TEXT("Unauthorized PvP damage in Co-op zone rejected"), Victim->GetDamageComponent()->ApplyDamageSpec(FriendlyFireSpec));
	TestEqual(TEXT("Victim HP unchanged at 80.0"), Victim->GetAttributeComponent()->GetHP(), 80.0f);

	// 5. Out-of-range Lock-On spoof (9,500 cm > MaxAcquisitionRange)
	TestFalse(TEXT("Out-of-range lock-on spoof rejected"), Attacker->GetTargetingComponent()->SetLockedTarget(FarEnemy));
	TestNull(TEXT("LockedTarget remains nullptr"), Attacker->GetTargetingComponent()->GetLockedTarget());

	// 6. Malicious NaN vector Dodge & KiBlast requests must not corrupt Actor transform with NaN!
	const FVector BadNaNVec(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 0.0f);
	Attacker->GetCombatComponent()->RequestDodge(BadNaNVec);
	TestFalse(TEXT("Actor location remains NaN-free after malformed NaN/Inf Dodge vector"),
		Attacker->GetActorLocation().ContainsNaN());

	// 7. Zero-Ki KiBlast spam exploit
	Attacker->GetCombatComponent()->ResetCombatState();
	Attacker->GetAttributeComponent()->TryConsumeKi(100.0f);
	TestEqual(TEXT("Attacker Ki drained to 0"), Attacker->GetAttributeComponent()->GetKi(), 0.0f);
	TestNull(TEXT("Server rejects KiBlast when Ki == 0"), Attacker->GetCombatComponent()->RequestKiBlast());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
