// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Combat/IU_DamageComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
#include "Core/IU_GameState.h"
#include "Core/IU_PlayerState.h"
#include "Network/IU_BackendSubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "CoreGlobals.h"
#include "InfiniteUniverse.h"

UIU_DamageComponent::UIU_DamageComponent()
{
	SetIsReplicatedByDefault(true);
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
	if (!Owner || !Owner->HasAuthority() || !FMath::IsFinite(Spec.Amount) || Spec.Amount <= 0.0f)
	{
		return false;
	}

	// Prevent self-inflicted damage
	if (Spec.InstigatorActor.Get() == Owner)
	{
		return false;
	}

	// Step 2 Co-op PvE vs Martial Combat Arena PvP Sparring Filter
	if (Owner->IsA<AIU_PlayerCharacter>() && Spec.InstigatorActor.IsValid() && Spec.InstigatorActor->IsA<AIU_PlayerCharacter>())
	{
		if (const UWorld* World = GetWorld())
		{
			if (const AIU_GameState* GS = World->GetGameState<AIU_GameState>())
			{
				if (!GS->ArePlayersAllowedToSpar(Spec.InstigatorActor.Get(), Owner))
				{
					return false;
				}
			}
		}
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

	bool bTriggeredStun = false;

	// Trigger Hit Reaction & subtle knockback only if target survived the hit
	if (!AttributeComp->IsDead())
	{
		if (ACharacter* OwnerChar = Cast<ACharacter>(Owner))
		{
			if (UCharacterMovementComponent* MoveComp = OwnerChar->GetCharacterMovement())
			{
				const FVector CleanHitDir = Spec.HitDirection.ContainsNaN() ? FVector::ZeroVector : Spec.HitDirection;
				const FVector PushDir = CleanHitDir.GetSafeNormal();
				if (!PushDir.IsNearlyZero())
				{
					MoveComp->Velocity += PushDir * HitKnockbackSpeed;
				}
			}
		}

		if (Spec.HitStunDuration > 0.0f)
		{
			bInHitReaction = true;
			bTriggeredStun = true;
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

		// Record replicated PlayerState kills & deaths on Server Authority
		if (APawn* VictimPawn = Cast<APawn>(Owner))
		{
			if (AIU_PlayerState* VictimPS = VictimPawn->GetPlayerState<AIU_PlayerState>())
			{
				VictimPS->AddDeath();
			}
		}
		if (APawn* KillerPawn = Cast<APawn>(Spec.InstigatorActor.Get()))
		{
			if (AIU_PlayerState* KillerPS = KillerPawn->GetPlayerState<AIU_PlayerState>())
			{
				if (Owner->IsA<AIU_PlayerCharacter>())
				{
					KillerPS->AddPvPKill();
				}
				else if (Owner->IsA<AIU_EnemyCharacter>())
				{
					KillerPS->AddEnemyKill();

					// Step 3 Authoritative Progression:
					// Monster kills grant +55 LV EXP (determines Level ONLY — no auto stat boost!) + 350 Potential Points (manual stat upgrades).
					if (KillerPS->HasValidBackendLease())
					{
						if (UWorld* World = GetWorld())
						{
							if (UGameInstance* GI = World->GetGameInstance())
							{
								if (UIU_BackendSubsystem* Backend = GI->GetSubsystem<UIU_BackendSubsystem>())
								{
									const FString IdemKey = FString::Printf(
										TEXT("kill_%s_ep%lld_t%.3f"),
										*Owner->GetName(),
										KillerPS->GetActiveFencingEpoch(),
										World->GetTimeSeconds());

									TWeakObjectPtr<AIU_PlayerState> WeakKillerPS(KillerPS);

									Backend->GrantCharacterEXP(
										KillerPS->GetPersistedCharacterUUID(),
										KillerPS->GetActiveFencingEpoch(),
										55,
										350,
										IdemKey,
										[WeakKillerPS](bool bSuccess, const FIU_BackendCharacterSummary& Updated, const FString& ErrMsg)
										{
											if (!bSuccess)
											{
												UE_LOG(LogInfiniteUniverse, Warning, TEXT("[Progression] GrantCharacterEXP failed: %s"), *ErrMsg);
												return;
											}

											if (AIU_PlayerState* KPS = WeakKillerPS.Get())
											{
												KPS->ApplyAuthoritativeEXPUpdate(
													Updated.Level,
													Updated.CurrentExp,
													Updated.NextLevelExp,
													Updated.PotentialPoints,
													Updated.EnemyKills,
													Updated.bLeveledUp);
											}

											UE_LOG(LogInfiniteUniverse, Log,
												TEXT("[Progression] Character '%s' gained +%lld LV EXP & +%lld Potential -> Lv.%d (%lld/%lld EXP, Potential=%lld, MaxHP=%.0f, Atk=%.0f)"),
												*Updated.CharacterName,
												Updated.ExpGranted,
												Updated.PotentialGranted,
												Updated.Level,
												Updated.CurrentExp,
												Updated.NextLevelExp,
												Updated.PotentialPoints,
												Updated.MaxHP,
												Updated.AttackPower);
										});
								}
							}
						}
					}
					else if (!GIsAutomationTesting)
					{
						KillerPS->AwardOfflineKillProgress(55, 350);
					}
				}
			}
		}
	}

	if (Owner->GetNetMode() != NM_Standalone)
	{
		Multicast_NotifyDamageApplied(Spec, AttributeComp->GetHP(), bTriggeredStun);
	}

	return true;
}

void UIU_DamageComponent::Multicast_NotifyDamageApplied_Implementation(FIU_DamageSpec Spec, float RemainingHP, bool bTriggeredHitStun)
{
	AActor* Owner = GetOwner();
	if (!Owner || Owner->HasAuthority())
	{
		return;
	}

	OnDamageReceived.Broadcast(Spec, RemainingHP);

	if (bTriggeredHitStun && Spec.HitStunDuration > 0.0f)
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

void UIU_DamageComponent::EndHitReaction()
{
	bInHitReaction = false;
	OnHitReactionRecovered.Broadcast();
}

void UIU_DamageComponent::ResetDamageState()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitReactionTimerHandle);
	}
	bInHitReaction = false;
	bIsInvulnerable = false;
}
