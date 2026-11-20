// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IU_AttributeComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FIU_OnAttributeChangedSignature, float, CurrentValue, float, MaxValue, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIU_OnHealthDepletedSignature, AActor*, OwningActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIU_OnAttackPowerChangedSignature, float, NewAttackPower);

/**
 * Event-driven HP and Ki attribute manager (Zero-Tick: uses timer only while regenerating Ki).
 * UI listens strictly to delegates and never mutates attributes directly.
 */
UCLASS(ClassGroup = (InfiniteUniverse), meta = (BlueprintSpawnableComponent))
class INFINITEUNIVERSE_API UIU_AttributeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIU_AttributeComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ========================================================================
	// Tunable Attribute Parameters
	// ========================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_MaxHP, Category = "IU|Attributes", meta = (ClampMin = "1.0", ClampMax = "100000.0"))
	float MaxHP = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_MaxKi, Category = "IU|Attributes", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
	float MaxKi = 100.0f;

	/** Passive Ki regenerated per second when Ki < MaxKi. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Attributes", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float PassiveKiRegenPerSecond = 8.0f;

	/** Outgoing combat damage multiplier attribute (1.0 = baseline 100 AttackPower, scales +0.10 per +10 Sức đánh). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "IU|Attributes", meta = (ClampMin = "0.1", ClampMax = "1000.0"))
	float AttackPowerMultiplier = 1.0f;

	/** Authoritative flat Attack Power ("Sức đánh": starts at 100, +10 per Potential upgrade). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_AttackPower, Category = "IU|Attributes", meta = (ClampMin = "10.0", ClampMax = "100000.0"))
	float AttackPower = 100.0f;

	/** Timer interval for smooth Ki regeneration (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Attributes", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float KiRegenTickInterval = 0.10f;

	// ========================================================================
	// Attribute Operations
	// ========================================================================

	/** Modifies HP by Delta (negative = damage, positive = heal). Returns actual delta applied. */
	UFUNCTION(BlueprintCallable, Category = "IU|Attributes")
	float ModifyHP(float Delta);

	/** Attempts to spend Cost Ki. Returns false without modifying Ki if insufficient or dead. */
	UFUNCTION(BlueprintCallable, Category = "IU|Attributes")
	bool TryConsumeKi(float Cost);

	/** Restores Ki by positive Amount, clamped to MaxKi. */
	UFUNCTION(BlueprintCallable, Category = "IU|Attributes")
	float RestoreKi(float Amount);

	/** Resets HP and Ki to maximum and clears dead flag (used on spawn and enemy reset). */
	UFUNCTION(BlueprintCallable, Category = "IU|Attributes")
	void ResetAttributesToFull();

	/** Server-only: applies authoritative persisted MaxHP/HP, MaxKi/Ki, and AttackPower/Multiplier from Go Backend or Potential upgrade. */
	UFUNCTION(BlueprintCallable, Category = "IU|Attributes")
	void ApplyPersistedVitals(float InMaxHP, float InCurrentHP, float InMaxKi, float InCurrentKi, float InAttackMultiplier, float InAttackPower = -1.0f);

	// ========================================================================
	// Pure Getters
	// ========================================================================

	UFUNCTION(BlueprintPure, Category = "IU|Attributes")
	float GetHP() const { return CurrentHP; }

	UFUNCTION(BlueprintPure, Category = "IU|Attributes")
	float GetMaxHP() const { return MaxHP; }

	UFUNCTION(BlueprintPure, Category = "IU|Attributes")
	float GetHPPercent() const { return MaxHP > 0.0f ? (CurrentHP / MaxHP) : 0.0f; }

	UFUNCTION(BlueprintPure, Category = "IU|Attributes")
	float GetKi() const { return CurrentKi; }

	UFUNCTION(BlueprintPure, Category = "IU|Attributes")
	float GetMaxKi() const { return MaxKi; }

	UFUNCTION(BlueprintPure, Category = "IU|Attributes")
	float GetKiPercent() const { return MaxKi > 0.0f ? (CurrentKi / MaxKi) : 0.0f; }

	UFUNCTION(BlueprintPure, Category = "IU|Attributes")
	bool IsDead() const { return bIsDead; }

	UFUNCTION(BlueprintPure, Category = "IU|Attributes")
	float GetAttackPowerMultiplier() const { return FMath::Max(0.1f, AttackPowerMultiplier); }

	UFUNCTION(BlueprintPure, Category = "IU|Attributes")
	float GetAttackPower() const { return AttackPower; }

	UFUNCTION(BlueprintCallable, Category = "IU|Attributes")
	void SetAttackPowerMultiplier(float NewMultiplier);

	// ========================================================================
	// Events
	// ========================================================================

	UPROPERTY(BlueprintAssignable, Category = "IU|Attributes|Events")
	FIU_OnAttributeChangedSignature OnHPChanged;

	UPROPERTY(BlueprintAssignable, Category = "IU|Attributes|Events")
	FIU_OnAttributeChangedSignature OnKiChanged;

	UPROPERTY(BlueprintAssignable, Category = "IU|Attributes|Events")
	FIU_OnHealthDepletedSignature OnHealthDepleted;

	UPROPERTY(BlueprintAssignable, Category = "IU|Attributes|Events")
	FIU_OnAttackPowerChangedSignature OnAttackPowerChanged;

private:
	void UpdateKiRegenTimerState();
	void OnKiRegenTimerTick();

	UFUNCTION()
	void OnRep_MaxHP(float OldMaxHP);

	UFUNCTION()
	void OnRep_MaxKi(float OldMaxKi);

	UFUNCTION()
	void OnRep_AttackPower(float OldAttackPower);

	UFUNCTION()
	void OnRep_CurrentHP(float OldHP);

	UFUNCTION()
	void OnRep_CurrentKi(float OldKi);

	UFUNCTION()
	void OnRep_IsDead(bool bWasDead);

	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing = OnRep_CurrentHP, Category = "IU|Attributes|State")
	float CurrentHP = 100.0f;

	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing = OnRep_CurrentKi, Category = "IU|Attributes|State")
	float CurrentKi = 100.0f;

	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing = OnRep_IsDead, Category = "IU|Attributes|State")
	bool bIsDead = false;

	FTimerHandle KiRegenTimerHandle;
};
