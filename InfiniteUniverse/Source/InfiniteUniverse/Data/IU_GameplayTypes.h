// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IU_GameplayTypes.generated.h"

/**
 * Core locomotion mode shared across Ground Movement (M1) and Free 3D Flight (M2).
 */
UENUM(BlueprintType)
enum class EIU_PlayerMovementMode : uint8
{
	Grounded UMETA(DisplayName = "Grounded"),
	Jumping  UMETA(DisplayName = "Jumping / Falling"),
	Flying   UMETA(DisplayName = "Free 3D Flight"),
	Landing  UMETA(DisplayName = "Landing Transition")
};

/**
 * Character yaw orientation behavior on the horizontal plane.
 */
UENUM(BlueprintType)
enum class EIU_CharacterRotationMode : uint8
{
	/** Character smoothly turns toward the direction of movement. */
	OrientToMovement UMETA(DisplayName = "Orient To Movement"),

	/** Character faces the camera forward direction (Default). */
	OrientToCamera   UMETA(DisplayName = "Orient To Camera")
};

/**
 * Archetype mobility style for Right-Mouse-Button (RMB) A -> B traversal:
 * - Dash: Continuous high-speed burst dash from A to B (e.g., Earthling martial artist).
 * - InstantBlink: Instantaneous relocation from A to B (e.g., Off-world / Cosmic race).
 * Note: Both styles cover the exact same A -> B vector & distance; when an enemy is nearby,
 * Combat Evade leaves a Wukong-style phantom afterimage at A and relocates to that exact same B.
 */
UENUM(BlueprintType)
enum class EIU_MobilityStepStyle : uint8
{
	Dash         UMETA(DisplayName = "Dash (Earthling Burst)"),
	InstantBlink UMETA(DisplayName = "Instant Blink (Alien Teleport)")
};

/**
 * Combat state machine states (Milestone 3 Specification).
 */
UENUM(BlueprintType)
enum class EIU_CombatState : uint8
{
	Idle        UMETA(DisplayName = "Idle"),
	Attacking   UMETA(DisplayName = "Attacking"),
	Dodging     UMETA(DisplayName = "Dodging"),
	Blocking    UMETA(DisplayName = "Blocking"),
	HitReaction UMETA(DisplayName = "Hit Reaction"),
	Knockback   UMETA(DisplayName = "Knockback"),
	Defeated    UMETA(DisplayName = "Defeated")
};

/**
 * Enemy AI state machine states (Milestone 4 Specification).
 */
UENUM(BlueprintType)
enum class EIU_EnemyAIState : uint8
{
	Idle        UMETA(DisplayName = "Idle"),
	Detect      UMETA(DisplayName = "Detect Player"),
	Chase       UMETA(DisplayName = "Chase Target"),
	Attack      UMETA(DisplayName = "Attack"),
	Recover     UMETA(DisplayName = "Recover"),
	HitReaction UMETA(DisplayName = "Hit Reaction"),
	Dead        UMETA(DisplayName = "Dead")
};

/**
 * Structured damage payload passed into UIU_DamageComponent.
 */
USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_DamageSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat")
	float Amount = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat")
	TWeakObjectPtr<AActor> InstigatorActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat")
	TWeakObjectPtr<AActor> DamageCauser = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat")
	FVector HitLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat")
	FVector HitDirection = FVector::ForwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Combat")
	float HitStunDuration = 0.22f;
};
