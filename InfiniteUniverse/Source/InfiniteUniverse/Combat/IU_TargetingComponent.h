// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IU_TargetingComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIU_OnTargetLockChangedSignature, AActor*, NewLockedTarget);

/**
 * 3D Target Lock-On Component (Milestone 3: P3-02).
 * Acquires valid living targets in range, supports cycling targets, assists camera framing,
 * and auto-releases when target dies or exits valid range.
 */
UCLASS(ClassGroup = (InfiniteUniverse), meta = (BlueprintSpawnableComponent))
class INFINITEUNIVERSE_API UIU_TargetingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIU_TargetingComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Targeting", meta = (ClampMin = "200.0", ClampMax = "10000.0"))
	float MaxAcquisitionRange = 2800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Targeting", meta = (ClampMin = "300.0", ClampMax = "12000.0"))
	float MaxMaintainLockDistance = 3400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Targeting", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float CameraAssistInterpSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Targeting")
	float TargetAimHeightOffset = 30.0f;

	/** Toggles lock-on: acquires best living target in view, or clears existing lock. */
	UFUNCTION(BlueprintCallable, Category = "IU|Targeting")
	bool ToggleLockOn();

	/** Cycles lock-on among other living targets in range (Direction > 0 = Right, < 0 = Left). */
	UFUNCTION(BlueprintCallable, Category = "IU|Targeting")
	bool SwitchLockedTarget(float Direction);

	/** Explicitly locks onto a specific actor if valid and alive. */
	UFUNCTION(BlueprintCallable, Category = "IU|Targeting")
	bool SetLockedTarget(AActor* CandidateTarget);

	/** Releases current lock-on cleanly. */
	UFUNCTION(BlueprintCallable, Category = "IU|Targeting")
	void ClearLockOn();

	UFUNCTION(BlueprintPure, Category = "IU|Targeting")
	bool IsLockedOn() const { return LockedTarget.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "IU|Targeting")
	AActor* GetLockedTarget() const { return LockedTarget.Get(); }

	UFUNCTION(BlueprintPure, Category = "IU|Targeting")
	FVector GetLockedTargetAimLocation() const;

	/** Finds the closest living combat target within MaxSearchRadius (used for contextual proximity evade). */
	UFUNCTION(BlueprintPure, Category = "IU|Targeting")
	AActor* FindNearestLivingTarget(float MaxSearchRadius) const;

	bool IsValidLockCandidate(const AActor* Candidate) const;

	UPROPERTY(BlueprintAssignable, Category = "IU|Targeting|Events")
	FIU_OnTargetLockChangedSignature OnTargetLockChanged;

private:
	UFUNCTION(Server, Reliable)
	void Server_SyncLockedTarget(AActor* NewTarget);

	UFUNCTION()
	void HandleLockedTargetDepleted(AActor* DepletedActor);

	void GatherValidCandidates(TArray<AActor*>& OutCandidates) const;

	TWeakObjectPtr<AActor> LockedTarget;
};
