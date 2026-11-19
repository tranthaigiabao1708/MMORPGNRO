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
 * Displays visual Hit Reaction recoil, hides/collapses on Defeat, and optionally auto-resets.
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_PracticeDummy : public AActor
{
	GENERATED_BODY()

public:
	AIU_PracticeDummy();

	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Dummy")
	bool bAutoResetAfterDefeat = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Dummy", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float AutoResetDelaySeconds = 3.5f;

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

	FTimerHandle AutoResetTimerHandle;
};
