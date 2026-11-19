// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_CharacterAnimInstance.generated.h"

/**
 * C++ Animation Blueprint base class exposing locomotion, 3D flight, and combat state
 * for both AIU_PlayerCharacter and AIU_EnemyCharacter (Milestone 6).
 */
UCLASS()
class INFINITEUNIVERSE_API UIU_CharacterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Animation")
	float GroundSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Animation")
	float TotalSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Animation")
	float VerticalVelocity = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Animation")
	bool bIsMoving = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Animation")
	bool bIsFallingOrJumping = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Animation")
	bool bIsFlying = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Animation")
	bool bIsHovering = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Animation")
	bool bIsFlightDashing = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Animation")
	bool bIsDead = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Animation")
	EIU_CombatState CombatState = EIU_CombatState::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Animation")
	int32 ComboStep = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Animation")
	EIU_EnemyAIState EnemyAIState = EIU_EnemyAIState::Idle;
};
