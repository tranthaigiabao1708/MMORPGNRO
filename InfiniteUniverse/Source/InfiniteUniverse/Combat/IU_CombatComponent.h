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
class AIU_AfterimageGhost;
class UAnimMontage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FIU_OnCombatStateChangedSignature, EIU_CombatState, PreviousState, EIU_CombatState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIU_OnComboStepExecutedSignature, int32, ComboStepIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FIU_OnKiBlastFiredSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIU_OnMobilityStyleChangedSignature, EIU_MobilityStepStyle, NewStyle);

/**
 * Prototype Action Combat State Machine (Milestone 3: P3-01 to P3-06).
 * Actor-agnostic for both PvE and PvP:
 * - Melee Combo (1 -> 2 -> 3) registers real windup threats before impact frame so opponents can Perfect-Evade
 * - Q Ki Blast projectile registers live trajectory/hitbox threat
 * - RMB Unified A -> B Mobility:
 *     * Normal traversal (no imminent incoming attack): Earthling Dash OR Alien InstantBlink (0 afterimages)
 *     * Perfect Evade (incoming attack <= 0.30s from dealing damage OR physical hitbox gap < 20 cm):
 *       Leaves Black Myth: Wukong style Afterimage Ghost at A, grants i-frames, and arrives at identical B!
 */
UCLASS(ClassGroup = (InfiniteUniverse), meta = (BlueprintSpawnableComponent))
class INFINITEUNIVERSE_API UIU_CombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIU_CombatComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ========================================================================
	// Tunable Melee & 3-Hit Combo Parameters
	// ========================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Melee")
	TArray<float> ComboStepDamages = { 15.0f, 20.0f, 30.0f };

	/** Windup time (seconds) per combo step from swing start until damage impact frame (enables PvP Perfect Evade!). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Melee")
	TArray<float> ComboStepWindupTimes = { 0.11f, 0.12f, 0.16f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Melee", meta = (ClampMin = "50.0", ClampMax = "600.0"))
	float MeleeReachDistance = 210.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Melee", meta = (ClampMin = "20.0", ClampMax = "250.0"))
	float MeleeHitSweepRadius = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Melee", meta = (ClampMin = "0.08", ClampMax = "1.5"))
	float AttackStepDuration = 0.28f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Melee", meta = (ClampMin = "0.10", ClampMax = "2.0"))
	float ComboResetGraceTime = 0.50f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Melee", meta = (ClampMin = "0.0", ClampMax = "1500.0"))
	float MeleeStepLungeSpeed = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Melee")
	TArray<TObjectPtr<UAnimMontage>> MeleeComboMontages;

	// ========================================================================
	// Tunable Ki Blast Parameters (Key: Q)
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
	// Tunable RMB Mobility & Imminent-Attack Perfect Evade (A -> B)
	// ========================================================================

	/** Archetype traversal style: Earthling continuous Dash vs Alien InstantBlink (both arrive at identical B). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Dodge")
	EIU_MobilityStepStyle MobilityStyle = EIU_MobilityStepStyle::Dash;

	/** Unified A -> B travel distance (cm) shared identically by Dash, InstantBlink, and Perfect Evade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Dodge", meta = (ClampMin = "100.0", ClampMax = "1200.0"))
	float MobilityTravelDistance = 420.0f;

	/** Imminent time-to-damage threshold (seconds) to trigger Perfect Evade + Afterimage Ghost (Default: 0.30s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Dodge", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float PerfectEvadeTimeWindow = 0.30f;

	/** Imminent physical surface gap (cm) between attack hitbox and player hurtbox to trigger Perfect Evade (Default: 20 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Dodge", meta = (ClampMin = "1.0", ClampMax = "100.0"))
	float PerfectEvadePhysicalGapCm = 20.0f;

	/** Duration (seconds) of the Dash travel & Perfect Evade i-frame window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Dodge", meta = (ClampMin = "0.08", ClampMax = "1.0"))
	float DodgeDuration = 0.20f;

	/** Cooldown (seconds) between consecutive RMB mobility activations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Dodge", meta = (ClampMin = "0.10", ClampMax = "5.0"))
	float DodgeCooldown = 0.45f;

	/** Lifetime (seconds) of the translucent phantom afterimage decoy left at origin A during Perfect Evade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Dodge", meta = (ClampMin = "0.15", ClampMax = "2.0"))
	float MirageAfterimageLifetime = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat|Dodge")
	TSubclassOf<AIU_AfterimageGhost> AfterimageGhostClass;

	// ========================================================================
	// Combat Actions & State Queries
	// ========================================================================

	/** Initiates basic melee or queues the next sequential hit in the 1 -> 2 -> 3 combo. */
	UFUNCTION(BlueprintCallable, Category = "IU|Combat")
	bool RequestMeleeAttack();

	/** Fires a Ki Blast projectile if Idle, off cooldown, and player has sufficient Ki. */
	UFUNCTION(BlueprintCallable, Category = "IU|Combat")
	AIU_KiProjectile* RequestKiBlast();

	/** Executes A -> B traversal; upgrades to Wukong Afterimage Perfect Evade ONLY if an attack is about to deal damage. */
	UFUNCTION(BlueprintCallable, Category = "IU|Combat")
	bool RequestDodge(const FVector& DesiredWorldDirection = FVector::ZeroVector);

	UFUNCTION(BlueprintCallable, Category = "IU|Combat")
	void SetMobilityStyle(EIU_MobilityStepStyle NewStyle);

	UFUNCTION(BlueprintCallable, Category = "IU|Combat")
	EIU_MobilityStepStyle ToggleMobilityStyle();

	UFUNCTION(BlueprintPure, Category = "IU|Combat")
	EIU_MobilityStepStyle GetMobilityStyle() const { return MobilityStyle; }

	UFUNCTION(BlueprintPure, Category = "IU|Combat")
	EIU_CombatState GetCombatState() const { return CurrentCombatState; }

	UFUNCTION(BlueprintPure, Category = "IU|Combat")
	int32 GetCurrentComboStep() const { return CurrentComboStep; }

	/** True ONLY when the latest RMB activation evaded an imminent incoming attack (<= 0.30s OR < 20 cm). */
	UFUNCTION(BlueprintPure, Category = "IU|Combat")
	bool IsLastDodgeCombatEvade() const { return bLastDodgeWasCombatEvade; }

	/** True when the latest RMB activation relocated instantaneously (Alien InstantBlink OR Perfect Evade). */
	UFUNCTION(BlueprintPure, Category = "IU|Combat")
	bool DidLastDodgeBlinkInstantly() const { return bLastDodgeBlinkedInstantly; }

	/** Returns the phantom afterimage actor spawned at origin A during the latest Perfect Evade (nullptr otherwise). */
	UFUNCTION(BlueprintPure, Category = "IU|Combat")
	AIU_AfterimageGhost* GetLastSpawnedAfterimage() const { return LastSpawnedAfterimage.Get(); }

	/** Clears active timers, cooldowns, and defeated/stun states back to Idle upon respawn. */
	UFUNCTION(BlueprintCallable, Category = "IU|Combat")
	void ResetCombatState();

	UPROPERTY(BlueprintAssignable, Category = "IU|Combat|Events")
	FIU_OnCombatStateChangedSignature OnCombatStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "IU|Combat|Events")
	FIU_OnComboStepExecutedSignature OnComboStepExecuted;

	UPROPERTY(BlueprintAssignable, Category = "IU|Combat|Events")
	FIU_OnKiBlastFiredSignature OnKiBlastFired;

	UPROPERTY(BlueprintAssignable, Category = "IU|Combat|Events")
	FIU_OnMobilityStyleChangedSignature OnMobilityStyleChanged;

private:
	void EnsureComponentsCached();
	void SetCombatState(EIU_CombatState NewState);
	void ExecuteComboStep(int32 StepIndex);
	void OnMeleeImpactTimerFired();
	void CancelPendingMeleeWindup();
	void PerformMeleeSweep(float StepDamage);
	FVector ComputeCombatAimDirection(const FVector& MuzzleOrigin) const;
	void SetOwnerPawnCollisionIgnored(bool bIgnorePawns);
	void SetDashZeroGravityOverride(bool bEnableZeroGravity);
	AIU_AfterimageGhost* SpawnMirageAfterimageAt(const FTransform& OriginTransform);

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

	TWeakObjectPtr<AIU_AfterimageGhost> LastSpawnedAfterimage;

	EIU_CombatState CurrentCombatState = EIU_CombatState::Idle;
	int32 CurrentComboStep = 0;
	bool bQueuedNextComboHit = false;
	bool bKiBlastOnCooldown = false;
	bool bDodgeOnCooldown = false;
	bool bLastDodgeWasCombatEvade = false;
	bool bLastDodgeBlinkedInstantly = false;
	bool bPawnCollisionTemporarilyIgnored = false;
	bool bDashGravityOverridden = false;
	float SavedDashGravityScale = 1.55f;

	int32 ActiveMeleeThreatId = 0;
	float PendingMeleeStepDamage = 15.0f;

	// Active Earthling kinematic Dash state (Zero-Tick when false)
	bool bActiveKinematicDash = false;
	FVector ActiveDashDirection = FVector::ForwardVector;
	float RemainingDashDistance = 0.0f;
	float ActiveDashSpeed = 2100.0f;

	FTimerHandle MeleeImpactTimerHandle;
	FTimerHandle AttackStepTimerHandle;
	FTimerHandle ComboResetTimerHandle;
	FTimerHandle KiBlastCooldownTimerHandle;
	FTimerHandle DodgeTimerHandle;
	FTimerHandle DodgeCooldownTimerHandle;
};
