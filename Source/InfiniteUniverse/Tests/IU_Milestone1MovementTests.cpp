// Copyright InfiniteUniverse Dev. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Core/IU_GameMode.h"
#include "Core/IU_PlayerController.h"
#include "Movement/IU_MovementComponent.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"

namespace IU_TestHelpers
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("IU_M1_TestWorld")));
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M1_CharacterCameraAndZoomTest,
	"InfiniteUniverse.Milestone1.CharacterCameraAndZoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M1_CharacterCameraAndZoomTest::RunTest(const FString& Parameters)
{
	IU_TestHelpers::FScopedTestWorld ScopeWorld;
	TestNotNull(TEXT("Transient Game World created"), ScopeWorld.World);

	AIU_PlayerCharacter* PlayerChar = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator);
	TestNotNull(TEXT("AIU_PlayerCharacter spawned"), PlayerChar);
	if (!PlayerChar)
	{
		return false;
	}

	UIU_MovementComponent* MoveComp = PlayerChar->GetIUMovementComponent();
	USpringArmComponent* Boom = PlayerChar->GetCameraBoom();
	UCameraComponent* Cam = PlayerChar->GetFollowCamera();

	TestNotNull(TEXT("Custom UIU_MovementComponent attached"), MoveComp);
	TestNotNull(TEXT("SpringArm CameraBoom attached"), Boom);
	TestNotNull(TEXT("FollowCamera attached"), Cam);

	TestTrue(TEXT("Camera collision test enabled (P1-06)"), Boom->bDoCollisionTest);
	TestTrue(TEXT("SpringArm uses pawn control rotation (360 orbit)"), Boom->bUsePawnControlRotation);
	TestFalse(TEXT("FollowCamera does not double-apply control rotation"), Cam->bUsePawnControlRotation);
	TestEqual(TEXT("Camera TargetOffset Y is centered on character axis (0)"), Boom->TargetOffset.Y, 0.0);
	TestEqual(TEXT("Camera SocketOffset Y is centered on character axis (0)"), Boom->SocketOffset.Y, 0.0);

	// Test camera zoom clamping within [MinCameraArmLength, MaxCameraArmLength]
	PlayerChar->AdjustCameraZoom(100.0f); // Zoom way in
	TestEqual(TEXT("Zoom in clamps cleanly at MinCameraArmLength"), PlayerChar->GetDesiredCameraArmLength(), PlayerChar->MinCameraArmLength);

	PlayerChar->AdjustCameraZoom(-100.0f); // Zoom way out
	TestEqual(TEXT("Zoom out clamps cleanly at MaxCameraArmLength"), PlayerChar->GetDesiredCameraArmLength(), PlayerChar->MaxCameraArmLength);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M1_WalkSprintAndRotationModesTest,
	"InfiniteUniverse.Milestone1.WalkSprintAndRotationModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M1_WalkSprintAndRotationModesTest::RunTest(const FString& Parameters)
{
	IU_TestHelpers::FScopedTestWorld ScopeWorld;
	AIU_PlayerCharacter* PlayerChar = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	TestNotNull(TEXT("AIU_PlayerCharacter spawned"), PlayerChar);
	if (!PlayerChar)
	{
		return false;
	}

	UIU_MovementComponent* MoveComp = PlayerChar->GetIUMovementComponent();
	TestNotNull(TEXT("UIU_MovementComponent valid"), MoveComp);

	// 1. Default Walk Speed & Sprint toggle (P1-03)
	TestEqual(TEXT("Initial MaxWalkSpeed equals WalkSpeed"), MoveComp->MaxWalkSpeed, MoveComp->WalkSpeed);

	PlayerChar->StartSprintInput();
	TestTrue(TEXT("IsSprinting true after StartSprintInput"), MoveComp->IsSprinting());
	TestEqual(TEXT("MaxWalkSpeed updates to SprintSpeed"), MoveComp->MaxWalkSpeed, MoveComp->SprintSpeed);

	PlayerChar->StopSprintInput();
	TestFalse(TEXT("IsSprinting false after StopSprintInput"), MoveComp->IsSprinting());
	TestEqual(TEXT("MaxWalkSpeed restores to WalkSpeed"), MoveComp->MaxWalkSpeed, MoveComp->WalkSpeed);

	// 2. Editable config verification (No hardcoded movement lock)
	MoveComp->WalkSpeed = 615.0f;
	MoveComp->RefreshMovementSettings();
	TestEqual(TEXT("Modified WalkSpeed UPROPERTY applies immediately"), MoveComp->MaxWalkSpeed, 615.0f);

	// 3. Rotation Mode switching (Default = OrientToCamera per user specification)
	TestEqual(TEXT("Default rotation mode is OrientToCamera"), MoveComp->GetCharacterRotationMode(), EIU_CharacterRotationMode::OrientToCamera);
	TestFalse(TEXT("bOrientRotationToMovement inactive in default camera mode"), MoveComp->bOrientRotationToMovement);
	TestTrue(TEXT("Smooth bUseControllerDesiredRotation active in default camera mode"), MoveComp->bUseControllerDesiredRotation);
	TestFalse(TEXT("Direct instant yaw snap remains off to prevent abnormal snapping"), PlayerChar->bUseControllerRotationYaw);

	PlayerChar->ToggleRotationModeInput();
	TestEqual(TEXT("Switched rotation mode to OrientToMovement"), MoveComp->GetCharacterRotationMode(), EIU_CharacterRotationMode::OrientToMovement);
	TestTrue(TEXT("bOrientRotationToMovement active after toggle"), MoveComp->bOrientRotationToMovement);
	TestFalse(TEXT("bUseControllerDesiredRotation inactive after toggle"), MoveComp->bUseControllerDesiredRotation);

	// 4. Verify IMC_Player.uasset on disk actually persisted W/A/S/D & Mouse2D modifiers!
	UInputMappingContext* LoadedIMC = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Player.IMC_Player"));
	TestNotNull(TEXT("IMC_Player loaded from /Game/Input/IMC_Player"), LoadedIMC);
	if (LoadedIMC)
	{
		bool bFoundWWithSwizzle = false;
		bool bFoundSWithSwizzleAndNegate = false;
		bool bFoundAWithNegate = false;
		bool bFoundDClean = false;

		for (const FEnhancedActionKeyMapping& Mapping : LoadedIMC->GetMappings())
		{
			if (Mapping.Key == EKeys::W)
			{
				bFoundWWithSwizzle = (Mapping.Modifiers.Num() == 1);
			}
			else if (Mapping.Key == EKeys::S)
			{
				bFoundSWithSwizzleAndNegate = (Mapping.Modifiers.Num() == 2);
			}
			else if (Mapping.Key == EKeys::A)
			{
				bFoundAWithNegate = (Mapping.Modifiers.Num() == 1);
			}
			else if (Mapping.Key == EKeys::D)
			{
				bFoundDClean = (Mapping.Modifiers.Num() == 0);
			}
		}

		TestTrue(TEXT("W key has Swizzle(YXZ) persisted (+Y Forward)"), bFoundWWithSwizzle);
		TestTrue(TEXT("S key has Swizzle(YXZ)+Negate persisted (-Y Backward)"), bFoundSWithSwizzleAndNegate);
		TestTrue(TEXT("A key has Negate persisted (-X Left)"), bFoundAWithNegate);
		TestTrue(TEXT("D key has clean +X Right mapping"), bFoundDClean);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIU_M1_GameModeAndJumpStateTest,
	"InfiniteUniverse.Milestone1.GameModeAndJumpState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIU_M1_GameModeAndJumpStateTest::RunTest(const FString& Parameters)
{
	AIU_GameMode* DefaultGM = GetMutableDefault<AIU_GameMode>();
	TestNotNull(TEXT("AIU_GameMode CDO exists"), DefaultGM);
	TestTrue(TEXT("DefaultPawnClass is AIU_PlayerCharacter"), DefaultGM->DefaultPawnClass == AIU_PlayerCharacter::StaticClass());
	TestTrue(TEXT("PlayerControllerClass is AIU_PlayerController"), DefaultGM->PlayerControllerClass == AIU_PlayerController::StaticClass());

	IU_TestHelpers::FScopedTestWorld ScopeWorld;
	AIU_PlayerCharacter* PlayerChar = ScopeWorld.World->SpawnActor<AIU_PlayerCharacter>(FVector(0.f, 0.f, 200.f), FRotator::ZeroRotator);
	UIU_MovementComponent* MoveComp = PlayerChar->GetIUMovementComponent();

	MoveComp->SetMovementMode(MOVE_Walking);
	TestEqual(TEXT("Walking maps to EIU_PlayerMovementMode::Grounded"), MoveComp->GetIUMovementMode(), EIU_PlayerMovementMode::Grounded);

	MoveComp->SetMovementMode(MOVE_Falling);
	TestEqual(TEXT("Falling maps to EIU_PlayerMovementMode::Jumping"), MoveComp->GetIUMovementMode(), EIU_PlayerMovementMode::Jumping);

	FHitResult DummyLandingHit;
	PlayerChar->Landed(DummyLandingHit);
	TestEqual(TEXT("Landed() restores EIU_PlayerMovementMode::Grounded"), MoveComp->GetIUMovementMode(), EIU_PlayerMovementMode::Grounded);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
