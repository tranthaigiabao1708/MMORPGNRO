// Copyright InfiniteUniverse Dev. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Core/IU_PlayerController.h"
#include "Movement/IU_MovementComponent.h"
#include "Combat/IU_CombatComponent.h"
#include "Combat/IU_TargetingComponent.h"
#include "Combat/IU_KiProjectile.h"
#include "Combat/IU_PracticeDummy.h"
#include "AI/IU_EnemySpawner.h"
#include "Attributes/IU_AttributeComponent.h"

namespace IU_M5TestHelpers
{
	struct FScopedCameraGreyboxWorld
	{
		UWorld* World = nullptr;
		UWorld* PrevGWorld = nullptr;

		FScopedCameraGreyboxWorld()
		{
			PrevGWorld = GWorld;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("IU_M5_CameraGreyboxWorld")));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			GWorld = World;
			FURL URL;
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

		~FScopedCameraGreyboxWorld()
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
// TEST 1: Genshin Impact-Inspired Elevated Camera & Zero-Parallax Crosshair Aim
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M5_GenshinElevatedCameraAndAimRayTest,
	"InfiniteUniverse.Milestone5.GenshinElevatedCameraAndTrueAimRay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M5_GenshinElevatedCameraAndAimRayTest::RunTest(const FString& Parameters)
{
	IU_M5TestHelpers::FScopedCameraGreyboxWorld ScopeWorld;

	AIU_PlayerCharacter* Player = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0, 0.0, 90.0), FRotator::ZeroRotator);
	AIU_PlayerController* PC = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	PC->MarkAsLocalTestController();
	PC->Possess(Player);
	PC->ApplyInitialGameplayCameraAngle();
	Player->GetIUMovementComponent()->GravityScale = 0.0f;

	Player->GetCameraBoom()->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);

	// 1. Verify elevated Genshin-style camera geometry clears the top of the 180 cm character's head (+180 cm Z)
	const float CharacterTopOfHeadZ = Player->GetActorLocation().Z + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(); // 180 cm
	FVector CamOrigin, CamDir;
	TestTrue(TEXT("GetCameraAimRay resolved active FollowCamera lens"), Player->GetCameraAimRay(CamOrigin, CamDir));
	TestTrue(TEXT("Elevated Genshin camera lens sits >= 70 cm ABOVE the top of character's head"),
		CamOrigin.Z >= CharacterTopOfHeadZ + 70.0f);
	TestTrue(TEXT("Initial gameplay pitch looks gently downward (-8.5 deg)"),
		FMath::IsNearlyEqual(PC->GetControlRotation().Pitch, -8.5, 0.25) ||
		FMath::IsNearlyEqual(FRotator::NormalizeAxis(PC->GetControlRotation().Pitch), -8.5, 0.25));

	// 2. Place a non-locked enemy target directly on the exact camera crosshair sightline 900 cm away and fire Free-Aim Ki Blast!
	const FVector PointOnCrosshair = CamOrigin + (CamDir * 1300.0f) - FVector(0.0f, 0.0f, 30.0f);
	AIU_PracticeDummy* CrosshairTarget = ScopeWorld.World->SpawnActor<AIU_PracticeDummy>(PointOnCrosshair, FRotator(0.0, 180.0, 0.0));
	CrosshairTarget->bAutoResetAfterDefeat = false;

	if (!Player->GetCombatComponent()->KiProjectileClass)
	{
		Player->GetCombatComponent()->KiProjectileClass = AIU_KiProjectile::StaticClass();
	}

	const float HPBefore = CrosshairTarget->GetAttributeComponent()->GetHP();
	AIU_KiProjectile* FreeAimBlast = Player->GetCombatComponent()->RequestKiBlast();
	TestNotNull(TEXT("Free-aim Ki Blast fired without Lock-On"), FreeAimBlast);

	ScopeWorld.TickWorld(0.55f);
	TestTrue(TEXT("Zero-parallax Free-Aim Ki Blast hit target directly under camera crosshair"),
		CrosshairTarget->GetAttributeComponent()->GetHP() < HPBefore);

	return true;
}

// ============================================================================
// TEST 2: Camera Wall Occlusion Collision (P5-04)
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M5_CameraWallOcclusionNoClipTest,
	"InfiniteUniverse.Milestone5.CameraWallOcclusionNoClip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M5_CameraWallOcclusionNoClipTest::RunTest(const FString& Parameters)
{
	IU_M5TestHelpers::FScopedCameraGreyboxWorld ScopeWorld;

	AIU_PlayerCharacter* Player = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0, 0.0, 120.0), FRotator::ZeroRotator);
	AIU_PlayerController* PC = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	PC->MarkAsLocalTestController();
	PC->Possess(Player);
	Player->GetIUMovementComponent()->GravityScale = 0.0f;

	Player->GetCameraBoom()->bEnableCameraLag = false;
	Player->GetCameraBoom()->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	const FVector UnobstructedCamLoc = Player->GetFollowCamera()->GetComponentLocation();
	TestTrue(TEXT("Unobstructed camera sits ~420 cm behind player (-X)"), UnobstructedCamLoc.X < -380.0);

	// Place a thick BlockAll wall at X = -200 cm (between Player X=0 and unobstructed camera X=-420)
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	AStaticMeshActor* Wall = ScopeWorld.World->SpawnActor<AStaticMeshActor>(FVector(-200.0, 0.0, 200.0), FRotator::ZeroRotator);
	Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Wall->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
	Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	Wall->SetActorScale3D(FVector(0.4, 8.0, 6.0)); // Front face toward player at X = -180 cm

	ScopeWorld.TickWorld(0.10f);
	Player->GetCameraBoom()->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);

	const FVector OccludedCamLoc = Player->GetFollowCamera()->GetComponentLocation();
	TestTrue(TEXT("SpringArm pulled camera forward in front of blocking wall (no wall clipping)"), OccludedCamLoc.X > -182.0);

	return true;
}

// ============================================================================
// TEST 3: Greybox Map Integrity (Floor, Parkour, Occlusion, Flight, Arena)
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M5_GreyboxMapIntegrityTest,
	"InfiniteUniverse.Milestone5.GreyboxMapZonesAndCollisionIntegrity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M5_GreyboxMapIntegrityTest::RunTest(const FString& Parameters)
{
	UWorld* MapWorld = LoadObject<UWorld>(nullptr, TEXT("/Game/Maps/Prototype/Lvl_Greybox_Prototype.Lvl_Greybox_Prototype"));
	if (!MapWorld)
	{
		MapWorld = LoadObject<UWorld>(nullptr, TEXT("/Game/Maps/Prototype/M_Prototype.M_Prototype"));
	}
	TestNotNull(TEXT("Loaded Greybox Prototype Map (/Game/Maps/Prototype/Lvl_Greybox_Prototype)"), MapWorld);
	if (!MapWorld)
	{
		return false;
	}

	int32 PlayerStartCount = 0;
	int32 EnemySpawnerCount = 0;
	int32 PracticeDummyCount = 0;
	int32 StaticGeoCount = 0;
	double HighestStructureZ = 0.0;

	for (TActorIterator<AActor> It(MapWorld); It; ++It)
	{
		const AActor* Act = *It;
		if (!IsValid(Act))
		{
			continue;
		}
		if (Act->IsA<APlayerStart>())
		{
			++PlayerStartCount;
		}
		else if (Act->IsA<AIU_EnemySpawner>())
		{
			++EnemySpawnerCount;
		}
		else if (Act->IsA<AIU_PracticeDummy>())
		{
			++PracticeDummyCount;
		}
		else if (const AStaticMeshActor* SMA = Cast<AStaticMeshActor>(Act))
		{
			if (!SMA->GetActorNameOrLabel().StartsWith(TEXT("IU_Greybox_")))
			{
				continue;
			}

			++StaticGeoCount;
			HighestStructureZ = FMath::Max(HighestStructureZ, SMA->GetActorLocation().Z);

			if (const UStaticMeshComponent* SMC = SMA->GetStaticMeshComponent())
			{
				TestTrue(TEXT("Greybox StaticMeshActor has valid collision enabled"),
					SMC->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
			}
		}
	}

	TestTrue(TEXT("Greybox map has at least 1 PlayerStart"), PlayerStartCount >= 1);
	TestTrue(TEXT("Greybox map has >= 3 Enemy AI Spawners (Ground + Aerial)"), EnemySpawnerCount >= 3);
	TestTrue(TEXT("Greybox map has Practice Dummies in forward courtyard"), PracticeDummyCount >= 3);
	TestTrue(TEXT("Greybox map contains rich obstacle, corridor, cliff & flight-gate geometry (>= 25 meshes)"), StaticGeoCount >= 25);
	TestTrue(TEXT("Greybox map includes high-altitude 3D flight structures (Z >= 5000 cm)"), HighestStructureZ >= 5000.0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
