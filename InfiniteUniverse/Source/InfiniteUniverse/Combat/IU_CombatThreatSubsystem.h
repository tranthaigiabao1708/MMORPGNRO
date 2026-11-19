// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "IU_CombatThreatSubsystem.generated.h"

class AIU_KiProjectile;
class USceneComponent;

/**
 * Active melee/ability strike windup registered by ANY combatant (Monster, Boss, or another Player in PvP).
 * Models both time-until-damage and the physical damage hitbox advancing toward its impact point.
 */
USTRUCT()
struct FIU_ActiveMeleeThreat
{
	GENERATED_BODY()

	int32 ThreatId = 0;

	TWeakObjectPtr<AActor> Attacker = nullptr;

	/** Optional live hitbox scene/socket component ("phần chạm vào người chơi thì mất máu"). */
	TWeakObjectPtr<const USceneComponent> LiveHitboxComponent = nullptr;

	/** World-space origin of the damaging limb/weapon at windup start (t = 0). */
	FVector WindupStartWorldPos = FVector::ZeroVector;

	/** World-space center of the damaging hitbox at impact frame (t = WindupDuration). */
	FVector ImpactTargetWorldPos = FVector::ZeroVector;

	/** Physical radius (cm) of the damaging hitbox sphere. */
	float HitboxRadius = 50.0f;

	/** Total windup duration (seconds) from cast/swing start until damage is applied (e.g., 0.50s). */
	float WindupDuration = 0.50f;

	/** World->GetTimeSeconds() timestamp when the windup started. */
	double StartWorldTime = 0.0;

	/** World->GetTimeSeconds() timestamp when the attack deals damage. */
	double HitWorldTime = 0.0;
};

/**
 * Actor-agnostic World Subsystem tracking active incoming attacks (PvE Monsters/Bosses AND PvP Players).
 * Evaluates whether any hostile attack is "imminent" ("sắp gây sát thương") against a defending actor via:
 *   1. Time-to-Impact <= MaxTimeWindowSeconds (default 0.30s) on a trajectory/zone that will hit Defender, OR
 *   2. Physical surface-to-surface gap between the damaging hitbox and Defender's hurtbox < MaxPhysicalGapCm (default 20 cm).
 */
UCLASS()
class INFINITEUNIVERSE_API UIU_CombatThreatSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Registers an active melee/skill windup before its damage frame executes. Returns ThreatId for early cancellation. */
	int32 RegisterMeleeAttackWindup(
		AActor* Attacker,
		const FVector& WindupStartWorldPos,
		const FVector& ImpactTargetWorldPos,
		float HitboxRadius,
		float WindupDuration,
		const USceneComponent* OptionalLiveHitboxComp = nullptr);

	/** Removes a specific threat (e.g., once damage frame has fired). */
	void UnregisterMeleeThreat(int32 ThreatId);

	/** Cancels all active windup threats from an attacker (e.g., when stunned into HitReaction or Defeated). */
	void CancelThreatsFromAttacker(const AActor* Attacker);

	/** Registers an active projectile (Player or Enemy Ki Blast) as a live physical/trajectory threat. */
	void RegisterProjectileThreat(AIU_KiProjectile* Projectile);

	/** Unregisters a projectile upon impact or destruction. */
	void UnregisterProjectileThreat(const AIU_KiProjectile* Projectile);

	/**
	 * Returns true ONLY if at least one hostile attack (from a Monster, Boss, or another Player) is genuinely
	 * about to deal damage to DefenderActor:
	 *   - Time remaining until collision/damage <= MaxTimeWindowSeconds (0.30s), OR
	 *   - Physical gap between attack hitbox outer surface and Defender hurtbox outer surface < MaxPhysicalGapCm (20 cm).
	 */
	bool HasImminentThreatAgainstActor(
		const AActor* DefenderActor,
		float MaxTimeWindowSeconds = 0.30f,
		float MaxPhysicalGapCm = 20.0f);

	/** Computes exact outer-surface-to-outer-surface distance (cm) between a damaging sphere and an actor's hurtbox capsule. */
	static float ComputeHitboxToActorSurfaceGapCm(
		const FVector& HitboxCenter,
		float HitboxRadius,
		const AActor* DefenderActor);

	/** Actor-agnostic hostility check (PvE & PvP ready; excludes self and dead/invalid actors). */
	static bool CanActorThreatenDefender(const AActor* Attacker, const AActor* Defender);

private:
	void PurgeExpiredThreats(double CurrentWorldTime);

	UPROPERTY(Transient)
	TArray<FIU_ActiveMeleeThreat> ActiveMeleeThreats;

	TArray<TWeakObjectPtr<AIU_KiProjectile>> ActiveProjectileThreats;

	int32 NextThreatId = 1;
};
