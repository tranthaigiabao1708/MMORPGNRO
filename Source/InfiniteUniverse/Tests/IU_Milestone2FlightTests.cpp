// Copyright InfiniteUniverse Dev. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Core/IU_PlayerController.h"
#include "Movement/IU_MovementComponent.h"
#include "Movement/IU_FlightComponent.h"

namespace IU_M2TestHelpers
{
	struct FScopedFlightTestWorld
	{
		UWorld* World = nullptr;
		UWorld* PrevGWorld = nullptr;

		FScopedFlightTestWorld()
		{
			PrevGWorld = GWorld;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("IU_M2_FlightTestWorld")));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			GWorld = World;
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		void TickSimulation(AIU_PlayerCharacter* PlayerChar, float TotalSeconds, float StepDelta = 1.0f / 60.0f)
		{
			const int32 Steps = FMath::Max(1, FMath::RoundToInt(TotalSeconds / StepDelta));
			for (int32 i = 0; i < Steps; ++i)
			{
				World->Tick(LEVELTICK_All, StepDelta);
				if (PlayerChar)
				{
					if (UIU_FlightComponent* FC = PlayerChar->GetFlightComponent())
					{
						FC->TickComponent(StepDelta, LEVELTICK_All, nullptr);
					}
					if (UIU_MovementComponent* MC = PlayerChar->GetIUMovementComponent())
					{
						MC->bRunPhysicsWithNoController = true;
						MC->TickComponent(StepDelta, LEVELTICK_All, nullptr);
					}
				}
			}
		}

		~FScopedFlightTestWorld()
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
	FIU_M2_DoubleSpaceActivationRuleTest,
	"InfiniteUniverse.Milestone2.FlightTransitionsNoTeleport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M2_DoubleSpaceActivationRuleTest::RunTest(const FString& Parameters)
{
	IU_M2TestHelpers::FScopedFlightTestWorld ScopeWorld;
	AIU_PlayerCharacter* PlayerChar = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator);
	AIU_PlayerController* PC = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	PC->Possess(PlayerChar);

	UIU_FlightComponent* FlightComp = PlayerChar->GetFlightComponent();
	UIU_MovementComponent* MoveComp = PlayerChar->GetIUMovementComponent();

	// 1. Start on ground: 1st Space press MUST ONLY Jump, NEVER auto-enter flight even if held for 0.6s!
	MoveComp->SetMovementMode(MOVE_Walking);
	PlayerChar->StartJumpInput();
	ScopeWorld.TickSimulation(PlayerChar, 0.60f);
	TestFalse(TEXT("Holding 1st Space from ground does NOT enter Flight"), FlightComp->IsFlying());
	PlayerChar->StopJumpInput();

	// 2. Now character is airborne (MOVE_Falling / Jumping): 2nd Space press MUST activate Flight with zero teleport!
	MoveComp->SetMovementMode(MOVE_Falling);
	const FVector PosBeforeSecondSpace = PlayerChar->GetActorLocation();
	PlayerChar->StartJumpInput(); // 2nd Space press in mid-air!
	const FVector PosAfterSecondSpace = PlayerChar->GetActorLocation();

	TestTrue(TEXT("2nd Space press while airborne activates Flight"), FlightComp->IsFlying());
	TestEqual(TEXT("IUMovementMode is Flying"), MoveComp->GetIUMovementMode(), EIU_PlayerMovementMode::Flying);
	TestTrue(TEXT("Zero position teleport on double-Space activation"), FVector::Dist(PosBeforeSecondSpace, PosAfterSecondSpace) < 0.1);

	PlayerChar->StopJumpInput();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M2_GradualAscentCurveAndCtrl2xSpeedTest,
	"InfiniteUniverse.Milestone2.SixDOFHoverAndDash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M2_GradualAscentCurveAndCtrl2xSpeedTest::RunTest(const FString& Parameters)
{
	IU_M2TestHelpers::FScopedFlightTestWorld ScopeWorld;
	AIU_PlayerCharacter* PlayerChar = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0, 0.0, 500.0), FRotator::ZeroRotator);
	AIU_PlayerController* PC = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	PC->Possess(PlayerChar);

	UIU_FlightComponent* FlightComp = PlayerChar->GetFlightComponent();
	UIU_MovementComponent* MoveComp = PlayerChar->GetIUMovementComponent();

	// Verify updated speed limits (1.5x previous: 1575 planar, 1275 vertical, x2.0 Ctrl multiplier)
	TestEqual(TEXT("Max normal FlightSpeed is 1.5x (1575 cm/s)"), FlightComp->FlightSpeed, 1575.0f);
	TestEqual(TEXT("Max normal VerticalFlightSpeed is 1.5x (1275 cm/s)"), FlightComp->VerticalFlightSpeed, 1275.0f);
	TestEqual(TEXT("Ctrl FlightDashMultiplier is x2.0"), FlightComp->FlightDashMultiplier, 2.0f);

	// Activate flight via airborne Space and KEEP HOLDING Space to test 2-stage physical ascent curve!
	MoveComp->SetMovementMode(MOVE_Falling);
	PlayerChar->StartJumpInput(); // 2nd Space press + keep holding!

	// Stage 1: At t = 0.50s (within the 1.0s initial slow window), ascent speed MUST be gentle (< 45% of max)!
	ScopeWorld.TickSimulation(PlayerChar, 0.50f);
	const double SpeedAtHalfSec = MoveComp->Velocity.Z;
	TestTrue(TEXT("Ascent is positive during first 1s slow gather"), SpeedAtHalfSec > 100.0);
	TestTrue(TEXT("Ascent stays slow during first 1s (< 550 cm/s)"), SpeedAtHalfSec < 550.0);

	// Stage 2: Tick past 1.0s up to t = 2.50s -> must accelerate rapidly and cap at VerticalFlightSpeed (1275 cm/s)!
	ScopeWorld.TickSimulation(PlayerChar, 2.00f);
	const double SpeedAtTerminalNormal = MoveComp->Velocity.Z;
	TestTrue(TEXT("After 1s, ascent accelerates far beyond slow-phase speed"), SpeedAtTerminalNormal > SpeedAtHalfSec * 2.2);
	TestTrue(TEXT("Normal ascent caps cleanly at 1.5x VerticalFlightSpeed (~1275 cm/s)"),
		FMath::IsNearlyEqual(SpeedAtTerminalNormal, (double)FlightComp->VerticalFlightSpeed, 25.0));

	// Stage 3: Hold Left Ctrl (StartSprintInput) -> speed doubles to x2 (2550 cm/s)!
	PlayerChar->StartSprintInput();
	ScopeWorld.TickSimulation(PlayerChar, 0.80f);
	const double SpeedWithCtrl2x = MoveComp->Velocity.Z;
	TestTrue(TEXT("Holding Ctrl doubles terminal ascent speed to x2 (~2550 cm/s)"),
		FMath::IsNearlyEqual(SpeedWithCtrl2x, (double)(FlightComp->VerticalFlightSpeed * 2.0f), 35.0));

	// Release inputs -> smooth braking into stationary Hover
	PlayerChar->StopSprintInput();
	PlayerChar->StopJumpInput();
	ScopeWorld.TickSimulation(PlayerChar, 1.00f);
	TestTrue(TEXT("Releasing inputs brakes smoothly to Hover"), FlightComp->IsHovering());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M2_ShiftDescendAndLandingTest,
	"InfiniteUniverse.Milestone2.CollisionAndLanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M2_ShiftDescendAndLandingTest::RunTest(const FString& Parameters)
{
	IU_M2TestHelpers::FScopedFlightTestWorld ScopeWorld;

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	AStaticMeshActor* FloorActor = ScopeWorld.World->SpawnActor<AStaticMeshActor>(FVector(0.0, 0.0, 0.0), FRotator::ZeroRotator);
	FloorActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	FloorActor->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
	FloorActor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	FloorActor->SetActorScale3D(FVector(20.0, 20.0, 1.0)); // Top surface at Z = +50 cm

	AIU_PlayerCharacter* PlayerChar = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0, 0.0, 260.0), FRotator::ZeroRotator);
	AIU_PlayerController* PC = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	PC->Possess(PlayerChar);

	UIU_FlightComponent* FlightComp = PlayerChar->GetFlightComponent();
	UIU_MovementComponent* MoveComp = PlayerChar->GetIUMovementComponent();
	FlightComp->EnterFlight();

	// Hold Left Shift while flying -> descends (-Z) and lands cleanly on floor!
	PlayerChar->StartDescendInput();
	ScopeWorld.TickSimulation(PlayerChar, 0.60f);
	PlayerChar->StopDescendInput();

	TestFalse(TEXT("Shift-descending onto floor exited flight cleanly"), FlightComp->IsFlying());
	TestEqual(TEXT("Character transitioned cleanly to Grounded on touchdown"), MoveComp->GetIUMovementMode(), EIU_PlayerMovementMode::Grounded);
	TestTrue(TEXT("Character capsule did not penetrate below floor surface"), PlayerChar->GetActorLocation().Z >= 130.0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
