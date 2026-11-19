// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_SkeletalAnimDirectorComponent.generated.h"

class UAnimSequence;
class USkeletalMesh;
class USkeletalMeshComponent;

/**
 * Drives the 15 imported anime UAnimSequence clips directly on Player & Enemy SkeletalMeshes
 * when no custom Blueprint AnimBlueprint override is assigned (Milestone 6).
 */
UCLASS(ClassGroup = (InfiniteUniverse), meta = (BlueprintSpawnableComponent))
class INFINITEUNIVERSE_API UIU_SkeletalAnimDirectorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIU_SkeletalAnimDirectorComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Loads standard /Game/Characters/Player/Animations/A_Player_* clips if not already populated. */
	UFUNCTION(BlueprintCallable, Category = "IU|Animation")
	void InitializeStandardAnimeClips();

	/** Refreshes cached SkeletalMeshComponent, delegate bindings, and applies initial Idle pose. */
	UFUNCTION(BlueprintCallable, Category = "IU|Animation")
	void RefreshMeshAndBindings();

	/** Clears action overrides and restores Idle animation upon character respawn/reset. */
	UFUNCTION(BlueprintCallable, Category = "IU|Animation")
	void ResetAnimDirector();

	/** Ensures a named socket exists on TargetMesh (authoring helper in Editor, safe query in Standalone). */
	UFUNCTION(BlueprintCallable, Category = "IU|Animation")
	static bool EnsureSkeletalMeshSocket(
		USkeletalMesh* TargetMesh,
		FName SocketName,
		FName BoneName,
		FVector RelativeLocation = FVector::ZeroVector,
		FRotator RelativeRotation = FRotator::ZeroRotator);

	/** Plays a one-shot action animation (Melee 1..3, KiBlast, HitReaction, Landing, or Death) immediately. */
	UFUNCTION(BlueprintCallable, Category = "IU|Animation")
	void PlayActionClip(UAnimSequence* ActionClip, float DurationHoldSeconds, bool bLoopClip = false);

	UFUNCTION(BlueprintPure, Category = "IU|Animation")
	UAnimSequence* GetActiveClip() const { return ActiveClip; }

	UFUNCTION(BlueprintPure, Category = "IU|Animation")
	bool HasLoadedCoreAnimSet() const;

	// ========================================================================
	// Configurable 15 Anime Prototype Animation Clips
	// ========================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation|Locomotion")
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation|Locomotion")
	TObjectPtr<UAnimSequence> WalkAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation|Locomotion")
	TObjectPtr<UAnimSequence> RunAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation|Jump")
	TObjectPtr<UAnimSequence> JumpAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation|Jump")
	TObjectPtr<UAnimSequence> FallAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation|Jump")
	TObjectPtr<UAnimSequence> LandingAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation|Flight")
	TObjectPtr<UAnimSequence> HoverAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation|Flight")
	TObjectPtr<UAnimSequence> FlyForwardAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation|Flight")
	TObjectPtr<UAnimSequence> FlyBackwardAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation|Combat")
	TObjectPtr<UAnimSequence> Melee01Anim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation|Combat")
	TObjectPtr<UAnimSequence> Melee02Anim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation|Combat")
	TObjectPtr<UAnimSequence> Melee03Anim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation|Combat")
	TObjectPtr<UAnimSequence> KiBlastAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation|Combat")
	TObjectPtr<UAnimSequence> HitReactionAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Animation|Combat")
	TObjectPtr<UAnimSequence> DeathAnim;

private:
	UAnimSequence* EvaluateDesiredLocomotionClip(bool& bOutLoop) const;
	void ApplyClipToMesh(UAnimSequence* DesiredClip, bool bLoop);

	UFUNCTION()
	void HandleComboStep(int32 StepIdx);

	UFUNCTION()
	void HandleKiBlastFired();

	UFUNCTION()
	void HandleHitReaction(const FIU_DamageSpec& Spec);

	UFUNCTION()
	void HandleDeath(AActor* DepletedActor);

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> CachedMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ActiveClip = nullptr;

	bool bActiveClipLooping = true;
	double ActionOverrideEndTime = 0.0;
	bool bWasAirborneLastFrame = false;
};
