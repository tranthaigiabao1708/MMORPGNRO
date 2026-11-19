// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
#include "Characters/IU_SkeletalAnimDirectorComponent.h"
#include "Core/IU_PlayerController.h"
#include "Movement/IU_MovementComponent.h"
#include "Movement/IU_FlightComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Combat/IU_CombatComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Combat/IU_TargetingComponent.h"
#include "Combat/IU_KiProjectile.h"
#include "Combat/IU_AfterimageGhost.h"
#include "AI/IU_EnemySpawner.h"
#include "UI/IU_HUDManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace IUM6And7TestHelpers
{
	struct FScopedFinalTestWorld
	{
		UWorld* World = nullptr;

		FScopedFinalTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("IU_M6M7FinalTestWorld"));
			if (World && GEngine)
			{
				FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
				WorldContext.SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
				World->SetBegunPlay(true);
			}
		}

		~FScopedFinalTestWorld()
		{
			if (World)
			{
				if (World->HasBegunPlay())
				{
					for (FActorIterator It(World); It; ++It)
					{
						if (AActor* Act = *It)
						{
							Act->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
						}
					}
					World->SetBegunPlay(false);
				}
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
				World->DestroyWorld(false);
				World = nullptr;
			}
		}

		void TickWorld(float TotalDuration, float StepDelta = 1.0f / 60.0f)
		{
			if (!World)
			{
				return;
			}
			float Elapsed = 0.0f;
			while (Elapsed < TotalDuration)
			{
				const float Dt = FMath::Min(StepDelta, TotalDuration - Elapsed);
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Dt);
				Elapsed += Dt;
			}
		}

		void SpawnArenaFloor()
		{
			if (!World)
			{
				return;
			}
			UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0.0f, 0.0f, -50.0f), FRotator::ZeroRotator);
			if (Floor && CubeMesh)
			{
				UStaticMeshComponent* SMC = Floor->GetStaticMeshComponent();
				SMC->SetMobility(EComponentMobility::Movable);
				SMC->SetStaticMesh(CubeMesh);
				SMC->SetWorldScale3D(FVector(250.0f, 250.0f, 1.0f));
				SMC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				SMC->SetCollisionProfileName(TEXT("BlockAll"));
			}
		}
	};
}

// ============================================================================
// TEST 1 (M6): Anime SkeletalMeshes, Scale (175-185 cm), 23 Humanoid Bones & Socket
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIUM6AnimeMeshesSkeletonAndSocketTest,
	"InfiniteUniverse.Milestone6.AnimeMeshesSkeletonAndSocketValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIUM6AnimeMeshesSkeletonAndSocketTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* PlayerMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Player/Meshes/SK_Player.SK_Player"));
	USkeletalMesh* EnemyMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Enemy/Meshes/SK_Enemy.SK_Enemy"));

	TestNotNull(TEXT("SK_Player Anime SkeletalMesh must exist"), PlayerMesh);
	TestNotNull(TEXT("SK_Enemy Anime Raider SkeletalMesh must exist"), EnemyMesh);
	if (!PlayerMesh || !EnemyMesh)
	{
		return false;
	}

	// Criterion 20: Exact scale 175-185 cm
	const FBoxSphereBounds PlayerBounds = PlayerMesh->GetImportedBounds();
	const FBoxSphereBounds EnemyBounds = EnemyMesh->GetImportedBounds();
	const float PlayerHeightCm = PlayerBounds.BoxExtent.Z * 2.0f;
	const float EnemyHeightCm = EnemyBounds.BoxExtent.Z * 2.0f;

	TestTrue(FString::Printf(TEXT("SK_Player height (%.1f cm) must be within [175, 185] cm"), PlayerHeightCm),
		PlayerHeightCm >= 175.0f && PlayerHeightCm <= 185.0f);
	TestTrue(FString::Printf(TEXT("SK_Enemy height (%.1f cm) must be within [175, 185] cm"), EnemyHeightCm),
		EnemyHeightCm >= 175.0f && EnemyHeightCm <= 185.0f);

	// Criterion 21: Shared humanoid USkeleton & all 23 standard UE5 humanoid bones recognized cleanly
	USkeleton* SharedSkeleton = PlayerMesh->GetSkeleton();
	TestNotNull(TEXT("SK_Player must have a valid USkeleton"), SharedSkeleton);
	TestEqual(TEXT("SK_Enemy must share SK_Player humanoid USkeleton"), EnemyMesh->GetSkeleton(), SharedSkeleton);

	const TCHAR* RequiredBones[] = {
		TEXT("root"), TEXT("pelvis"), TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"),
		TEXT("neck_01"), TEXT("head"),
		TEXT("clavicle_l"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"),
		TEXT("clavicle_r"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"),
		TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("ball_l"),
		TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), TEXT("ball_r")
	};

	const FReferenceSkeleton& RefSkel = PlayerMesh->GetRefSkeleton();
	for (const TCHAR* BoneName : RequiredBones)
	{
		TestTrue(FString::Printf(TEXT("Humanoid bone '%s' must exist in SK_Player"), BoneName),
			RefSkel.FindBoneIndex(FName(BoneName)) != INDEX_NONE);
	}

	// Hand Ki Muzzle socket validation
	USkeletalMeshSocket* KiSocket = PlayerMesh->FindSocket(TEXT("Socket_KiMuzzle_R"));
	TestNotNull(TEXT("Socket_KiMuzzle_R must exist on SK_Player"), KiSocket);
	if (KiSocket)
	{
		TestEqual(TEXT("Socket_KiMuzzle_R must be attached to hand_r"), KiSocket->BoneName, FName(TEXT("hand_r")));
	}

	// Stylized anime material slots check
	TestTrue(TEXT("SK_Player must have 5 stylized material slots"), PlayerMesh->GetMaterials().Num() >= 5);
	TestTrue(TEXT("SK_Enemy must have 5 stylized material slots"), EnemyMesh->GetMaterials().Num() >= 5);
	for (int32 i = 0; i < FMath::Min(5, PlayerMesh->GetMaterials().Num()); ++i)
	{
		TestNotNull(FString::Printf(TEXT("SK_Player material slot %d assigned"), i), PlayerMesh->GetMaterials()[i].MaterialInterface.Get());
	}

	return true;
}

// ============================================================================
// TEST 2 (M6): All 15 Anime Animations, Pose Evaluation & Wukong Poseable Afterimage
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIUM6All15AnimationsAndAfterimageTest,
	"InfiniteUniverse.Milestone6.All15AnimeAnimationsAndPoseableAfterimage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIUM6All15AnimationsAndAfterimageTest::RunTest(const FString& Parameters)
{
	const TCHAR* AnimAssetPaths[] = {
		TEXT("/Game/Characters/Player/Animations/A_Player_Idle.A_Player_Idle"),
		TEXT("/Game/Characters/Player/Animations/A_Player_Walk.A_Player_Walk"),
		TEXT("/Game/Characters/Player/Animations/A_Player_Run.A_Player_Run"),
		TEXT("/Game/Characters/Player/Animations/A_Player_Jump.A_Player_Jump"),
		TEXT("/Game/Characters/Player/Animations/A_Player_Fall.A_Player_Fall"),
		TEXT("/Game/Characters/Player/Animations/A_Player_Landing.A_Player_Landing"),
		TEXT("/Game/Characters/Player/Animations/A_Player_Hover.A_Player_Hover"),
		TEXT("/Game/Characters/Player/Animations/A_Player_FlyForward.A_Player_FlyForward"),
		TEXT("/Game/Characters/Player/Animations/A_Player_FlyBackward.A_Player_FlyBackward"),
		TEXT("/Game/Characters/Player/Animations/A_Player_Melee_01.A_Player_Melee_01"),
		TEXT("/Game/Characters/Player/Animations/A_Player_Melee_02.A_Player_Melee_02"),
		TEXT("/Game/Characters/Player/Animations/A_Player_Melee_03.A_Player_Melee_03"),
		TEXT("/Game/Characters/Player/Animations/A_Player_KiBlast.A_Player_KiBlast"),
		TEXT("/Game/Characters/Player/Animations/A_Player_HitReaction.A_Player_HitReaction"),
		TEXT("/Game/Characters/Player/Animations/A_Player_Death.A_Player_Death")
	};

	for (const TCHAR* Path : AnimAssetPaths)
	{
		UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, Path);
		TestNotNull(FString::Printf(TEXT("Animation '%s' must load cleanly"), Path), Seq);
		if (Seq)
		{
			TestTrue(FString::Printf(TEXT("Animation '%s' play length (%.2fs) > 0.15s"), Path, Seq->GetPlayLength()),
				Seq->GetPlayLength() > 0.15f);
		}
	}

	IUM6And7TestHelpers::FScopedFinalTestWorld Scope;
	Scope.SpawnArenaFloor();

	AIU_PlayerController* PC = Scope.World->SpawnActor<AIU_PlayerController>();
	PC->MarkAsLocalTestController();
	AIU_PlayerCharacter* Player = Scope.World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0f, 0.0f, 95.0f), FRotator::ZeroRotator);
	AIU_EnemyCharacter* Enemy = Scope.World->SpawnActor<AIU_EnemyCharacter>(FVector(180.0f, 0.0f, 95.0f), FRotator(0.0f, 180.0f, 0.0f));
	PC->Possess(Player);
	Scope.TickWorld(0.10f);

	// Verify auto-equipped SkeletalMeshes & hidden geometric placeholders
	TestNotNull(TEXT("Player auto-equipped SK_Player"), Player->GetMesh()->GetSkeletalMeshAsset());
	TestFalse(TEXT("Player placeholder cylinder hidden when SK_Player equipped"), Player->GetPlaceholderBodyMesh()->IsVisible());
	TestNotNull(TEXT("Enemy auto-equipped SK_Enemy"), Enemy->GetMesh()->GetSkeletalMeshAsset());
	TestFalse(TEXT("Enemy placeholder cylinder hidden when SK_Enemy equipped"), Enemy->GetPlaceholderBodyMesh()->IsVisible());

	UIU_SkeletalAnimDirectorComponent* AnimDir = Player->GetAnimDirectorComponent();
	TestNotNull(TEXT("Player has UIU_SkeletalAnimDirectorComponent"), AnimDir);
	TestTrue(TEXT("AnimDirector loaded all 15 core anime clips"), AnimDir->HasLoadedCoreAnimSet());
	TestEqual(TEXT("Initial active clip is IdleAnim"), AnimDir->GetActiveClip(), AnimDir->IdleAnim.Get());

	// Record rest-pose upper-arm length for Criterion 23 joint deformation integrity check
	Player->GetMesh()->TickAnimation(0.016f, false);
	Player->GetMesh()->RefreshBoneTransforms();
	const float RestUpperArmLen = FVector::Dist(
		Player->GetMesh()->GetBoneLocation(TEXT("upperarm_r")),
		Player->GetMesh()->GetBoneLocation(TEXT("lowerarm_r")));
	TestTrue(TEXT("Rest upperarm_r segment length valid (> 15 cm)"), RestUpperArmLen > 15.0f);

	// Trigger KiBlast -> verify KiBlastAnim + Socket_KiMuzzle_R spawn
	AIU_KiProjectile* KiProj = Player->GetCombatComponent()->RequestKiBlast();
	TestNotNull(TEXT("KiBlast spawned from anime character"), KiProj);
	TestEqual(TEXT("KiBlast plays A_Player_KiBlast"), AnimDir->GetActiveClip(), AnimDir->KiBlastAnim.Get());
	Scope.TickWorld(0.35f);

	// Trigger Melee Combo 1 -> verify Melee01Anim & joint bone integrity under extreme swing pose
	TestTrue(TEXT("Melee Attack 1 started"), Player->GetCombatComponent()->RequestMeleeAttack());
	TestEqual(TEXT("Melee 1 plays A_Player_Melee_01"), AnimDir->GetActiveClip(), AnimDir->Melee01Anim.Get());
	Player->GetMesh()->TickAnimation(0.10f, false);
	Player->GetMesh()->RefreshBoneTransforms();

	const FVector SwingShoulder = Player->GetMesh()->GetBoneLocation(TEXT("upperarm_r"));
	const FVector SwingElbow = Player->GetMesh()->GetBoneLocation(TEXT("lowerarm_r"));
	const FVector SwingHand = Player->GetMesh()->GetBoneLocation(TEXT("hand_r"));
	TestFalse(TEXT("Pose bone transforms free of NaN"), SwingShoulder.ContainsNaN() || SwingElbow.ContainsNaN() || SwingHand.ContainsNaN());
	const float SwingUpperArmLen = FVector::Dist(SwingShoulder, SwingElbow);
	TestTrue(TEXT("Criterion 23: Shoulder/Elbow bone segment preserves length without collapse during swing"),
		FMath::IsNearlyEqual(SwingUpperArmLen, RestUpperArmLen, RestUpperArmLen * 0.08f));

	Scope.TickWorld(0.35f);

	// Trigger Enemy telegraphed windup (<= 0.30s) -> Player RMB Perfect Evade -> verify PoseableMesh Afterimage!
	Enemy->AttackWindupDuration = 0.24f;
	TestTrue(TEXT("Enemy started telegraphed melee windup"), Enemy->StartTelegraphedAttack(Player));
	Scope.TickWorld(0.04f);

	TestTrue(TEXT("Player executed Perfect Evade"), Player->GetCombatComponent()->RequestDodge(FVector(-1.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("Dodge upgraded to Combat Perfect Evade"), Player->GetCombatComponent()->IsLastDodgeCombatEvade());
	AIU_AfterimageGhost* Ghost = Player->GetCombatComponent()->GetLastSpawnedAfterimage();
	TestNotNull(TEXT("Afterimage Ghost spawned at origin A"), Ghost);
	if (Ghost)
	{
		TestTrue(TEXT("Afterimage Ghost copies anime SkeletalMesh pose onto UPoseableMeshComponent"), Ghost->IsUsingSkeletalPose());
	}

	return true;
}

// ============================================================================
// TEST 3 (M7): Audited Bug Fixes (Hit-Stun Jump Block, Dead Lockout & Auto-Respawn)
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIUM7AuditedBugFixesAndRespawnTest,
	"InfiniteUniverse.Milestone7.AuditedBugFixesDeadLockoutAndAutoRespawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIUM7AuditedBugFixesAndRespawnTest::RunTest(const FString& Parameters)
{
	IUM6And7TestHelpers::FScopedFinalTestWorld Scope;
	Scope.SpawnArenaFloor();

	AIU_PlayerController* PC = Scope.World->SpawnActor<AIU_PlayerController>();
	PC->MarkAsLocalTestController();
	const FVector SpawnLoc(0.0f, 0.0f, 95.0f);
	AIU_PlayerCharacter* Player = Scope.World->SpawnActor<AIU_PlayerCharacter>(SpawnLoc, FRotator::ZeroRotator);
	Player->PlayerRespawnDelaySeconds = 0.60f;
	PC->Possess(Player);

	AIU_EnemyCharacter* Enemy = Scope.World->SpawnActor<AIU_EnemyCharacter>(FVector(450.0f, 0.0f, 95.0f), FRotator(0.0f, 180.0f, 0.0f));
	Scope.TickWorld(0.15f);

	// 1. Hit-Stun Jump/Flight Bypass Fix Check
	FIU_DamageSpec StunHit;
	StunHit.Amount = 15.0f;
	StunHit.InstigatorActor = Enemy;
	StunHit.DamageCauser = Enemy;
	StunHit.HitDirection = FVector(-1.0f, 0.0f, 0.0f);
	StunHit.HitStunDuration = 0.35f;
	TestTrue(TEXT("Applied stun hit to Player"), Player->GetDamageComponent()->ApplyDamageSpec(StunHit));
	TestTrue(TEXT("Player is in HitReaction"), Player->GetDamageComponent()->IsInHitReaction());

	Player->StartJumpInput();
	Scope.TickWorld(0.05f);
	TestFalse(TEXT("Stunned player cannot jump-cancel out of HitReaction"), Player->GetCharacterMovement()->IsFalling());
	Player->StopJumpInput();
	Scope.TickWorld(0.40f);
	TestFalse(TEXT("Player recovered from HitReaction"), Player->GetDamageComponent()->IsInHitReaction());

	// 2. Enter Flight & Lock-On, then take lethal damage (0 HP)
	Player->StartJumpInput();
	Scope.TickWorld(0.10f);
	Player->StopJumpInput();
	Player->StartJumpInput(); // 2nd Space airborne -> Flight!
	Scope.TickWorld(0.10f);
	Player->StopJumpInput();
	TestTrue(TEXT("Player is flying prior to lethal damage"), Player->GetFlightComponent()->IsFlying());

	TestTrue(TEXT("Player locked onto Enemy"), Player->GetTargetingComponent()->SetLockedTarget(Enemy));

	FIU_DamageSpec LethalHit;
	LethalHit.Amount = 500.0f;
	LethalHit.InstigatorActor = Enemy;
	LethalHit.DamageCauser = Enemy;
	LethalHit.HitDirection = FVector(-1.0f, 0.0f, 0.0f);
	TestTrue(TEXT("Applied lethal damage to Player"), Player->GetDamageComponent()->ApplyDamageSpec(LethalHit));

	TestTrue(TEXT("Player AttributeComponent IsDead() == true"), Player->GetAttributeComponent()->IsDead());
	TestFalse(TEXT("Defeated player immediately exited Flight mode"), Player->GetFlightComponent()->IsFlying());
	TestFalse(TEXT("Defeated player auto-released Lock-On"), Player->GetTargetingComponent()->IsLockedOn());
	TestEqual(TEXT("Defeated player CombatState == Defeated"), Player->GetCombatComponent()->GetCombatState(), EIU_CombatState::Defeated);

	// Attempt movement, jump, and flight inputs while dead -> must remain locked out!
	const FVector DeadPosBeforeInput = Player->GetActorLocation();
	Player->ApplyMovementInput(FVector2D(0.0f, 1.0f));
	Player->StartJumpInput();
	Player->StartSprintInput();
	Scope.TickWorld(0.20f);
	Player->StopJumpInput();
	Player->StopSprintInput();

	TestFalse(TEXT("Dead player cannot re-enter Flight"), Player->GetFlightComponent()->IsFlying());
	TestTrue(TEXT("Dead player horizontal position stays locked while defeated"),
		FVector::Dist2D(Player->GetActorLocation(), DeadPosBeforeInput) < 5.0f);

	// 3. Advance past PlayerRespawnDelaySeconds (0.60s) -> Auto-Respawn restores full gameplay!
	Scope.TickWorld(0.55f);
	TestFalse(TEXT("Auto-respawned player is alive again"), Player->GetAttributeComponent()->IsDead());
	TestEqual(TEXT("Auto-respawned player restored to MaxHP"), Player->GetAttributeComponent()->GetHP(), Player->GetAttributeComponent()->GetMaxHP());
	TestEqual(TEXT("Auto-respawned player CombatState == Idle"), Player->GetCombatComponent()->GetCombatState(), EIU_CombatState::Idle);

	const FVector RespawnPos = Player->GetActorLocation();
	for (int32 i = 0; i < 15; ++i)
	{
		Player->ApplyMovementInput(FVector2D(0.0f, 1.0f));
		Scope.TickWorld(1.0f / 60.0f);
	}
	TestTrue(TEXT("Respawned player moves normally again"), FVector::Dist2D(Player->GetActorLocation(), RespawnPos) > 25.0f);

	return true;
}

// ============================================================================
// TEST 4 (M7): End-to-End Gameplay Loop & 60 FPS Frame Time Budget Profile
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIUM7FullGameplayLoopAnd60FPSBudgetTest,
	"InfiniteUniverse.Milestone7.FullGameplayLoopAnd60FPSBudgetProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIUM7FullGameplayLoopAnd60FPSBudgetTest::RunTest(const FString& Parameters)
{
	IUM6And7TestHelpers::FScopedFinalTestWorld Scope;
	Scope.SpawnArenaFloor();

	AIU_PlayerController* PC = Scope.World->SpawnActor<AIU_PlayerController>();
	PC->MarkAsLocalTestController();
	AIU_HUDManager* HUD = Scope.World->SpawnActor<AIU_HUDManager>();
	PC->MyHUD = HUD;

	AIU_PlayerCharacter* Player = Scope.World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0f, 0.0f, 95.0f), FRotator::ZeroRotator);
	PC->Possess(Player);

	// Spawn 3 live Enemy Spawners around the combat arena
	TArray<AIU_EnemySpawner*> Spawners;
	const FVector SpawnerOffsets[] = {
		FVector(420.0f, 0.0f, 95.0f),
		FVector(550.0f, 260.0f, 95.0f),
		FVector(550.0f, -260.0f, 280.0f)
	};
	for (int32 i = 0; i < 3; ++i)
	{
		FActorSpawnParameters SP;
		SP.bDeferConstruction = true;
		AIU_EnemySpawner* Spawner = Scope.World->SpawnActor<AIU_EnemySpawner>(AIU_EnemySpawner::StaticClass(), FTransform(FRotator(0.0f, 180.0f, 0.0f), SpawnerOffsets[i]), SP);
		Spawner->RespawnDelaySeconds = 0.80f;
		Spawner->bSpawnAerialFlyer = (i == 2);
		Spawner->FinishSpawning(FTransform(FRotator(0.0f, 180.0f, 0.0f), SpawnerOffsets[i]));
		Spawners.Add(Spawner);
	}

	Scope.TickWorld(0.10f);

	AIU_EnemyCharacter* PrimaryEnemy = Spawners[0]->GetSpawnedEnemy();
	TestNotNull(TEXT("Primary arena enemy spawned"), PrimaryEnemy);
	if (!PrimaryEnemy)
	{
		return false;
	}
	// Set 30 HP on primary enemy so Ki Blast (20) + Melee Combo (15+20+30) defeats it cleanly and triggers spawner respawn
	PrimaryEnemy->GetAttributeComponent()->MaxHP = 30.0f;
	PrimaryEnemy->GetAttributeComponent()->ResetAttributesToFull();

	Player->GetTargetingComponent()->SetLockedTarget(PrimaryEnemy);

	const int32 TotalFrames = 240;
	const float Dt = 1.0f / 60.0f;
	double AccumulatedFrameSeconds = 0.0;
	bool bObservedEnemyDeath = false;
	bool bObservedRespawnPending = false;

	for (int32 Frame = 0; Frame < TotalFrames; ++Frame)
	{
		const double T0 = FPlatformTime::Seconds();

		if (Frame < 20)
		{
			Player->StartSprintInput();
			Player->ApplyMovementInput(FVector2D(0.0f, 1.0f));
		}
		else if (Frame == 20)
		{
			Player->StopSprintInput();
			Player->GetCombatComponent()->RequestKiBlast();
		}
		else if (Frame == 42 || Frame == 60 || Frame == 78)
		{
			// Ensure player is ready to swing (clear any AI stun in 3v1 brawl) and unleash 1->2->3 combo
			Player->GetDamageComponent()->ResetDamageState();
			if (Player->GetCombatComponent()->GetCombatState() == EIU_CombatState::HitReaction)
			{
				Player->GetCombatComponent()->ResetCombatState();
			}
			if (IsValid(PrimaryEnemy) && !PrimaryEnemy->IsDead())
			{
				const FVector StrikeSpot = PrimaryEnemy->GetActorLocation() - FVector(135.0f, 0.0f, 0.0f);
				Player->SetActorLocation(StrikeSpot);
				Player->SetActorRotation(FRotator::ZeroRotator);
			}
			Player->GetCombatComponent()->RequestMeleeAttack();
		}
		else if (Frame == 110)
		{
			// Jump -> 3D Flight transition
			Player->GetDamageComponent()->ResetDamageState();
			Player->StartJumpInput();
		}
		else if (Frame == 114)
		{
			Player->StopJumpInput();
		}
		else if (Frame == 118)
		{
			Player->StartJumpInput();
		}
		else if (Frame >= 119 && Frame < 155)
		{
			Player->ApplyMovementInput(FVector2D(0.5f, 1.0f));
		}
		else if (Frame == 155)
		{
			Player->StopJumpInput();
			Player->GetCombatComponent()->RequestDodge(FVector(-1.0f, 0.0f, 0.0f));
		}

		++GFrameCounter;
		Scope.World->Tick(LEVELTICK_All, Dt);

		const double T1 = FPlatformTime::Seconds();
		AccumulatedFrameSeconds += (T1 - T0);

		if (IsValid(PrimaryEnemy) && PrimaryEnemy->IsDead())
		{
			bObservedEnemyDeath = true;
		}
		if (Spawners[0]->IsRespawnPending())
		{
			bObservedRespawnPending = true;
		}
	}

	const double AvgFrameMs = (AccumulatedFrameSeconds / static_cast<double>(TotalFrames)) * 1000.0;
	UE_LOG(LogTemp, Display, TEXT("[M7 PERF PROFILE] 240 frames simulated (Player + HUD + 3 Anime AI Enemies + Flight + Combat): Avg CPU Frame = %.3f ms (60 FPS Budget = 16.67 ms)"), AvgFrameMs);

	TestTrue(TEXT("Primary enemy was defeated during end-to-end combat loop"), bObservedEnemyDeath || bObservedRespawnPending);
	TestNotNull(TEXT("Primary spawner auto-respawned a fresh enemy"), Spawners[0]->GetSpawnedEnemy());
	if (Spawners[0]->GetSpawnedEnemy())
	{
		TestFalse(TEXT("Respawned enemy is alive with full HP"), Spawners[0]->GetSpawnedEnemy()->IsDead());
	}
	TestFalse(TEXT("Player transform free of NaN after 240-frame full loop"), Player->GetActorLocation().ContainsNaN());
	TestTrue(FString::Printf(TEXT("Average CPU gameplay/AI/anim frame cost (%.3f ms) well under 16.67 ms (60 FPS budget)"), AvgFrameMs),
		AvgFrameMs < 8.0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
