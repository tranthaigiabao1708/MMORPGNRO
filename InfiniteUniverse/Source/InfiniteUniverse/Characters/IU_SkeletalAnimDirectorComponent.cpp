// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Characters/IU_SkeletalAnimDirectorComponent.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
#include "Movement/IU_FlightComponent.h"
#include "Combat/IU_CombatComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

UIU_SkeletalAnimDirectorComponent::UIU_SkeletalAnimDirectorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UIU_SkeletalAnimDirectorComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshMeshAndBindings();
}

void UIU_SkeletalAnimDirectorComponent::RefreshMeshAndBindings()
{
	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!OwnerChar)
	{
		SetComponentTickEnabled(false);
		return;
	}

	CachedMesh = OwnerChar->GetMesh();
	InitializeStandardAnimeClips();

	if (UIU_CombatComponent* Combat = OwnerChar->FindComponentByClass<UIU_CombatComponent>())
	{
		Combat->OnComboStepExecuted.AddUniqueDynamic(this, &UIU_SkeletalAnimDirectorComponent::HandleComboStep);
		Combat->OnKiBlastFired.AddUniqueDynamic(this, &UIU_SkeletalAnimDirectorComponent::HandleKiBlastFired);
	}
	if (UIU_DamageComponent* Dmg = OwnerChar->FindComponentByClass<UIU_DamageComponent>())
	{
		Dmg->OnHitReactionStarted.AddUniqueDynamic(this, &UIU_SkeletalAnimDirectorComponent::HandleHitReaction);
	}
	if (UIU_AttributeComponent* Attr = OwnerChar->FindComponentByClass<UIU_AttributeComponent>())
	{
		Attr->OnHealthDepleted.AddUniqueDynamic(this, &UIU_SkeletalAnimDirectorComponent::HandleDeath);
	}

	if (CachedMesh && CachedMesh->GetSkeletalMeshAsset() && IdleAnim && !ActiveClip)
	{
		ApplyClipToMesh(IdleAnim, true);
	}
}

void UIU_SkeletalAnimDirectorComponent::ResetAnimDirector()
{
	ActionOverrideEndTime = 0.0;
	bWasAirborneLastFrame = false;
	ActiveClip = nullptr;
	RefreshMeshAndBindings();
	if (CachedMesh && CachedMesh->GetSkeletalMeshAsset() && IdleAnim)
	{
		ApplyClipToMesh(IdleAnim, true);
	}
}

void UIU_SkeletalAnimDirectorComponent::InitializeStandardAnimeClips()
{
	auto TryLoadClip = [](TObjectPtr<UAnimSequence>& Slot, const TCHAR* AssetPath)
	{
		if (!Slot)
		{
			Slot = LoadObject<UAnimSequence>(nullptr, AssetPath);
		}
	};

	TryLoadClip(IdleAnim, TEXT("/Game/Characters/Player/Animations/A_Player_Idle.A_Player_Idle"));
	TryLoadClip(WalkAnim, TEXT("/Game/Characters/Player/Animations/A_Player_Walk.A_Player_Walk"));
	TryLoadClip(RunAnim, TEXT("/Game/Characters/Player/Animations/A_Player_Run.A_Player_Run"));
	TryLoadClip(JumpAnim, TEXT("/Game/Characters/Player/Animations/A_Player_Jump.A_Player_Jump"));
	TryLoadClip(FallAnim, TEXT("/Game/Characters/Player/Animations/A_Player_Fall.A_Player_Fall"));
	TryLoadClip(LandingAnim, TEXT("/Game/Characters/Player/Animations/A_Player_Landing.A_Player_Landing"));
	TryLoadClip(HoverAnim, TEXT("/Game/Characters/Player/Animations/A_Player_Hover.A_Player_Hover"));
	TryLoadClip(FlyForwardAnim, TEXT("/Game/Characters/Player/Animations/A_Player_FlyForward.A_Player_FlyForward"));
	TryLoadClip(FlyBackwardAnim, TEXT("/Game/Characters/Player/Animations/A_Player_FlyBackward.A_Player_FlyBackward"));
	TryLoadClip(Melee01Anim, TEXT("/Game/Characters/Player/Animations/A_Player_Melee_01.A_Player_Melee_01"));
	TryLoadClip(Melee02Anim, TEXT("/Game/Characters/Player/Animations/A_Player_Melee_02.A_Player_Melee_02"));
	TryLoadClip(Melee03Anim, TEXT("/Game/Characters/Player/Animations/A_Player_Melee_03.A_Player_Melee_03"));
	TryLoadClip(KiBlastAnim, TEXT("/Game/Characters/Player/Animations/A_Player_KiBlast.A_Player_KiBlast"));
	TryLoadClip(HitReactionAnim, TEXT("/Game/Characters/Player/Animations/A_Player_HitReaction.A_Player_HitReaction"));
	TryLoadClip(DeathAnim, TEXT("/Game/Characters/Player/Animations/A_Player_Death.A_Player_Death"));
}

bool UIU_SkeletalAnimDirectorComponent::HasLoadedCoreAnimSet() const
{
	return IdleAnim && WalkAnim && RunAnim && JumpAnim && FallAnim && LandingAnim &&
		HoverAnim && FlyForwardAnim && FlyBackwardAnim &&
		Melee01Anim && Melee02Anim && Melee03Anim &&
		KiBlastAnim && HitReactionAnim && DeathAnim;
}

void UIU_SkeletalAnimDirectorComponent::PlayActionClip(UAnimSequence* ActionClip, float DurationHoldSeconds, bool bLoopClip)
{
	if (!CachedMesh || !CachedMesh->GetSkeletalMeshAsset() || !ActionClip)
	{
		return;
	}

	// Yield cleanly if a custom Blueprint AnimBlueprint is driving the mesh
	if (CachedMesh->GetAnimationMode() == EAnimationMode::AnimationBlueprint && CachedMesh->GetAnimClass() != nullptr)
	{
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		ActionOverrideEndTime = World->GetTimeSeconds() + FMath::Max(0.05f, DurationHoldSeconds);
	}
	ApplyClipToMesh(ActionClip, bLoopClip);
}

void UIU_SkeletalAnimDirectorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CachedMesh || !CachedMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	if (CachedMesh->GetAnimationMode() == EAnimationMode::AnimationBlueprint && CachedMesh->GetAnimClass() != nullptr)
	{
		return;
	}

	const AActor* Owner = GetOwner();
	const UIU_AttributeComponent* Attr = Owner ? Owner->FindComponentByClass<UIU_AttributeComponent>() : nullptr;
	if (Attr && Attr->IsDead())
	{
		bWasDeadLastFrame = true;
		if (DeathAnim && ActiveClip != DeathAnim)
		{
			ApplyClipToMesh(DeathAnim, false);
		}
		return;
	}

	if (bWasDeadLastFrame)
	{
		// Deterministic recovery when OnRep_IsDead transitions Dead -> Alive regardless of RPC packet arrival order!
		bWasDeadLastFrame = false;
		ActionOverrideEndTime = 0.0;
		ActiveClip = nullptr;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	if (Now < ActionOverrideEndTime && ActiveClip != nullptr && ActiveClip != DeathAnim)
	{
		return;
	}

	bool bDesiredLoop = true;
	if (UAnimSequence* Desired = EvaluateDesiredLocomotionClip(bDesiredLoop))
	{
		if (Desired != ActiveClip || bDesiredLoop != bActiveClipLooping)
		{
			ApplyClipToMesh(Desired, bDesiredLoop);
		}
	}
}

UAnimSequence* UIU_SkeletalAnimDirectorComponent::EvaluateDesiredLocomotionClip(bool& bOutLoop) const
{
	bOutLoop = true;
	const ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!OwnerChar)
	{
		return IdleAnim;
	}

	// Check Enemy AI Attack Windup state
	if (const AIU_EnemyCharacter* EnemyChar = Cast<AIU_EnemyCharacter>(OwnerChar))
	{
		if (EnemyChar->IsWindingUpAttack() && Melee01Anim)
		{
			bOutLoop = false;
			return Melee01Anim;
		}
	}

	const UCharacterMovementComponent* CMC = OwnerChar->GetCharacterMovement();
	const FVector Vel = OwnerChar->GetVelocity();

	bool bFlying = CMC && (CMC->IsFlying() || CMC->MovementMode == MOVE_Custom);
	if (const AIU_PlayerCharacter* PlayerChar = Cast<AIU_PlayerCharacter>(OwnerChar))
	{
		if (const UIU_FlightComponent* FC = PlayerChar->GetFlightComponent())
		{
			bFlying = FC->IsFlying();
		}
	}

	if (bFlying)
	{
		const_cast<UIU_SkeletalAnimDirectorComponent*>(this)->bWasAirborneLastFrame = true;
		const float SpeedSq = Vel.SizeSquared();
		if (SpeedSq < FMath::Square(45.0f))
		{
			return HoverAnim ? HoverAnim : IdleAnim;
		}
		const FVector LocalVel = OwnerChar->GetActorTransform().InverseTransformVectorNoScale(Vel);
		if (LocalVel.X < -80.0f && FlyBackwardAnim)
		{
			return FlyBackwardAnim;
		}
		return FlyForwardAnim ? FlyForwardAnim : HoverAnim;
	}

	if (CMC && CMC->IsFalling())
	{
		const_cast<UIU_SkeletalAnimDirectorComponent*>(this)->bWasAirborneLastFrame = true;
		if (Vel.Z > 80.0f && JumpAnim)
		{
			bOutLoop = false;
			return JumpAnim;
		}
		return FallAnim ? FallAnim : JumpAnim;
	}

	// Trigger brief Landing clip on touchdown
	if (bWasAirborneLastFrame)
	{
		const_cast<UIU_SkeletalAnimDirectorComponent*>(this)->bWasAirborneLastFrame = false;
		if (LandingAnim)
		{
			if (const UWorld* World = GetWorld())
			{
				const_cast<UIU_SkeletalAnimDirectorComponent*>(this)->ActionOverrideEndTime = World->GetTimeSeconds() + 0.22;
			}
			bOutLoop = false;
			return LandingAnim;
		}
	}

	const float PlanarSpeed = Vel.Size2D();
	if (PlanarSpeed > 620.0f && RunAnim)
	{
		return RunAnim;
	}
	if (PlanarSpeed > 20.0f && WalkAnim)
	{
		return WalkAnim;
	}

	return IdleAnim;
}

void UIU_SkeletalAnimDirectorComponent::ApplyClipToMesh(UAnimSequence* DesiredClip, bool bLoop)
{
	if (!CachedMesh || !DesiredClip)
	{
		return;
	}

	ActiveClip = DesiredClip;
	bActiveClipLooping = bLoop;
	CachedMesh->PlayAnimation(DesiredClip, bLoop);
}

void UIU_SkeletalAnimDirectorComponent::HandleComboStep(int32 StepIdx)
{
	UAnimSequence* Chosen = Melee01Anim;
	if (StepIdx == 2 && Melee02Anim)
	{
		Chosen = Melee02Anim;
	}
	else if (StepIdx >= 3 && Melee03Anim)
	{
		Chosen = Melee03Anim;
	}
	PlayActionClip(Chosen, 0.28f, false);
}

void UIU_SkeletalAnimDirectorComponent::HandleKiBlastFired()
{
	PlayActionClip(KiBlastAnim, 0.28f, false);
}

void UIU_SkeletalAnimDirectorComponent::HandleHitReaction(const FIU_DamageSpec& Spec)
{
	PlayActionClip(HitReactionAnim, FMath::Max(0.20f, Spec.HitStunDuration), false);
}

void UIU_SkeletalAnimDirectorComponent::HandleDeath(AActor* DepletedActor)
{
	PlayActionClip(DeathAnim, 10.0f, false);
}

bool UIU_SkeletalAnimDirectorComponent::EnsureSkeletalMeshSocket(
	USkeletalMesh* TargetMesh,
	FName SocketName,
	FName BoneName,
	FVector RelativeLocation,
	FRotator RelativeRotation)
{
	if (!TargetMesh || SocketName.IsNone() || BoneName.IsNone())
	{
		return false;
	}

	if (USkeletalMeshSocket* Existing = TargetMesh->FindSocket(SocketName))
	{
		Existing->BoneName = BoneName;
		Existing->RelativeLocation = RelativeLocation;
		Existing->RelativeRotation = RelativeRotation;
		return true;
	}

#if WITH_EDITOR
	USkeletalMeshSocket* NewSocket = NewObject<USkeletalMeshSocket>(TargetMesh);
	NewSocket->SocketName = SocketName;
	NewSocket->BoneName = BoneName;
	NewSocket->RelativeLocation = RelativeLocation;
	NewSocket->RelativeRotation = RelativeRotation;
	NewSocket->RelativeScale = FVector::OneVector;
	TargetMesh->AddSocket(NewSocket, /*bAddToSkeleton=*/true);
	TargetMesh->MarkPackageDirty();
	return TargetMesh->FindSocket(SocketName) != nullptr;
#else
	return false;
#endif
}
