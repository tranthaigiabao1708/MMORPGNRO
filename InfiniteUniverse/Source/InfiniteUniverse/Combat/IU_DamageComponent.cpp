// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Combat/IU_DamageComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
#include "Core/IU_GameState.h"
#include "Core/IU_PlayerState.h"
#include "Core/IU_PlayerController.h"
#include "Network/IU_BackendSubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "CoreGlobals.h"
#include "Templates/Atomic.h"
#include "InfiniteUniverse.h"

namespace
{
	static TAtomic<uint64> GServerHitRewardSeq{ 0 };
}

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

	const float MitigatedAmount = UIU_AttributeComponent::ComputeMitigatedDamage(Spec.Amount, AttributeComp->GetDefense());
	const float AppliedDelta = AttributeComp->ModifyHP(-MitigatedAmount);
	if (FMath::IsNearlyZero(AppliedDelta))
	{
		return false;
	}

	FIU_DamageSpec EffectiveSpec = Spec;
	EffectiveSpec.Amount = MitigatedAmount;
	OnDamageReceived.Broadcast(EffectiveSpec, AttributeComp->GetHP());

	const bool bTargetKilledByThisHit = AttributeComp->IsDead();

	// Step 3 Authentic NRO Per-Hit Progression + Step 4 CSPRNG Personal Loot & Vang on Kill:
	// Every valid hit against a monster immediately awards BOTH Điểm Sức mạnh (LV EXP) and Điểm Tiềm năng
	// equal to 1.5x the outgoing damage (EffectiveSpec.Amount) — NOT waiting for the finishing blow!
	if (Owner->IsA<AIU_EnemyCharacter>())
	{
		if (APawn* AttackerPawn = Cast<APawn>(Spec.InstigatorActor.Get()))
		{
			if (AIU_PlayerState* AttackerPS = AttackerPawn->GetPlayerState<AIU_PlayerState>())
			{
				if (bTargetKilledByThisHit)
				{
					AttackerPS->AddEnemyKill();
				}

				// Step 5 Party Synergy & Personal Instanced Loot: collect living Party allies (including self) within 120m (12,000 cm)
				int32 NearbyPartyMembers = 1;
				TArray<APawn*> NearbyPartyPawns;
				NearbyPartyPawns.Add(AttackerPawn);
				if (UWorld* SynergyWorld = GetWorld())
				{
					const FString MyPartyID = AttackerPS->GetPartyID();
					if (!MyPartyID.IsEmpty())
					{
						NearbyPartyPawns.Reset();
						const FVector AttackerPos = AttackerPawn->GetActorLocation();
						for (TActorIterator<AIU_PlayerCharacter> It(SynergyWorld); It; ++It)
						{
							AIU_PlayerCharacter* AllyPawn = *It;
							if (!IsValid(AllyPawn))
							{
								continue;
							}
							const UIU_AttributeComponent* AllyAttr = AllyPawn->GetAttributeComponent();
							if (AllyAttr && AllyAttr->IsDead())
							{
								continue;
							}
							if (const AIU_PlayerState* AllyPS = AllyPawn->GetPlayerState<AIU_PlayerState>())
							{
								if (AllyPS->GetPartyID() == MyPartyID &&
									FVector::DistSquared(AttackerPos, AllyPawn->GetActorLocation()) <= FMath::Square(12000.0))
								{
									NearbyPartyPawns.AddUnique(AllyPawn);
									if (NearbyPartyPawns.Num() >= 5)
									{
										break;
									}
								}
							}
						}
						if (NearbyPartyPawns.IsEmpty())
						{
							NearbyPartyPawns.Add(AttackerPawn);
						}
						NearbyPartyMembers = FMath::Clamp(NearbyPartyPawns.Num(), 1, 5);
					}
				}

				if (AttackerPS->HasValidBackendLease())
				{
					if (UWorld* World = GetWorld())
					{
						if (UGameInstance* GI = World->GetGameInstance())
						{
							if (UIU_BackendSubsystem* Backend = GI->GetSubsystem<UIU_BackendSubsystem>())
							{
								const uint64 HitSeq = ++GServerHitRewardSeq;
								const FString IdemKey = FString::Printf(
									TEXT("hit_%s_ep%lld_seq%llu"),
									*Owner->GetName(),
									AttackerPS->GetActiveFencingEpoch(),
									HitSeq);

								const float OutgoingDmg = EffectiveSpec.Amount;
								TWeakObjectPtr<AIU_PlayerState> WeakAttackerPS(AttackerPS);

								Backend->GrantCharacterEXP(
									AttackerPS->GetPersistedCharacterUUID(),
									AttackerPS->GetActiveFencingEpoch(),
									OutgoingDmg,
									bTargetKilledByThisHit,
									IdemKey,
									[WeakAttackerPS, OutgoingDmg, bTargetKilledByThisHit](bool bSuccess, const FIU_BackendCharacterSummary& Updated, const FString& ErrMsg)
									{
										if (!bSuccess)
										{
											UE_LOG(LogInfiniteUniverse, Warning, TEXT("[Progression] Per-hit GrantCharacterEXP failed: %s"), *ErrMsg);
											return;
										}

										if (AIU_PlayerState* APS = WeakAttackerPS.Get())
										{
											APS->ApplyAuthoritativeEXPUpdate(
												Updated.Level,
												Updated.CurrentExp,
												Updated.NextLevelExp,
												Updated.PotentialPoints,
												Updated.EnemyKills,
												Updated.bLeveledUp);
										}

										UE_LOG(LogInfiniteUniverse, Log,
											TEXT("[Progression] Hit Dmg=%.1f (Kill=%d, PartySynergy=x%.2f) -> '%s' +%lld SucManh & +%lld TiemNang | Lv.%d (%lld/%lld), TN=%lld"),
											OutgoingDmg,
											bTargetKilledByThisHit ? 1 : 0,
											Updated.PartySynergyMult,
											*Updated.CharacterName,
											Updated.ExpGranted,
											Updated.PotentialGranted,
											Updated.Level,
											Updated.CurrentExp,
											Updated.NextLevelExp,
											Updated.PotentialPoints);
									},
									NearbyPartyMembers);

								if (bTargetKilledByThisHit)
								{
									for (APawn* LootPawn : NearbyPartyPawns)
									{
										AIU_PlayerState* LootPS = IsValid(LootPawn) ? LootPawn->GetPlayerState<AIU_PlayerState>() : nullptr;
										if (!LootPS || !LootPS->HasValidBackendLease())
										{
											continue;
										}

										TWeakObjectPtr<AIU_PlayerState> WeakLootPS(LootPS);
										TWeakObjectPtr<AIU_PlayerController> WeakLootPC(Cast<AIU_PlayerController>(LootPawn->GetController()));
										const FString LootIdemKey = FString::Printf(
											TEXT("loot_%s_ep%lld_seq%llu_%s"),
											*Owner->GetName(),
											LootPS->GetActiveFencingEpoch(),
											HitSeq,
											*LootPS->GetPersistedCharacterUUID());

										Backend->RollMonsterLoot(
											LootPS->GetPersistedCharacterUUID(),
											LootPS->GetActiveFencingEpoch(),
											TEXT("mob.prototype_raider"),
											LootIdemKey,
											[WeakLootPS, WeakLootPC](bool bLootOK, const FIU_MobLootDropSummary& LootRes, const FIU_InventorySnapshot& Snap, const FString& LootErr)
											{
												if (!bLootOK)
												{
													UE_LOG(LogInfiniteUniverse, Warning, TEXT("[Loot] RollMonsterLoot failed: %s"), *LootErr);
													return;
												}

												if (AIU_PlayerState* LPS = WeakLootPS.Get())
												{
													LPS->ApplyAuthoritativeEconomySummary(
														Snap.StandardCoin,
														Snap.ElixirCount,
														Snap.CrimsonOreCount,
														Snap.EquippedItems.Num());
												}

												if (AIU_PlayerController* LPC = WeakLootPC.Get())
												{
													LPC->Client_ReceiveInventorySnapshot(Snap);
													LPC->Client_NotifyLootDropped(LootRes.CoinsGranted, LootRes.DroppedItemLabels, LootRes.bBagFullSkipped);
												}

												UE_LOG(LogInfiniteUniverse, Log,
													TEXT("[Loot] Raider defeated -> +%lld Vang (Total=%lld), DroppedItems=%d, UsedBag=%d/%d"),
													LootRes.CoinsGranted,
													LootRes.NewCoinBalance,
													LootRes.DroppedItemLabels.Num(),
													Snap.UsedBagSlots,
													Snap.MaxBagSlots);
											});
									}
								}
							}
						}
					}
				}
				else if (!GIsAutomationTesting)
				{
					AttackerPS->AwardOfflineHitProgress(EffectiveSpec.Amount, NearbyPartyMembers);
					if (bTargetKilledByThisHit)
					{
						AttackerPS->AwardOfflineMobLoot(45);
					}
				}
			}
		}
	}

	bool bTriggeredStun = false;

	// Trigger Hit Reaction & subtle knockback only if target survived the hit
	if (!bTargetKilledByThisHit)
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

		// Record replicated PlayerState PvP kills & deaths on Server Authority
		if (APawn* VictimPawn = Cast<APawn>(Owner))
		{
			if (AIU_PlayerState* VictimPS = VictimPawn->GetPlayerState<AIU_PlayerState>())
			{
				VictimPS->AddDeath();
			}
		}
		if (Owner->IsA<AIU_PlayerCharacter>())
		{
			if (APawn* KillerPawn = Cast<APawn>(Spec.InstigatorActor.Get()))
			{
				if (AIU_PlayerState* KillerPS = KillerPawn->GetPlayerState<AIU_PlayerState>())
				{
					KillerPS->AddPvPKill();
				}
			}
		}
	}

	if (Owner->GetNetMode() != NM_Standalone)
	{
		Multicast_NotifyDamageApplied(EffectiveSpec, AttributeComp->GetHP(), bTriggeredStun);
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
