// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Characters/Player/IU_PlayerCharacter.h"
#include "Movement/IU_MovementComponent.h"
#include "Movement/IU_FlightComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Combat/IU_TargetingComponent.h"
#include "Combat/IU_CombatComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"

AIU_PlayerCharacter::AIU_PlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UIU_MovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// Zero-Tick when camera zoom is settled; enabled dynamically only during zoom interpolation
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// Standard 180 cm anime humanoid capsule (Radius 40 cm, HalfHeight 90 cm)
	GetCapsuleComponent()->InitCapsuleSize(40.0f, 90.0f);

	// Decouple direct actor rotation from camera; UIU_MovementComponent controls smooth yaw rotation
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	IUMovementComponent = Cast<UIU_MovementComponent>(GetCharacterMovement());
	FlightComponent = CreateDefaultSubobject<UIU_FlightComponent>(TEXT("FlightComponent"));
	AttributeComponent = CreateDefaultSubobject<UIU_AttributeComponent>(TEXT("AttributeComponent"));
	DamageComponent = CreateDefaultSubobject<UIU_DamageComponent>(TEXT("DamageComponent"));
	TargetingComponent = CreateDefaultSubobject<UIU_TargetingComponent>(TEXT("TargetingComponent"));
	CombatComponent = CreateDefaultSubobject<UIU_CombatComponent>(TEXT("CombatComponent"));

	// Position SkeletalMesh at bottom of capsule facing +X
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
	GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

	// Placeholder geometric silhouette for M1-M5 testing (auto-hidden once Anime SkeletalMesh is set)
	PlaceholderBodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBodyMesh"));
	PlaceholderBodyMesh->SetupAttachment(GetCapsuleComponent());
	PlaceholderBodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -5.0f));
	PlaceholderBodyMesh->SetRelativeScale3D(FVector(0.65f, 0.65f, 1.65f));

	PlaceholderFacingVisor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderFacingVisor"));
	PlaceholderFacingVisor->SetupAttachment(PlaceholderBodyMesh);
	PlaceholderFacingVisor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderFacingVisor->SetRelativeLocation(FVector(42.0f, 0.0f, 32.0f));
	PlaceholderFacingVisor->SetRelativeScale3D(FVector(0.45f, 0.70f, 0.12f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderAsset.Succeeded())
	{
		PlaceholderBodyMesh->SetStaticMesh(CylinderAsset.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeAsset.Succeeded())
	{
		PlaceholderFacingVisor->SetStaticMesh(CubeAsset.Object);
	}

	// Third-person SpringArm centered strictly on the character's spine axis (X=0, Y=0)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetRelativeLocation(FVector::ZeroVector);
	CameraBoom->TargetArmLength = DefaultCameraArmLength;
	CameraBoom->TargetOffset = CameraTargetOffset;
	CameraBoom->SocketOffset = CameraSocketOffset;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeChannel = ECC_Camera;
	CameraBoom->ProbeSize = 12.0f;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 14.0f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	DesiredCameraArmLength = DefaultCameraArmLength;
}

void AIU_PlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	DesiredCameraArmLength = FMath::Clamp(DefaultCameraArmLength, MinCameraArmLength, MaxCameraArmLength);
	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = DesiredCameraArmLength;
		CameraBoom->TargetOffset = CameraTargetOffset;
		CameraBoom->SocketOffset = CameraSocketOffset;
	}
	if (FollowCamera)
	{
		FollowCamera->SetFieldOfView(DefaultCameraFOV);
	}
	if (FlightComponent)
	{
		FlightComponent->OnFlightDashChanged.AddDynamic(this, &AIU_PlayerCharacter::HandleFlightDashChanged);
	}
	if (CombatComponent)
	{
		CombatComponent->OnComboStepExecuted.AddDynamic(this, &AIU_PlayerCharacter::HandleComboVisualPulse);
	}

	UpdatePlaceholderVisibility();
}

void AIU_PlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const float TargetArm = DesiredCameraArmLength + (bFlightDashCameraActive ? FlightDashArmLengthOffset : 0.0f);
	const float TargetFOV = bFlightDashCameraActive ? FlightDashCameraFOV : DefaultCameraFOV;

	bool bArmSettled = true;
	bool bFOVSettled = true;

	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = FMath::FInterpTo(
			CameraBoom->TargetArmLength,
			TargetArm,
			DeltaTime,
			CameraZoomInterpSpeed);

		bArmSettled = FMath::IsNearlyEqual(CameraBoom->TargetArmLength, TargetArm, 0.25f);
		if (bArmSettled)
		{
			CameraBoom->TargetArmLength = TargetArm;
		}
	}

	if (FollowCamera)
	{
		const float NewFOV = FMath::FInterpTo(FollowCamera->FieldOfView, TargetFOV, DeltaTime, 10.0f);
		FollowCamera->SetFieldOfView(NewFOV);
		bFOVSettled = FMath::IsNearlyEqual(NewFOV, TargetFOV, 0.15f);
		if (bFOVSettled)
		{
			FollowCamera->SetFieldOfView(TargetFOV);
		}
	}

	if (bArmSettled && bFOVSettled)
	{
		SetActorTickEnabled(false);
	}
}

void AIU_PlayerCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (FlightComponent)
	{
		FlightComponent->SetMoveAxisInput(FVector2D::ZeroVector);
	}
	if (IUMovementComponent)
	{
		IUMovementComponent->SetIUMovementMode(EIU_PlayerMovementMode::Grounded);
	}
}

void AIU_PlayerCharacter::ApplyMovementInput(const FVector2D& MoveAxis)
{
	LastMoveAxisInput = MoveAxis;

	if (FlightComponent)
	{
		FlightComponent->SetMoveAxisInput(MoveAxis);
		if (FlightComponent->IsFlying())
		{
			return;
		}
	}

	if (!Controller || MoveAxis.IsNearlyZero())
	{
		return;
	}

	// Use exact Controller Yaw (matches centered camera view with zero CameraLag delay)
	const FRotator YawRot(0.0f, Controller->GetControlRotation().Yaw, 0.0f);

	const FVector ForwardDir = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
	const FVector RightDir = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);

	AddMovementInput(ForwardDir, MoveAxis.Y);
	AddMovementInput(RightDir, MoveAxis.X);
}

void AIU_PlayerCharacter::ApplyLookInput(const FVector2D& LookAxis)
{
	if (!Controller || LookAxis.IsNearlyZero())
	{
		return;
	}

	AddControllerYawInput(LookAxis.X * LookSensitivity);
	AddControllerPitchInput(LookAxis.Y * LookSensitivity);
}

void AIU_PlayerCharacter::AdjustCameraZoom(float ZoomAxisValue)
{
	if (FMath::IsNearlyZero(ZoomAxisValue))
	{
		return;
	}

	DesiredCameraArmLength = FMath::Clamp(
		DesiredCameraArmLength - (ZoomAxisValue * CameraZoomStep),
		MinCameraArmLength,
		MaxCameraArmLength);

	SetActorTickEnabled(true);
}

void AIU_PlayerCharacter::StartJumpInput()
{
	if (FlightComponent)
	{
		FlightComponent->HandleJumpOrAscendStarted();
	}
	else
	{
		Jump();
	}
}

void AIU_PlayerCharacter::StopJumpInput()
{
	if (FlightComponent)
	{
		FlightComponent->HandleJumpOrAscendCompleted();
	}
	else
	{
		StopJumping();
	}
}

void AIU_PlayerCharacter::StartDescendInput()
{
	if (FlightComponent && FlightComponent->IsFlying())
	{
		FlightComponent->HandleDescendStarted();
	}
	else
	{
		Crouch();
		if (IUMovementComponent)
		{
			IUMovementComponent->RefreshMovementSettings();
		}
	}
}

void AIU_PlayerCharacter::StopDescendInput()
{
	if (FlightComponent)
	{
		FlightComponent->HandleDescendCompleted();
	}
	UnCrouch();
	if (IUMovementComponent)
	{
		IUMovementComponent->RefreshMovementSettings();
	}
}

void AIU_PlayerCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	if (PlaceholderBodyMesh)
	{
		PlaceholderBodyMesh->SetRelativeScale3D(FVector(0.65f, 0.65f, 1.05f));
	}
}

void AIU_PlayerCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	if (PlaceholderBodyMesh)
	{
		PlaceholderBodyMesh->SetRelativeScale3D(FVector(0.65f, 0.65f, 1.65f));
	}
}

void AIU_PlayerCharacter::ToggleFlightInput()
{
	if (FlightComponent)
	{
		FlightComponent->ToggleFlightMode();
	}
}

void AIU_PlayerCharacter::StartSprintInput()
{
	if (IUMovementComponent)
	{
		IUMovementComponent->SetSprinting(true);
	}
	if (FlightComponent)
	{
		FlightComponent->SetFlightDashActive(true);
	}
}

void AIU_PlayerCharacter::StopSprintInput()
{
	if (IUMovementComponent)
	{
		IUMovementComponent->SetSprinting(false);
	}
	if (FlightComponent)
	{
		FlightComponent->SetFlightDashActive(false);
	}
}

void AIU_PlayerCharacter::ApplyVisualFlightLean(const FRotator& LeanRotator)
{
	if (PlaceholderBodyMesh && PlaceholderBodyMesh->IsVisible())
	{
		PlaceholderBodyMesh->SetRelativeRotation(LeanRotator);
	}
	if (GetMesh() && GetMesh()->GetSkeletalMeshAsset() != nullptr)
	{
		// Base skeletal mesh faces +X via -90 Yaw offset
		GetMesh()->SetRelativeRotation(FRotator(LeanRotator.Roll, -90.0f, -LeanRotator.Pitch));
	}
}

void AIU_PlayerCharacter::HandleFlightDashChanged(bool bIsDashing)
{
	bFlightDashCameraActive = bIsDashing;
	SetActorTickEnabled(true);
}

void AIU_PlayerCharacter::ToggleRotationModeInput()
{
	if (!IUMovementComponent)
	{
		return;
	}

	const EIU_CharacterRotationMode NextMode =
		(IUMovementComponent->GetCharacterRotationMode() == EIU_CharacterRotationMode::OrientToMovement)
		? EIU_CharacterRotationMode::OrientToCamera
		: EIU_CharacterRotationMode::OrientToMovement;

	IUMovementComponent->SetCharacterRotationMode(NextMode);
}

void AIU_PlayerCharacter::MeleeAttackInput()
{
	if (CombatComponent)
	{
		CombatComponent->RequestMeleeAttack();
	}
}

void AIU_PlayerCharacter::KiBlastInput()
{
	if (CombatComponent)
	{
		CombatComponent->RequestKiBlast();
	}
}

void AIU_PlayerCharacter::DodgeInput()
{
	if (!CombatComponent)
	{
		return;
	}

	FVector EvadeWorldDir = FVector::ZeroVector;
	if (!LastMoveAxisInput.IsNearlyZero(0.1f) && Controller)
	{
		const FRotator YawRot(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
		const FVector Fwd = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
		const FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);
		EvadeWorldDir = (Fwd * LastMoveAxisInput.Y + Right * LastMoveAxisInput.X).GetSafeNormal();
	}
	CombatComponent->RequestDodge(EvadeWorldDir);
}

void AIU_PlayerCharacter::ToggleLockOnInput()
{
	if (TargetingComponent)
	{
		TargetingComponent->ToggleLockOn();
	}
}

void AIU_PlayerCharacter::SwitchTargetInput(float Direction)
{
	if (TargetingComponent)
	{
		TargetingComponent->SwitchLockedTarget(Direction);
	}
}

void AIU_PlayerCharacter::HandleComboVisualPulse(int32 ComboStep)
{
	if (PlaceholderFacingVisor)
	{
		// Extend forward visor punch indicator dynamically by combo step (1 -> 2 -> 3)
		const float ReachScale = 0.75f + (0.25f * ComboStep);
		PlaceholderFacingVisor->SetRelativeLocation(FVector(65.0f + 12.0f * ComboStep, 0.0f, 28.0f));
		PlaceholderFacingVisor->SetRelativeScale3D(FVector(ReachScale, 0.85f, 0.18f));
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				StrikePulseResetTimerHandle,
				this,
				&AIU_PlayerCharacter::ResetStrikeVisualPulse,
				0.18f,
				false);
		}
	}
}

void AIU_PlayerCharacter::ResetStrikeVisualPulse()
{
	if (PlaceholderFacingVisor)
	{
		PlaceholderFacingVisor->SetRelativeLocation(FVector(42.0f, 0.0f, 32.0f));
		PlaceholderFacingVisor->SetRelativeScale3D(FVector(0.45f, 0.70f, 0.12f));
	}
}

void AIU_PlayerCharacter::UpdatePlaceholderVisibility()
{
	const bool bHasSkeletalMesh = (GetMesh() && GetMesh()->GetSkeletalMeshAsset() != nullptr);
	if (PlaceholderBodyMesh)
	{
		PlaceholderBodyMesh->SetVisibility(!bHasSkeletalMesh, true);
	}
}
