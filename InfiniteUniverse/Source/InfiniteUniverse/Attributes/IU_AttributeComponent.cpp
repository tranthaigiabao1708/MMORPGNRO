// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Attributes/IU_AttributeComponent.h"
#include "InfiniteUniverse.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "TimerManager.h"

UIU_AttributeComponent::UIU_AttributeComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UIU_AttributeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UIU_AttributeComponent, MaxHP);
	DOREPLIFETIME(UIU_AttributeComponent, MaxKi);
	DOREPLIFETIME(UIU_AttributeComponent, AttackPowerMultiplier);
	DOREPLIFETIME(UIU_AttributeComponent, CurrentHP);
	DOREPLIFETIME(UIU_AttributeComponent, CurrentKi);
	DOREPLIFETIME(UIU_AttributeComponent, bIsDead);
}

void UIU_AttributeComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner() || GetOwner()->HasAuthority())
	{
		ResetAttributesToFull();
	}
	else
	{
		OnHPChanged.Broadcast(CurrentHP, MaxHP, 0.0f);
		OnKiChanged.Broadcast(CurrentKi, MaxKi, 0.0f);
	}
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
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		return 0.0f;
	}
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
		UE_LOG(LogIUCombat, Log, TEXT("[%s] HP depleted (0/%.0f) -> Dead state triggered"), *GetNameSafe(GetOwner()), MaxHP);
		OnHealthDepleted.Broadcast(GetOwner());
	}

	return ActualDelta;
}

bool UIU_AttributeComponent::TryConsumeKi(float Cost)
{
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		return false;
	}
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
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		return 0.0f;
	}
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
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		return;
	}

	bIsDead = false;
	CurrentHP = MaxHP;
	CurrentKi = MaxKi;
	UpdateKiRegenTimerState();

	OnHPChanged.Broadcast(CurrentHP, MaxHP, 0.0f);
	OnKiChanged.Broadcast(CurrentKi, MaxKi, 0.0f);
}

void UIU_AttributeComponent::ApplyPersistedVitals(
	float InMaxHP,
	float InCurrentHP,
	float InMaxKi,
	float InCurrentKi,
	float InAttackMultiplier)
{
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		return;
	}
	if (!FMath::IsFinite(InMaxHP) || !FMath::IsFinite(InCurrentHP) ||
		!FMath::IsFinite(InMaxKi) || !FMath::IsFinite(InCurrentKi) || !FMath::IsFinite(InAttackMultiplier))
	{
		return;
	}

	MaxHP = FMath::Clamp(InMaxHP, 1.0f, 100000.0f);
	MaxKi = FMath::Clamp(InMaxKi, 0.0f, 10000.0f);
	CurrentHP = FMath::Clamp(InCurrentHP, 1.0f, MaxHP);
	CurrentKi = FMath::Clamp(InCurrentKi, 0.0f, MaxKi);
	AttackPowerMultiplier = FMath::Clamp(InAttackMultiplier, 0.1f, 100.0f);
	bIsDead = false;

	UpdateKiRegenTimerState();
	OnHPChanged.Broadcast(CurrentHP, MaxHP, 0.0f);
	OnKiChanged.Broadcast(CurrentKi, MaxKi, 0.0f);
}

void UIU_AttributeComponent::UpdateKiRegenTimerState()
{
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		return;
	}

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

void UIU_AttributeComponent::SetAttackPowerMultiplier(float NewMultiplier)
{
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		return;
	}
	AttackPowerMultiplier = FMath::Clamp(NewMultiplier, 0.1f, 100.0f);
}

void UIU_AttributeComponent::OnRep_MaxHP(float OldMaxHP)
{
	OnHPChanged.Broadcast(CurrentHP, MaxHP, 0.0f);
}

void UIU_AttributeComponent::OnRep_MaxKi(float OldMaxKi)
{
	OnKiChanged.Broadcast(CurrentKi, MaxKi, 0.0f);
}

void UIU_AttributeComponent::OnRep_CurrentHP(float OldHP)
{
	OnHPChanged.Broadcast(CurrentHP, MaxHP, CurrentHP - OldHP);
}

void UIU_AttributeComponent::OnRep_CurrentKi(float OldKi)
{
	OnKiChanged.Broadcast(CurrentKi, MaxKi, CurrentKi - OldKi);
}

void UIU_AttributeComponent::OnRep_IsDead(bool bWasDead)
{
	if (!bWasDead && bIsDead)
	{
		OnHealthDepleted.Broadcast(GetOwner());
	}
	else if (bWasDead && !bIsDead)
	{
		if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
		{
			if (UCapsuleComponent* Cap = OwnerChar->GetCapsuleComponent())
			{
				Cap->SetCollisionProfileName(TEXT("Pawn"));
				Cap->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
		}
	}
}
