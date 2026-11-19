// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Characters/IU_CharacterAnimInstance.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
#include "Movement/IU_FlightComponent.h"
#include "Combat/IU_CombatComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UIU_CharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	APawn* OwningPawn = TryGetPawnOwner();
	if (!OwningPawn)
	{
		return;
	}

	const FVector Vel = OwningPawn->GetVelocity();
	GroundSpeed = Vel.Size2D();
	TotalSpeed = Vel.Size();
	VerticalVelocity = Vel.Z;
	bIsMoving = TotalSpeed > 15.0f;

	if (const ACharacter* Char = Cast<ACharacter>(OwningPawn))
	{
		if (const UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
		{
			bIsFallingOrJumping = CMC->IsFalling();
			bIsFlying = CMC->IsFlying() || (CMC->MovementMode == MOVE_Custom);
		}
	}

	if (const UIU_AttributeComponent* Attr = OwningPawn->FindComponentByClass<UIU_AttributeComponent>())
	{
		bIsDead = Attr->IsDead();
	}

	if (const AIU_PlayerCharacter* PlayerChar = Cast<AIU_PlayerCharacter>(OwningPawn))
	{
		if (const UIU_FlightComponent* FC = PlayerChar->GetFlightComponent())
		{
			bIsFlying = FC->IsFlying();
			bIsHovering = FC->IsHovering();
			bIsFlightDashing = FC->IsFlightDashing();
		}
		if (const UIU_CombatComponent* CC = PlayerChar->GetCombatComponent())
		{
			CombatState = CC->GetCombatState();
			ComboStep = CC->GetCurrentComboStep();
		}
	}
	else if (const AIU_EnemyCharacter* EnemyChar = Cast<AIU_EnemyCharacter>(OwningPawn))
	{
		EnemyAIState = EnemyChar->GetEnemyAIState();
	}
}
