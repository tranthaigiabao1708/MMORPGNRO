// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_DamageComponent.generated.h"

class UIU_AttributeComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FIU_OnDamageReceivedSignature, const FIU_DamageSpec&, DamageSpec, float, RemainingHP);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIU_OnHitReactionSignature, const FIU_DamageSpec&, DamageSpec);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FIU_OnHitReactionRecoveredSignature);

/**
 * Validates incoming damage, rejects damage to dead/invulnerable actors, applies HP deduction,
 * and drives Hit Reaction recovery timers (Zero-Tick).
 */
UCLASS(ClassGroup = (InfiniteUniverse), meta = (BlueprintSpawnableComponent))
class INFINITEUNIVERSE_API UIU_DamageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIU_DamageComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Damage", meta = (ClampMin = "0.0", ClampMax = "2000.0"))
	float HitKnockbackSpeed = 200.0f;

	/** Applies validated damage. Returns true if damage was accepted and applied. */
	UFUNCTION(BlueprintCallable, Category = "IU|Damage")
	bool ApplyDamageSpec(const FIU_DamageSpec& Spec);

	UFUNCTION(BlueprintCallable, Category = "IU|Damage")
	void SetInvulnerable(bool bNewInvulnerable) { bIsInvulnerable = bNewInvulnerable; }

	UFUNCTION(BlueprintPure, Category = "IU|Damage")
	bool IsInvulnerable() const { return bIsInvulnerable; }

	UFUNCTION(BlueprintPure, Category = "IU|Damage")
	bool IsInHitReaction() const { return bInHitReaction; }

	/** Clears hit-stun timer and invulnerability flag cleanly upon respawn/reset. */
	UFUNCTION(BlueprintCallable, Category = "IU|Damage")
	void ResetDamageState();

	UPROPERTY(BlueprintAssignable, Category = "IU|Damage|Events")
	FIU_OnDamageReceivedSignature OnDamageReceived;

	UPROPERTY(BlueprintAssignable, Category = "IU|Damage|Events")
	FIU_OnHitReactionSignature OnHitReactionStarted;

	UPROPERTY(BlueprintAssignable, Category = "IU|Damage|Events")
	FIU_OnHitReactionRecoveredSignature OnHitReactionRecovered;

private:
	void EndHitReaction();

	UPROPERTY(Transient)
	TObjectPtr<UIU_AttributeComponent> AttributeComp;

	bool bIsInvulnerable = false;
	bool bInHitReaction = false;
	FTimerHandle HitReactionTimerHandle;
};
