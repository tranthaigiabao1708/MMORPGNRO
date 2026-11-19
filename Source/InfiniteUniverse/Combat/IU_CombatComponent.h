// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_CombatComponent.generated.h"

class UIU_AttributeComponent;
class UIU_DamageComponent;
class UIU_TargetingComponent;
class AIU_KiProjectile;
class UAnimMontage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FIU_OnCombatStateChangedSignature, EIU_CombatState, PreviousState, EIU_CombatState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIU_OnComboStepExecutedSignature, int32, ComboStepIndex);

/**
 * Prototype Action Combat State Machine (Milestone 3: P3-01 to P3-06).
 * Supports Ground & Aerial 3-Hit Melee Combo, Ki Blast projectile spawning, and Dodge with i-frames.
 */
UCLASS(ClassGroup = (InfiniteUniverse), meta = (BlueprintSpawnableComponent))
class INFINITEUNIVERSE_API UIU_CombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIU_CombatComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ========================================================================
	// Tunable Melee & 3-Hit Combo Parameters
	// ========================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Melee")
	TArray<float> ComboStepDamages = { 15.0f, 20.0f, 30.0f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Melee", meta = (ClampMin = "50.0", ClampMax = "600.0"))
	float MeleeReachDistance = 210.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Melee", meta = (ClampMin = "20.0", ClampMax = "250.0"))
	float MeleeHitSweepRadius = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Melee", meta = (ClampMin = "0.08", ClampMax = "1.5"))
	float AttackStepDuration = 0.24f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Melee", meta = (ClampMin = "0.10", ClampMax = "2.0"))
	float ComboResetGraceTime = 0.50f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Melee", meta = (ClampMin = "0.0", ClampMax = "1500.0"))
	float MeleeStepLungeSpeed = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Melee")
	TArray<TObjectPtr<UAnimMontage>> MeleeComboMontages;

	// ========================================================================
	// Tunable Ki Blast Parameters
	// ========================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|KiBlast")
	TSubclassOf<AIU_KiProjectile> KiProjectileClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|KiBlast", meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float KiBlastDamage = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|KiBlast", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float KiBlastKiCost = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|KiBlast", meta = (ClampMin = "200.0", ClampMax = "15000.0"))
	float KiBlastSpeed = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|KiBlast", meta = (ClampMin = "0.05", ClampMax = "5.0"))
	float KiBlastCooldown = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|KiBlast")
	FName KiMuzzleSocketName = TEXT("Socket_KiMuzzle_R");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|KiBlast")
	FVector FallbackMuzzleOffset = FVector(75.0f, 22.0f, 25.0f);

	// ========================================================================
	// Tunable Dodge Parameters
	// ========================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Dodge", meta = (ClampMin = "300.0", ClampMax = "5000.0"))
	float DodgeBurstSpeed = 1650.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Dodge", meta = (ClampMin = "0.08", ClampMax = "1.0"))
	float DodgeDuration = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Dodge", meta = (ClampMin = "0.10", ClampMax = "5.0"))
	float DodgeCooldown = 0.50f;

	// ========================================================================
	// Combat Actions & State Queries
	// ========================================================================

	/** Initiates basic melee or queues the next sequential hit in the 1 -> 2 -> 3 combo. */
	UFUNCTION(BlueprintCallable, Category = "IU|Combat")
	bool RequestMeleeAttack();

	/** Fires a Ki Blast projectile if Idle, off cooldown, and player has sufficient Ki. */
	UFUNCTION(BlueprintCallable, Category = "IU|Combat")
	AIU_KiProjectile* RequestKiBlast();

	/** Executes directional Dodge burst (Ground or Air) with temporary invulnerability. */
	UFUNCTION(BlueprintCallable, Category = "IU|Combat")
	bool RequestDodge(const FVector& DesiredWorldDirection = FVector::ZeroVector);

	UFUNCTION(BlueprintPure, Category = "IU|Combat")
	EIU_CombatState GetCombatState() const { return CurrentCombatState; }

	UFUNCTION(BlueprintPure, Category = "IU|Combat")
	int32 GetCurrentComboStep() const { return CurrentComboStep; }

	UPROPERTY(BlueprintAssignable, Category = "IU|Combat|Events")
	FIU_OnCombatStateChangedSignature OnCombatStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "IU|Combat|Events")
	FIU_OnComboStepExecutedSignature OnComboStepExecuted;

private:
	void EnsureComponentsCached();
	void SetCombatState(EIU_CombatState NewState);
	void ExecuteComboStep(int32 StepIndex);
	void PerformMeleeSweep(float StepDamage);
	FVector ComputeCombatAimDirection(const FVector& MuzzleOrigin) const;

	void OnAttackStepTimerFinished();
	void OnComboResetTimerExpired();
	void OnDodgeFinished();
	void ClearKiBlastCooldown();
	void ClearDodgeCooldown();

	UFUNCTION()
	void HandleHitReactionStarted(const FIU_DamageSpec& Spec);

	UFUNCTION()
	void HandleHitReactionRecovered();

	UFUNCTION()
	void HandleOwnerHealthDepleted(AActor* DepletedActor);

	UPROPERTY(Transient)
	TObjectPtr<UIU_AttributeComponent> AttributeComp;

	UPROPERTY(Transient)
	TObjectPtr<UIU_DamageComponent> DamageComp;

	UPROPERTY(Transient)
	TObjectPtr<UIU_TargetingComponent> TargetingComp;

	EIU_CombatState CurrentCombatState = EIU_CombatState::Idle;
	int32 CurrentComboStep = 0; // 0 = None, 1..3 = Active/Recent step
	bool bQueuedNextComboHit = false;
	bool bKiBlastOnCooldown = false;
	bool bDodgeOnCooldown = false;

	FTimerHandle AttackStepTimerHandle;
	FTimerHandle ComboResetTimerHandle;
	FTimerHandle KiBlastCooldownTimerHandle;
	FTimerHandle DodgeTimerHandle;
	FTimerHandle DodgeCooldownTimerHandle;
};
