// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_EnemyCharacter.generated.h"

class UStaticMeshComponent;
class UIU_AttributeComponent;
class UIU_DamageComponent;
class UIU_SkeletalAnimDirectorComponent;
class AIU_EnemyAIController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIU_OnEnemyDefeatedSignature, AIU_EnemyCharacter*, DefeatedEnemy);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIU_OnEnemyResetSignature, AIU_EnemyCharacter*, ResetEnemy);

/**
 * Milestone 4 Enemy AI Character (P4-01, P4-05, P4-06, P4-07, P4-08).
 * Integrates UIU_AttributeComponent (HP/Ki), UIU_DamageComponent (Hit Reaction & Dead guard),
 * UIU_CombatThreatSubsystem (telegraphed strike windup for Player Perfect Evade), and AIU_EnemyAIController.
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_EnemyCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AIU_EnemyCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	// ========================================================================
	// Tunable Detection & Chase Parameters (P4-03, P4-04)
	// ========================================================================

	/** Distance (cm) within which the enemy spots a living hostile player from Idle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Enemy|AI", meta = (ClampMin = "150.0", ClampMax = "10000.0"))
	float DetectionRange = 1100.0f;

	/** Maximum distance (cm) from target or home before disengaging back to Idle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Enemy|AI", meta = (ClampMin = "300.0", ClampMax = "20000.0"))
	float LeashDisengageRange = 2400.0f;

	/** Brief alert duration (seconds) in Detect state before pursuing the player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Enemy|AI", meta = (ClampMin = "0.02", ClampMax = "2.0"))
	float DetectAlertDuration = 0.20f;

	/** Movement speed (cm/s) while chasing a detected player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Enemy|Movement", meta = (ClampMin = "100.0", ClampMax = "3000.0"))
	float ChaseMoveSpeed = 480.0f;

	/** If true, enemy switches to 3D flying locomotion to pursue airborne players. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "IU|Enemy|Movement")
	bool bCanFlyChase = false;

	/** If true, requires unobstructed visibility line-of-sight to acquire target from Idle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Enemy|AI")
	bool bRequireLineOfSight = false;

	// ========================================================================
	// Tunable Attack & Recovery Parameters (P4-05)
	// ========================================================================

	/** Center-to-center distance threshold (cm) to stop chasing and start a melee attack. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Enemy|Combat", meta = (ClampMin = "90.0", ClampMax = "600.0"))
	float AttackRange = 210.0f;

	/** Telegraphed strike windup duration (seconds) registered with UIU_CombatThreatSubsystem. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Enemy|Combat", meta = (ClampMin = "0.10", ClampMax = "2.5"))
	float AttackWindupDuration = 0.45f;

	/** Post-attack vulnerability/recovery pause (seconds) before next action. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Enemy|Combat", meta = (ClampMin = "0.10", ClampMax = "5.0"))
	float AttackRecoveryDuration = 0.65f;

	/** Damage dealt to the player on a connected melee strike. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Enemy|Combat", meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float AttackDamage = 18.0f;

	/** Maximum forward reach (cm) from enemy center to strike impact center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Enemy|Combat", meta = (ClampMin = "80.0", ClampMax = "500.0"))
	float AttackReachCm = 200.0f;

	/** Sphere radius (cm) of the damaging strike hitbox. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Enemy|Combat", meta = (ClampMin = "15.0", ClampMax = "150.0"))
	float AttackHitboxRadius = 52.0f;

	/** Hit-stun applied to the struck target (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Enemy|Combat", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float AttackHitStunDuration = 0.22f;

	/** Automatically equips /Game/Characters/Enemy/Meshes/SK_Enemy at BeginPlay when no custom mesh is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Enemy|Animation")
	bool bAutoLoadDefaultAnimeMesh = true;

	// ========================================================================
	// Combat & Lifecycle Operations
	// ========================================================================

	/** Begins a telegraphed melee strike toward TargetActor; notifies AIController when impact resolves. */
	UFUNCTION(BlueprintCallable, Category = "IU|Enemy|Combat")
	bool StartTelegraphedAttack(AActor* TargetActor);

	/** Immediately cancels any in-flight attack windup and unregisters threat from UIU_CombatThreatSubsystem. */
	UFUNCTION(BlueprintCallable, Category = "IU|Enemy|Combat")
	void CancelActiveAttack();

	/** Restores full HP, collision, movement, and Idle AI state at NewSpawnTransform for repeatable testing (P4-08). */
	UFUNCTION(BlueprintCallable, Category = "IU|Enemy")
	void ResetEnemy(const FTransform& NewSpawnTransform);

	/** Convenience overload resetting enemy back to its original spawn transform. */
	UFUNCTION(BlueprintCallable, Category = "IU|Enemy")
	void ResetEnemyAtHome();

	// ========================================================================
	// Pure Getters
	// ========================================================================

	UFUNCTION(BlueprintPure, Category = "IU|Enemy")
	UIU_AttributeComponent* GetAttributeComponent() const { return AttributeComponent; }

	UFUNCTION(BlueprintPure, Category = "IU|Enemy")
	UIU_DamageComponent* GetDamageComponent() const { return DamageComponent; }

	UFUNCTION(BlueprintPure, Category = "IU|Enemy|Animation")
	UIU_SkeletalAnimDirectorComponent* GetAnimDirectorComponent() const { return AnimDirectorComponent; }

	UFUNCTION(BlueprintPure, Category = "IU|Enemy")
	UStaticMeshComponent* GetPlaceholderBodyMesh() const { return PlaceholderBodyMesh; }

	UFUNCTION(BlueprintPure, Category = "IU|Enemy")
	AIU_EnemyAIController* GetEnemyAIController() const;

	UFUNCTION(BlueprintPure, Category = "IU|Enemy")
	EIU_EnemyAIState GetEnemyAIState() const;

	void SetReplicatedAIState(EIU_EnemyAIState NewState) { ReplicatedAIState = NewState; }

	UFUNCTION(BlueprintPure, Category = "IU|Enemy")
	bool IsDead() const;

	UFUNCTION(BlueprintPure, Category = "IU|Enemy")
	bool IsWindingUpAttack() const { return bWindingUpAttack; }

	UFUNCTION(BlueprintPure, Category = "IU|Enemy")
	FTransform GetHomeSpawnTransform() const { return HomeSpawnTransform; }

	// ========================================================================
	// Events
	// ========================================================================

	UPROPERTY(BlueprintAssignable, Category = "IU|Enemy|Events")
	FIU_OnEnemyDefeatedSignature OnEnemyDefeated;

	UPROPERTY(BlueprintAssignable, Category = "IU|Enemy|Events")
	FIU_OnEnemyResetSignature OnEnemyReset;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Enemy")
	TObjectPtr<UStaticMeshComponent> PlaceholderBodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Enemy")
	TObjectPtr<UStaticMeshComponent> PlaceholderHeadMesh;

	/** Visible advancing fist sphere rendered during telegraphed melee attack windups. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Enemy|Combat")
	TObjectPtr<UStaticMeshComponent> StrikeFistMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Enemy")
	TObjectPtr<UIU_AttributeComponent> AttributeComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Enemy")
	TObjectPtr<UIU_DamageComponent> DamageComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Enemy|Animation")
	TObjectPtr<UIU_SkeletalAnimDirectorComponent> AnimDirectorComponent;

private:
	UFUNCTION()
	void HandleHitReactionStarted(const FIU_DamageSpec& Spec);

	UFUNCTION()
	void HandleHitReactionRecovered();

	UFUNCTION()
	void HandleHealthDepleted(AActor* DepletedActor);

	UFUNCTION()
	void OnRep_WindingUpAttack();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnEnemyReset(FVector NewLocation, FRotator NewRotation);

	void ExecuteAttackImpact();
	void ApplyDefaultMovementConfig();
	void UpdatePlaceholderVisibility();

	FTransform HomeSpawnTransform;
	TWeakObjectPtr<AActor> LastDamageInstigator = nullptr;

	UPROPERTY(Replicated)
	EIU_EnemyAIState ReplicatedAIState = EIU_EnemyAIState::Idle;

	UPROPERTY(ReplicatedUsing = OnRep_WindingUpAttack)
	bool bWindingUpAttack = false;

	int32 ActiveThreatId = 0;

	UPROPERTY(Replicated)
	float ActiveWindupTotal = 0.45f;

	float ActiveWindupElapsed = 0.0f;

	UPROPERTY(Replicated)
	FVector StrikeStartWorldPos = FVector::ZeroVector;

	UPROPERTY(Replicated)
	FVector StrikeImpactWorldPos = FVector::ZeroVector;

	FTimerHandle AttackWindupTimerHandle;
};
