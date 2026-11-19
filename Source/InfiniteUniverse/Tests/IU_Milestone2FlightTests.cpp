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
	FIU_M2_FlightTransitionsNoTeleportTest,
	"InfiniteUniverse.Milestone2.FlightTransitionsNoTeleport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M2_FlightTransitionsNoTeleportTest::RunTest(const FString& Parameters)
{
	IU_M2TestHelpers::FScopedFlightTestWorld ScopeWorld;
	AIU_PlayerCharacter* PlayerChar = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator);
	AIU_PlayerController* PC = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	TestNotNull(TEXT("Spawned PlayerChar"), PlayerChar);
	TestNotNull(TEXT("Spawned PlayerController"), PC);
	if (!PlayerChar || !PC)
	{
		return false;
	}
	PC->Possess(PlayerChar);

	UIU_FlightComponent* FlightComp = PlayerChar->GetFlightComponent();
	UIU_MovementComponent* MoveComp = PlayerChar->GetIUMovementComponent();
	TestNotNull(TEXT("FlightComponent attached"), FlightComp);
	TestNotNull(TEXT("MovementComponent attached"), MoveComp);

	const FVector PosBeforeFlight = PlayerChar->GetActorLocation();
	const bool bEntered = FlightComp->EnterFlight();
	const FVector PosImmediatelyAfterEnter = PlayerChar->GetActorLocation();

	TestTrue(TEXT("EnterFlight succeeded"), bEntered);
	TestTrue(TEXT("IsFlying() is true"), FlightComp->IsFlying());
	TestEqual(TEXT("IUMovementMode is Flying"), MoveComp->GetIUMovementMode(), EIU_PlayerMovementMode::Flying);

	// Mandatory Rule: Zero instant position teleport when entering flight!
	const double InstantDelta = FVector::Dist(PosBeforeFlight, PosImmediatelyAfterEnter);
	TestTrue(TEXT("No character teleport on flight activation (< 0.1 cm delta)"), InstantDelta < 0.1);

	// Exit flight in mid-air -> transitions cleanly through Landing into Jumping/Falling without teleport
	FlightComp->ExitFlight(false);
	TestFalse(TEXT("IsFlying() is false after ExitFlight"), FlightComp->IsFlying());
	TestEqual(TEXT("Airborne exit transitions to Jumping/Falling"), MoveComp->GetIUMovementMode(), EIU_PlayerMovementMode::Jumping);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M2_SixDOFHoverAndDashTest,
	"InfiniteUniverse.Milestone2.SixDOFHoverAndDash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M2_SixDOFHoverAndDashTest::RunTest(const FString& Parameters)
{
	IU_M2TestHelpers::FScopedFlightTestWorld ScopeWorld;
	AIU_PlayerCharacter* PlayerChar = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(0.0, 0.0, 500.0), FRotator::ZeroRotator);
	AIU_PlayerController* PC = ScopeWorld.World->SpawnActor<AIU_PlayerController>();
	PC->Possess(PlayerChar);

	UIU_FlightComponent* FlightComp = PlayerChar->GetFlightComponent();
	UIU_MovementComponent* MoveComp = PlayerChar->GetIUMovementComponent();
	FlightComp->EnterFlight();

	// 1. Stationary Hover test (settle liftoff velocity with zero inputs -> must hover without losing altitude)
	FlightComp->SetMoveAxisInput(FVector2D::ZeroVector);
	ScopeWorld.TickSimulation(PlayerChar, 0.35f);
	TestTrue(TEXT("Character settles into stationary Hover with no keys held"), FlightComp->IsHovering());
	const double HoverZStart = PlayerChar->GetActorLocation().Z;
	ScopeWorld.TickSimulation(PlayerChar, 0.50f);
	const double HoverZEnd = PlayerChar->GetActorLocation().Z;
	TestTrue(TEXT("Hover maintains altitude without unexpected drop"), FMath::Abs(HoverZEnd - HoverZStart) < 1.0);

	// 2. Camera-relative 3D Forward flight (Pitch +30 deg upward + W -> moves +X and +Z!)
	PC->SetControlRotation(FRotator(30.0f, 0.0f, 0.0f));
	const FVector Start3D = PlayerChar->GetActorLocation();
	PlayerChar->ApplyMovementInput(FVector2D(0.0f, 1.0f));
	ScopeWorld.TickSimulation(PlayerChar, 0.40f);
	const FVector AfterForwardUp = PlayerChar->GetActorLocation();
	TestTrue(TEXT("Camera-pitched W moves forward (+X)"), AfterForwardUp.X > Start3D.X + 100.0);
	TestTrue(TEXT("Camera-pitched W climbs upward (+Z) along 3D view"), AfterForwardUp.Z > Start3D.Z + 50.0);

	// 3. Strafe Right (+Y) & Left (-Y), Ascend (+Z), Descend (-Z)
	PC->SetControlRotation(FRotator::ZeroRotator);
	PlayerChar->ApplyMovementInput(FVector2D(1.0f, 0.0f)); // D
	ScopeWorld.TickSimulation(PlayerChar, 0.35f);
	TestTrue(TEXT("Right flight input moves +Y"), MoveComp->Velocity.Y > 200.0);

	PlayerChar->ApplyMovementInput(FVector2D::ZeroVector);
	PlayerChar->StartJumpInput(); // Space while flying = Ascend
	ScopeWorld.TickSimulation(PlayerChar, 0.30f);
	TestTrue(TEXT("Space while flying ascends (+Z velocity)"), MoveComp->Velocity.Z > 200.0);
	PlayerChar->StopJumpInput();

	PlayerChar->StartDescendInput(); // Ctrl while flying = Descend
	ScopeWorld.TickSimulation(PlayerChar, 0.45f);
	TestTrue(TEXT("Ctrl while flying descends (-Z velocity)"), MoveComp->Velocity.Z < -200.0);
	PlayerChar->StopDescendInput();

	// 4. Flight Dash speed multiplier verification
	PlayerChar->ApplyMovementInput(FVector2D(0.0f, 1.0f));
	PlayerChar->StartSprintInput();
	ScopeWorld.TickSimulation(PlayerChar, 0.60f);
	TestTrue(TEXT("IsFlightDashing() active during Shift + Move"), FlightComp->IsFlightDashing());
	TestTrue(TEXT("Flight Dash exceeds normal FlightSpeed"), MoveComp->Velocity.Size() > FlightComp->FlightSpeed * 1.3f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M2_CollisionAndLandingTest,
	"InfiniteUniverse.Milestone2.CollisionAndLanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M2_CollisionAndLandingTest::RunTest(const FString& Parameters)
{
	IU_M2TestHelpers::FScopedFlightTestWorld ScopeWorld;

	// Spawn solid floor cube from Z=-50 to Z=+50 (100cm Cube scaled 20x20x1)
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	TestNotNull(TEXT("Loaded Engine Cube StaticMesh"), CubeMesh);

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

	// Descend onto walkable floor -> must transition cleanly Flight -> Landing -> Grounded without penetrating floor!
	PlayerChar->StartDescendInput();
	ScopeWorld.TickSimulation(PlayerChar, 0.60f);
	PlayerChar->StopDescendInput();

	TestFalse(TEXT("Descending onto floor exited flight cleanly"), FlightComp->IsFlying());
	TestEqual(TEXT("Character transitioned cleanly to Grounded on touchdown"), MoveComp->GetIUMovementMode(), EIU_PlayerMovementMode::Grounded);
	TestTrue(TEXT("Character capsule did not penetrate below floor surface"), PlayerChar->GetActorLocation().Z >= 130.0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
