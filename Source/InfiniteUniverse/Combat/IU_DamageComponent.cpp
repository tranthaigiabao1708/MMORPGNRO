// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Combat/IU_DamageComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

UIU_DamageComponent::UIU_DamageComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UIU_DamageComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* Owner = GetOwner())
	{
		AttributeComp = Owner->FindComponentByClass<UIU_AttributeComponent>();
	}
}

void UIU_DamageComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitReactionTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

bool UIU_DamageComponent::ApplyDamageSpec(const FIU_DamageSpec& Spec)
{
	AActor* Owner = GetOwner();
	if (!Owner || Spec.Amount <= 0.0f)
	{
		return false;
	}

	// Prevent self-inflicted damage
	if (Spec.InstigatorActor.Get() == Owner)
	{
		return false;
	}

	if (!AttributeComp)
	{
		AttributeComp = Owner->FindComponentByClass<UIU_AttributeComponent>();
	}

	// Mandatory Rule: Dead targets and invulnerable (Dodging) targets cannot receive damage
	if (!AttributeComp || AttributeComp->IsDead() || bIsInvulnerable)
	{
		return false;
	}

	const float AppliedDelta = AttributeComp->ModifyHP(-Spec.Amount);
	if (FMath::IsNearlyZero(AppliedDelta))
	{
		return false;
	}

	OnDamageReceived.Broadcast(Spec, AttributeComp->GetHP());

	// Trigger Hit Reaction & subtle knockback only if target survived the hit
	if (!AttributeComp->IsDead())
	{
		if (ACharacter* OwnerChar = Cast<ACharacter>(Owner))
		{
			if (UCharacterMovementComponent* MoveComp = OwnerChar->GetCharacterMovement())
			{
				const FVector PushDir = Spec.HitDirection.GetSafeNormal();
				if (!PushDir.IsNearlyZero())
				{
					MoveComp->Velocity += PushDir * HitKnockbackSpeed;
				}
			}
		}

		if (Spec.HitStunDuration > 0.0f)
		{
			bInHitReaction = true;
			OnHitReactionStarted.Broadcast(Spec);

			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimer(
					HitReactionTimerHandle,
					this,
					&UIU_DamageComponent::EndHitReaction,
					Spec.HitStunDuration,
					false);
			}
		}
	}
	else
	{
		bInHitReaction = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(HitReactionTimerHandle);
		}
	}

	return true;
}

void UIU_DamageComponent::EndHitReaction()
{
	bInHitReaction = false;
	OnHitReactionRecovered.Broadcast();
}
