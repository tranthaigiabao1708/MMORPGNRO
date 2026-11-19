// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Characters/Player/IU_PlayerCharacter.h"
#include "Movement/IU_MovementComponent.h"
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

	// Third-person SpringArm with terrain/wall collision protection
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = DefaultCameraArmLength;
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
		CameraBoom->SocketOffset = CameraSocketOffset;
	}

	UpdatePlaceholderVisibility();
}

void AIU_PlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = FMath::FInterpTo(
			CameraBoom->TargetArmLength,
			DesiredCameraArmLength,
			DeltaTime,
			CameraZoomInterpSpeed);

		if (FMath::IsNearlyEqual(CameraBoom->TargetArmLength, DesiredCameraArmLength, 0.25f))
		{
			CameraBoom->TargetArmLength = DesiredCameraArmLength;
			SetActorTickEnabled(false);
		}
	}
	else
	{
		SetActorTickEnabled(false);
	}
}

void AIU_PlayerCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (IUMovementComponent)
	{
		IUMovementComponent->SetIUMovementMode(EIU_PlayerMovementMode::Grounded);
	}
}

void AIU_PlayerCharacter::ApplyMovementInput(const FVector2D& MoveAxis)
{
	if (!Controller || MoveAxis.IsNearlyZero())
	{
		return;
	}

	// Project movement onto horizontal plane aligned with Camera/Controller Yaw
	const FRotator ControlRot = Controller->GetControlRotation();
	const FRotator YawRot(0.0f, ControlRot.Yaw, 0.0f);

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
	Jump();
}

void AIU_PlayerCharacter::StopJumpInput()
{
	StopJumping();
}

void AIU_PlayerCharacter::StartSprintInput()
{
	if (IUMovementComponent)
	{
		IUMovementComponent->SetSprinting(true);
	}
}

void AIU_PlayerCharacter::StopSprintInput()
{
	if (IUMovementComponent)
	{
		IUMovementComponent->SetSprinting(false);
	}
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

void AIU_PlayerCharacter::UpdatePlaceholderVisibility()
{
	const bool bHasSkeletalMesh = (GetMesh() && GetMesh()->GetSkeletalMeshAsset() != nullptr);
	if (PlaceholderBodyMesh)
	{
		PlaceholderBodyMesh->SetVisibility(!bHasSkeletalMesh, true);
	}
}
