// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Attributes/IU_AttributeComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

UIU_AttributeComponent::UIU_AttributeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UIU_AttributeComponent::BeginPlay()
{
	Super::BeginPlay();
	ResetAttributesToFull();
}

void UIU_AttributeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(KiRegenTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

float UIU_AttributeComponent::ModifyHP(float Delta)
{
	if (bIsDead || FMath::IsNearlyZero(Delta))
	{
		return 0.0f;
	}

	const float OldHP = CurrentHP;
	CurrentHP = FMath::Clamp(CurrentHP + Delta, 0.0f, MaxHP);
	const float ActualDelta = CurrentHP - OldHP;

	if (!FMath::IsNearlyZero(ActualDelta))
	{
		OnHPChanged.Broadcast(CurrentHP, MaxHP, ActualDelta);
	}

	if (CurrentHP <= 0.0f && !bIsDead)
	{
		bIsDead = true;
		UpdateKiRegenTimerState();
		OnHealthDepleted.Broadcast(GetOwner());
	}

	return ActualDelta;
}

bool UIU_AttributeComponent::TryConsumeKi(float Cost)
{
	if (bIsDead || Cost < 0.0f)
	{
		return false;
	}
	if (FMath::IsNearlyZero(Cost))
	{
		return true;
	}
	if (CurrentKi + KINDA_SMALL_NUMBER < Cost)
	{
		return false;
	}

	const float OldKi = CurrentKi;
	CurrentKi = FMath::Clamp(CurrentKi - Cost, 0.0f, MaxKi);
	OnKiChanged.Broadcast(CurrentKi, MaxKi, CurrentKi - OldKi);

	UpdateKiRegenTimerState();
	return true;
}

float UIU_AttributeComponent::RestoreKi(float Amount)
{
	if (bIsDead || Amount <= 0.0f)
	{
		return 0.0f;
	}

	const float OldKi = CurrentKi;
	CurrentKi = FMath::Clamp(CurrentKi + Amount, 0.0f, MaxKi);
	const float ActualDelta = CurrentKi - OldKi;

	if (!FMath::IsNearlyZero(ActualDelta))
	{
		OnKiChanged.Broadcast(CurrentKi, MaxKi, ActualDelta);
	}

	UpdateKiRegenTimerState();
	return ActualDelta;
}

void UIU_AttributeComponent::ResetAttributesToFull()
{
	bIsDead = false;
	CurrentHP = MaxHP;
	CurrentKi = MaxKi;
	UpdateKiRegenTimerState();

	OnHPChanged.Broadcast(CurrentHP, MaxHP, 0.0f);
	OnKiChanged.Broadcast(CurrentKi, MaxKi, 0.0f);
}

void UIU_AttributeComponent::UpdateKiRegenTimerState()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const bool bShouldRegen = !bIsDead && (PassiveKiRegenPerSecond > 0.0f) && (CurrentKi < MaxKi - KINDA_SMALL_NUMBER);
	if (bShouldRegen)
	{
		if (!World->GetTimerManager().IsTimerActive(KiRegenTimerHandle))
		{
			World->GetTimerManager().SetTimer(
				KiRegenTimerHandle,
				this,
				&UIU_AttributeComponent::OnKiRegenTimerTick,
				KiRegenTickInterval,
				true);
		}
	}
	else
	{
		World->GetTimerManager().ClearTimer(KiRegenTimerHandle);
	}
}

void UIU_AttributeComponent::OnKiRegenTimerTick()
{
	if (bIsDead || PassiveKiRegenPerSecond <= 0.0f)
	{
		UpdateKiRegenTimerState();
		return;
	}

	RestoreKi(PassiveKiRegenPerSecond * KiRegenTickInterval);
}
