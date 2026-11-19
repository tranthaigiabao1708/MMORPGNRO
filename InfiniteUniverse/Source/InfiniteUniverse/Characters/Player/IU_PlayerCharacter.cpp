// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Characters/Player/IU_PlayerCharacter.h"
#include "Movement/IU_MovementComponent.h"
#include "Movement/IU_FlightComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Combat/IU_TargetingComponent.h"
#include "Combat/IU_CombatComponent.h"
#include "Characters/IU_SkeletalAnimDirectorComponent.h"
#include "Engine/SkeletalMesh.h"
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
	bReplicates = true;
	SetReplicateMovement(true);

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
	AnimDirectorComponent = CreateDefaultSubobject<UIU_SkeletalAnimDirectorComponent>(TEXT("AnimDirectorComponent"));

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

void AIU_PlayerCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (AttributeComponent)
	{
		AttributeComponent->OnHealthDepleted.AddUniqueDynamic(this, &AIU_PlayerCharacter::HandlePlayerDefeated);
	}
}

void AIU_PlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	InitialSpawnTransform = GetActorTransform();
	bHasSavedInitialSpawn = true;

	// Auto-migrate legacy M1 Blueprint CDO camera values (360 / Z=55 / Z=20 / FOV=90) to Genshin-elevated profile
	if (FMath::IsNearlyEqual(DefaultCameraArmLength, 360.0f, 0.5f))
	{
		DefaultCameraArmLength = 420.0f;
	}
	if (CameraTargetOffset.Equals(FVector(0.0f, 0.0f, 55.0f), 0.5f))
	{
		CameraTargetOffset = FVector(0.0f, 0.0f, 68.0f);
	}
	if (CameraSocketOffset.Equals(FVector(0.0f, 0.0f, 20.0f), 0.5f))
	{
		CameraSocketOffset = FVector(0.0f, 0.0f, 52.0f);
	}
	if (FMath::IsNearlyEqual(DefaultCameraFOV, 90.0f, 0.5f))
	{
		DefaultCameraFOV = 86.0f;
	}

	DesiredCameraArmLength = FMath::Clamp(DefaultCameraArmLength, MinCameraArmLength, MaxCameraArmLength);
	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = DesiredCameraArmLength;
		CameraBoom->TargetOffset = CameraTargetOffset;
		CameraBoom->SocketOffset = CameraSocketOffset;
		CameraBoom->TickComponent(0.0f, LEVELTICK_All, nullptr);
	}
	if (FollowCamera)
	{
		FollowCamera->SetFieldOfView(DefaultCameraFOV);
	}
	if (FlightComponent)
	{
		FlightComponent->OnFlightDashChanged.AddUniqueDynamic(this, &AIU_PlayerCharacter::HandleFlightDashChanged);
	}
	if (CombatComponent)
	{
		CombatComponent->OnComboStepExecuted.AddUniqueDynamic(this, &AIU_PlayerCharacter::HandleComboVisualPulse);
		CombatComponent->OnKiBlastFired.AddUniqueDynamic(this, &AIU_PlayerCharacter::HandleKiBlastCameraKick);
		CombatComponent->OnCombatStateChanged.AddUniqueDynamic(this, &AIU_PlayerCharacter::HandleCombatStateVisuals);
	}
	if (AttributeComponent)
	{
		AttributeComponent->OnHealthDepleted.AddUniqueDynamic(this, &AIU_PlayerCharacter::HandlePlayerDefeated);
	}

	if (bAutoLoadDefaultAnimeMesh && GetMesh() && !GetMesh()->GetSkeletalMeshAsset())
	{
		if (USkeletalMesh* DefaultAnimeMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Player/Meshes/SK_Player.SK_Player")))
		{
			GetMesh()->SetSkeletalMeshAsset(DefaultAnimeMesh);
		}
	}

	UpdatePlaceholderVisibility();
	if (AnimDirectorComponent)
	{
		AnimDirectorComponent->RefreshMeshAndBindings();
	}
}

void AIU_PlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		TM.ClearTimer(StrikePulseResetTimerHandle);
		TM.ClearTimer(VanishFlickerTimerHandle);
		TM.ClearTimer(PlayerAutoRespawnTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AIU_PlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ActiveCameraFOVKick = FMath::FInterpTo(ActiveCameraFOVKick, 0.0f, DeltaTime, 14.0f);
	if (FMath::Abs(ActiveCameraFOVKick) < 0.05f)
	{
		ActiveCameraFOVKick = 0.0f;
	}

	const bool bGroundSprinting = (IUMovementComponent && IUMovementComponent->IsSprinting() &&
		!bFlightDashCameraActive && GetVelocity().SizeSquared2D() > FMath::Square(120.0f));

	const float TargetArm = DesiredCameraArmLength + (bFlightDashCameraActive ? FlightDashArmLengthOffset : 0.0f);
	const float BaseFOV = bFlightDashCameraActive
		? FlightDashCameraFOV
		: (bGroundSprinting ? SprintCameraFOV : DefaultCameraFOV);
	const float TargetFOV = FMath::Clamp(BaseFOV + ActiveCameraFOVKick, 65.0f, 125.0f);

	// Pitch-adaptive vertical socket framing: keeps exact Z=52 inside [-12..+12] deg, lifts on steep downward aim
	float DesiredSocketZ = CameraSocketOffset.Z;
	if (bEnablePitchAdaptiveFraming && Controller)
	{
		const float NormPitch = FRotator::NormalizeAxis(Controller->GetControlRotation().Pitch);
		if (NormPitch < -12.0f)
		{
			const float Alpha = FMath::Clamp((-NormPitch - 12.0f) / 48.0f, 0.0f, 1.0f);
			DesiredSocketZ = CameraSocketOffset.Z + (16.0f * Alpha);
		}
		else if (NormPitch > 12.0f)
		{
			const float Alpha = FMath::Clamp((NormPitch - 12.0f) / 48.0f, 0.0f, 1.0f);
			DesiredSocketZ = CameraSocketOffset.Z - (16.0f * Alpha);
		}
	}

	bool bArmSettled = true;
	bool bSocketSettled = true;
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

		const float NewSocketZ = FMath::FInterpTo(CameraBoom->SocketOffset.Z, DesiredSocketZ, DeltaTime, 10.0f);
		CameraBoom->SocketOffset.Z = NewSocketZ;
		bSocketSettled = FMath::IsNearlyEqual(NewSocketZ, DesiredSocketZ, 0.20f);
		if (bSocketSettled)
		{
			CameraBoom->SocketOffset.Z = DesiredSocketZ;
		}
	}

	if (FollowCamera)
	{
		const float NewFOV = FMath::FInterpTo(FollowCamera->FieldOfView, TargetFOV, DeltaTime, 12.0f);
		FollowCamera->SetFieldOfView(NewFOV);
		bFOVSettled = FMath::IsNearlyEqual(NewFOV, TargetFOV, 0.15f);
		if (bFOVSettled)
		{
			FollowCamera->SetFieldOfView(TargetFOV);
		}
	}

	if (bArmSettled && bSocketSettled && bFOVSettled && FMath::IsNearlyZero(ActiveCameraFOVKick) && !bGroundSprinting)
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
	if (AttributeComponent && AttributeComponent->IsDead())
	{
		LastMoveAxisInput = FVector2D::ZeroVector;
		if (FlightComponent)
		{
			FlightComponent->SetMoveAxisInput(FVector2D::ZeroVector);
		}
		return;
	}

	LastMoveAxisInput = MoveAxis;

	if (FlightComponent)
	{
		FlightComponent->SetMoveAxisInput(MoveAxis);
		if (FlightComponent->IsFlying())
		{
			// Stream 3D flight directional intent via CMC Acceleration so FSavedMove sends it to Dedicated Server!
			if (Controller && !MoveAxis.IsNearlyZero())
			{
				const FRotator ViewRot = Controller->GetControlRotation();
				const FVector CamFwd3D = ViewRot.Vector();
				const FVector CamRight2D = FRotationMatrix(FRotator(0.0f, ViewRot.Yaw, 0.0f)).GetUnitAxis(EAxis::Y);
				AddMovementInput(CamFwd3D, MoveAxis.Y);
				AddMovementInput(CamRight2D, MoveAxis.X);
			}
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
	if (bEnablePitchAdaptiveFraming)
	{
		SetActorTickEnabled(true);
	}
}

void AIU_PlayerCharacter::TriggerCameraImpactKick(float FOVDeltaDegrees)
{
	ActiveCameraFOVKick = FMath::Clamp(ActiveCameraFOVKick + FOVDeltaDegrees, -6.0f, 8.0f);
	SetActorTickEnabled(true);
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

bool AIU_PlayerCharacter::GetCameraAimRay(FVector& OutCameraOrigin, FVector& OutCameraDirection) const
{
	const FRotator AimRot = Controller ? Controller->GetControlRotation() : GetActorRotation();
	OutCameraDirection = AimRot.Vector();

	if (CameraBoom && FollowCamera)
	{
		// Compute exact elevated lens location even if SpringArm has not ticked yet this frame
		if (CameraBoom->IsRegistered())
		{
			const FVector PivotPos = CameraBoom->GetComponentLocation() + CameraTargetOffset;
			const FVector ArmTip = PivotPos - (OutCameraDirection * CameraBoom->TargetArmLength);
			const FVector SocketWorld = ArmTip + FQuat(AimRot).RotateVector(CameraSocketOffset);
			const FVector LiveCamLoc = FollowCamera->GetComponentLocation();

			// Use live SpringArm-collided camera position once initialized; fallback to analytical elevated lens
			OutCameraOrigin = (FVector::DistSquared(LiveCamLoc, GetActorLocation()) > 100.0)
				? LiveCamLoc
				: SocketWorld;
			return true;
		}
	}

	OutCameraOrigin = GetPawnViewLocation();
	return false;
}

void AIU_PlayerCharacter::StartJumpInput()
{
	if ((AttributeComponent && AttributeComponent->IsDead()) ||
		(DamageComponent && DamageComponent->IsInHitReaction()))
	{
		return;
	}

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
	if (AttributeComponent && AttributeComponent->IsDead())
	{
		return;
	}

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
	if ((AttributeComponent && AttributeComponent->IsDead()) ||
		(DamageComponent && DamageComponent->IsInHitReaction()))
	{
		return;
	}

	if (FlightComponent)
	{
		FlightComponent->ToggleFlightMode();
	}
}

void AIU_PlayerCharacter::StartSprintInput()
{
	if (AttributeComponent && AttributeComponent->IsDead())
	{
		return;
	}

	if (IUMovementComponent)
	{
		IUMovementComponent->SetSprinting(true);
	}
	if (FlightComponent)
	{
		FlightComponent->SetFlightDashActive(true);
	}
	SetActorTickEnabled(true);
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
	SetActorTickEnabled(true);
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
	if (!HasAuthority())
	{
		Server_SetCharacterRotationMode(NextMode);
	}
}

void AIU_PlayerCharacter::Server_SetCharacterRotationMode_Implementation(EIU_CharacterRotationMode NewMode)
{
	if (IUMovementComponent)
	{
		IUMovementComponent->SetCharacterRotationMode(NewMode);
	}
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
		const bool bFlying3D = (FlightComponent && FlightComponent->IsFlying());
		const FRotator RefRot = bFlying3D
			? Controller->GetControlRotation()
			: FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
		const FVector Fwd = FRotationMatrix(RefRot).GetUnitAxis(EAxis::X);
		const FVector Right = FRotationMatrix(RefRot).GetUnitAxis(EAxis::Y);
		EvadeWorldDir = (Fwd * LastMoveAxisInput.Y + Right * LastMoveAxisInput.X).GetSafeNormal();
	}
	CombatComponent->RequestDodge(EvadeWorldDir);
}

void AIU_PlayerCharacter::ToggleMobilityStyleInput()
{
	if (CombatComponent)
	{
		CombatComponent->ToggleMobilityStyle();
	}
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
	TriggerCameraImpactKick(ComboStep >= 3 ? -3.0f : -1.2f);

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

void AIU_PlayerCharacter::HandleKiBlastCameraKick()
{
	TriggerCameraImpactKick(2.0f);
}

void AIU_PlayerCharacter::HandleCombatStateVisuals(EIU_CombatState PreviousState, EIU_CombatState NewState)
{
	if (NewState == EIU_CombatState::Dodging)
	{
		const bool bCombatEvade = (CombatComponent && CombatComponent->IsLastDodgeCombatEvade());
		TriggerCameraImpactKick(bCombatEvade ? 4.8f : 1.6f);

		const bool bInstantBlinkOrMirage = (CombatComponent && CombatComponent->DidLastDodgeBlinkInstantly());
		if (bInstantBlinkOrMirage)
		{
			// Alien InstantBlink OR Wukong Mirage Evade: real body vanishes for 0.065s while phantom afterimage remains at A!
			if (PlaceholderBodyMesh)
			{
				PlaceholderBodyMesh->SetVisibility(false, true);
			}
			if (GetMesh() && GetMesh()->GetSkeletalMeshAsset() != nullptr)
			{
				GetMesh()->SetVisibility(false, true);
			}
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimer(
					VanishFlickerTimerHandle,
					this,
					&AIU_PlayerCharacter::RestoreVanishVisibility,
					0.065f,
					false);
			}
		}
		else
		{
			// Earthling Dash: body stays 100% visible, leaning forward aggressively as it streaks A -> B!
			ApplyVisualFlightLean(FRotator(-22.0f, 0.0f, 0.0f));
		}
	}
	else if (PreviousState == EIU_CombatState::Dodging)
	{
		RestoreVanishVisibility();
		if (!FlightComponent || !FlightComponent->IsFlying())
		{
			ApplyVisualFlightLean(FRotator::ZeroRotator);
		}
	}
}

void AIU_PlayerCharacter::RestoreVanishVisibility()
{
	const bool bHasSkeletalMesh = (GetMesh() && GetMesh()->GetSkeletalMeshAsset() != nullptr);
	if (bHasSkeletalMesh)
	{
		GetMesh()->SetVisibility(true, true);
	}
	UpdatePlaceholderVisibility();
}

void AIU_PlayerCharacter::UpdatePlaceholderVisibility()
{
	const bool bHasSkeletalMesh = (GetMesh() && GetMesh()->GetSkeletalMeshAsset() != nullptr);
	if (PlaceholderBodyMesh)
	{
		PlaceholderBodyMesh->SetVisibility(!bHasSkeletalMesh, true);
	}
}

void AIU_PlayerCharacter::HandlePlayerDefeated(AActor* DepletedActor)
{
	LastMoveAxisInput = FVector2D::ZeroVector;

	if (FlightComponent)
	{
		FlightComponent->SetMoveAxisInput(FVector2D::ZeroVector);
		FlightComponent->HandleJumpOrAscendCompleted();
		FlightComponent->HandleDescendCompleted();
		FlightComponent->SetFlightDashActive(false);
		FlightComponent->ExitFlight(false);
	}

	if (TargetingComponent)
	{
		TargetingComponent->ClearLockOn();
	}

	if (IUMovementComponent)
	{
		IUMovementComponent->SetSprinting(false);
		IUMovementComponent->StopMovementImmediately();
		IUMovementComponent->DisableMovement();
	}

	const bool bHasSkeletalMesh = (GetMesh() && GetMesh()->GetSkeletalMeshAsset() != nullptr);
	if (!bHasSkeletalMesh && PlaceholderBodyMesh)
	{
		PlaceholderBodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -66.0f));
		PlaceholderBodyMesh->SetRelativeRotation(FRotator(-85.0f, 0.0f, 0.0f));
		PlaceholderBodyMesh->SetRelativeScale3D(FVector(0.65f, 0.65f, 0.45f));
	}

	if (bAutoRespawnOnDefeat && HasAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				PlayerAutoRespawnTimerHandle,
				this,
				&AIU_PlayerCharacter::ResetPlayerAtSpawn,
				FMath::Max(0.2f, PlayerRespawnDelaySeconds),
				false);
		}
	}
}

void AIU_PlayerCharacter::UpdateSavedSpawnTransform(const FTransform& NewTransform)
{
	InitialSpawnTransform = NewTransform;
	bHasSavedInitialSpawn = true;
}

void AIU_PlayerCharacter::ResetPlayerAtSpawn()
{
	if (!HasAuthority())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlayerAutoRespawnTimerHandle);
	}

	if (AttributeComponent)
	{
		AttributeComponent->ResetAttributesToFull();
	}
	if (DamageComponent)
	{
		DamageComponent->ResetDamageState();
	}
	if (CombatComponent)
	{
		CombatComponent->ResetCombatState();
	}
	if (FlightComponent)
	{
		FlightComponent->SetMoveAxisInput(FVector2D::ZeroVector);
		FlightComponent->HandleJumpOrAscendCompleted();
		FlightComponent->HandleDescendCompleted();
		FlightComponent->SetFlightDashActive(false);
		FlightComponent->ExitFlight(false);
	}

	const FVector RespawnLoc = bHasSavedInitialSpawn ? InitialSpawnTransform.GetLocation() : GetActorLocation();
	const FRotator RespawnRot = bHasSavedInitialSpawn ? InitialSpawnTransform.GetRotation().Rotator() : GetActorRotation();
	if (bHasSavedInitialSpawn)
	{
		TeleportTo(RespawnLoc, RespawnRot, false, true);
	}

	Multicast_OnPlayerRespawned(RespawnLoc, RespawnRot);
}

void AIU_PlayerCharacter::Multicast_OnPlayerRespawned_Implementation(FVector RespawnLocation, FRotator RespawnRotation)
{
	if (!HasAuthority())
	{
		SetActorLocationAndRotation(RespawnLocation, RespawnRotation, false, nullptr, ETeleportType::TeleportPhysics);
	}

	UnCrouch();
	if (IUMovementComponent)
	{
		IUMovementComponent->SetSprinting(false);
		IUMovementComponent->StopMovementImmediately();
		IUMovementComponent->SetMovementMode(MOVE_Walking);
		IUMovementComponent->SetIUMovementMode(EIU_PlayerMovementMode::Grounded);
		IUMovementComponent->RefreshMovementSettings();
	}

	if (PlaceholderBodyMesh)
	{
		PlaceholderBodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -5.0f));
		PlaceholderBodyMesh->SetRelativeRotation(FRotator::ZeroRotator);
		PlaceholderBodyMesh->SetRelativeScale3D(FVector(0.65f, 0.65f, 1.65f));
	}

	ApplyVisualFlightLean(FRotator::ZeroRotator);
	RestoreVanishVisibility();

	if (AnimDirectorComponent)
	{
		AnimDirectorComponent->ResetAnimDirector();
	}
}
