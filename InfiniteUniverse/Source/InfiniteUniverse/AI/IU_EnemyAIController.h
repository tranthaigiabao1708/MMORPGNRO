// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_EnemyAIController.generated.h"

class AIU_EnemyCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FIU_OnEnemyAIStateChangedSignature, EIU_EnemyAIState, OldState, EIU_EnemyAIState, NewState);

/**
 * Deterministic Milestone 4 Enemy AI Controller (P4-02, P4-03, P4-04, P4-05).
 * Drives the state machine: Idle -> Detect -> Chase -> Attack -> Recover (+ HitReaction & Dead).
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_EnemyAIController : public AAIController
{
	GENERATED_BODY()

public:
	AIU_EnemyAIController();

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void Tick(float DeltaSeconds) override;

	// ========================================================================
	// State Queries & External Notifications
	// ========================================================================

	UFUNCTION(BlueprintPure, Category = "IU|AI")
	EIU_EnemyAIState GetAIState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "IU|AI")
	AActor* GetTargetActor() const { return CurrentTarget.Get(); }

	/** Explicitly assigns or clears a target actor (used for aggro-on-hit or deterministic testing). */
	UFUNCTION(BlueprintCallable, Category = "IU|AI")
	void SetTargetActorOverride(AActor* NewTarget);

	/** Called by AIU_EnemyCharacter when non-lethal hit-stun begins. */
	void NotifyHitReactionStarted(AActor* InstigatorActor);

	/** Called by AIU_EnemyCharacter when hit-stun recovery finishes. */
	void NotifyHitReactionRecovered();

	/** Called by AIU_EnemyCharacter once the telegraphed attack impact sweep resolves. */
	void NotifyAttackImpactFinished();

	/** Called by AIU_EnemyCharacter immediately upon reaching 0 HP. */
	void NotifyEnemyDied();

	/** Resets AI state back to Idle at NewHomeLocation (called on enemy respawn/reset). */
	void ResetToIdle(const FVector& NewHomeLocation);

	UPROPERTY(BlueprintAssignable, Category = "IU|AI|Events")
	FIU_OnEnemyAIStateChangedSignature OnAIStateChanged;

private:
	void TransitionToState(EIU_EnemyAIState NewState);

	void TickIdleState(float DeltaSeconds);
	void TickDetectState(float DeltaSeconds);
	void TickChaseState(float DeltaSeconds);
	void TickAttackState(float DeltaSeconds);
	void TickRecoverState(float DeltaSeconds);

	AActor* ScanForHostileTarget() const;
	bool IsValidHostileTarget(const AActor* Candidate) const;
	bool HasVisibilityToTarget(const AActor* Candidate) const;
	float ComputeEngagementDistance(const AActor* Target) const;
	void FaceTargetSmoothly(const AActor* Target, float DeltaSeconds, float InterpSpeed = 12.0f);

	UPROPERTY(Transient)
	TObjectPtr<AIU_EnemyCharacter> ControlledEnemy = nullptr;

	TWeakObjectPtr<AActor> CurrentTarget = nullptr;
	TWeakObjectPtr<AActor> ExplicitTargetOverride = nullptr;

	EIU_EnemyAIState CurrentState = EIU_EnemyAIState::Idle;
	float StateElapsedSeconds = 0.0f;
	FVector HomeLocation = FVector::ZeroVector;
};
