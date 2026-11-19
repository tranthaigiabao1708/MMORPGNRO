// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_PracticeDummy.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class UIU_AttributeComponent;
class UIU_DamageComponent;

/**
 * Ground or Aerial target dummy for Milestone 3 Combat Prototype testing.
 * Supports optional periodic telegraph strikes (0.50s windup) so players can also practice
 * timing Black Myth: Wukong style Perfect Evades (<= 0.30s before hit OR < 20 cm hitbox gap).
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_PracticeDummy : public AActor
{
	GENERATED_BODY()

public:
	AIU_PracticeDummy();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Dummy")
	bool bAutoResetAfterDefeat = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Dummy", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float AutoResetDelaySeconds = 3.5f;

	/** Optional training mode: periodically executes a 0.50s telegraphed strike toward nearby players. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Dummy|TrainingStriker")
	bool bEnablePeriodicTestStrike = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Dummy|TrainingStriker", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float StrikeIntervalSeconds = 2.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Dummy|TrainingStriker", meta = (ClampMin = "0.15", ClampMax = "2.0"))
	float StrikeWindupDuration = 0.50f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Dummy|TrainingStriker", meta = (ClampMin = "1.0", ClampMax = "200.0"))
	float StrikeDamage = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Dummy|TrainingStriker", meta = (ClampMin = "80.0", ClampMax = "500.0"))
	float StrikeReachCm = 210.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Dummy|TrainingStriker", meta = (ClampMin = "15.0", ClampMax = "120.0"))
	float StrikeHitboxRadius = 46.0f;

	/** Starts a single telegraphed strike aimed toward TargetLocation (can also be called directly in tests). */
	UFUNCTION(BlueprintCallable, Category = "IU|Dummy|TrainingStriker")
	void TriggerTelegraphedStrike(const FVector& TargetWorldLocation, float CustomWindupSeconds = -1.0f);

	UFUNCTION(BlueprintCallable, Category = "IU|Dummy")
	void ResetDummy();

	UFUNCTION(BlueprintPure, Category = "IU|Dummy")
	UIU_AttributeComponent* GetAttributeComponent() const { return AttributeComponent; }

	UFUNCTION(BlueprintPure, Category = "IU|Dummy")
	UIU_DamageComponent* GetDamageComponent() const { return DamageComponent; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Dummy")
	TObjectPtr<UCapsuleComponent> CapsuleCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Dummy")
	TObjectPtr<UStaticMeshComponent> DummyMesh;

	/** Visible advancing fist sphere shown during an active training strike windup. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Dummy|TrainingStriker")
	TObjectPtr<UStaticMeshComponent> StrikeFistMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Dummy")
	TObjectPtr<UIU_AttributeComponent> AttributeComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Dummy")
	TObjectPtr<UIU_DamageComponent> DamageComponent;

private:
	UFUNCTION()
	void HandleHitReactionStarted(const FIU_DamageSpec& Spec);

	UFUNCTION()
	void HandleHitReactionRecovered();

	UFUNCTION()
	void HandleDummyDefeated(AActor* DepletedActor);

	void TryPeriodicTrainingStrike();
	void ExecuteStrikeImpact();
	void CancelActiveStrike();

	bool bWindingUpStrike = false;
	int32 ActiveThreatId = 0;
	float ActiveStrikeWindupTotal = 0.50f;
	float ActiveStrikeElapsed = 0.0f;
	FVector StrikeStartWorldPos = FVector::ZeroVector;
	FVector StrikeImpactWorldPos = FVector::ZeroVector;

	FTimerHandle AutoResetTimerHandle;
	FTimerHandle PeriodicStrikeTimerHandle;
	FTimerHandle StrikeImpactTimerHandle;
};
