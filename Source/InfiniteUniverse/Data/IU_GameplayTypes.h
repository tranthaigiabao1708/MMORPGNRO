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
	/** Character smoothly turns toward the direction of movement (Exploration default). */
	OrientToMovement UMETA(DisplayName = "Orient To Movement"),

	/** Character faces the camera forward direction (Strafe / Lock-On ready). */
	OrientToCamera   UMETA(DisplayName = "Orient To Camera")
};
