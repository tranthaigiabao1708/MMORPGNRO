// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Combat/IU_CombatThreatSubsystem.h"
#include "Combat/IU_KiProjectile.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"

int32 UIU_CombatThreatSubsystem::RegisterMeleeAttackWindup(
	AActor* Attacker,
	const FVector& WindupStartWorldPos,
	const FVector& ImpactTargetWorldPos,
	float HitboxRadius,
	float WindupDuration,
	const USceneComponent* OptionalLiveHitboxComp)
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(Attacker) || WindupDuration <= KINDA_SMALL_NUMBER)
	{
		return 0;
	}

	const double Now = World->GetTimeSeconds();
	PurgeExpiredThreats(Now);

	FIU_ActiveMeleeThreat NewThreat;
	NewThreat.ThreatId = NextThreatId++;
	NewThreat.Attacker = Attacker;
	NewThreat.LiveHitboxComponent = OptionalLiveHitboxComp;
	NewThreat.WindupStartWorldPos = WindupStartWorldPos;
	NewThreat.ImpactTargetWorldPos = ImpactTargetWorldPos;
	NewThreat.HitboxRadius = FMath::Max(1.0f, HitboxRadius);
	NewThreat.WindupDuration = WindupDuration;
	NewThreat.StartWorldTime = Now;
	NewThreat.HitWorldTime = Now + (double)WindupDuration;

	ActiveMeleeThreats.Add(NewThreat);
	return NewThreat.ThreatId;
}

void UIU_CombatThreatSubsystem::UnregisterMeleeThreat(int32 ThreatId)
{
	if (ThreatId <= 0)
	{
		return;
	}
	ActiveMeleeThreats.RemoveAllSwap([ThreatId](const FIU_ActiveMeleeThreat& Item)
	{
		return Item.ThreatId == ThreatId;
	});
}

void UIU_CombatThreatSubsystem::CancelThreatsFromAttacker(const AActor* Attacker)
{
	if (!Attacker)
	{
		return;
	}
	ActiveMeleeThreats.RemoveAllSwap([Attacker](const FIU_ActiveMeleeThreat& Item)
	{
		return !Item.Attacker.IsValid() || Item.Attacker.Get() == Attacker;
	});
}

void UIU_CombatThreatSubsystem::RegisterProjectileThreat(AIU_KiProjectile* Projectile)
{
	if (!IsValid(Projectile))
	{
		return;
	}
	ActiveProjectileThreats.AddUnique(Projectile);
}

void UIU_CombatThreatSubsystem::UnregisterProjectileThreat(const AIU_KiProjectile* Projectile)
{
	ActiveProjectileThreats.RemoveAllSwap([Projectile](const TWeakObjectPtr<AIU_KiProjectile>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Projectile;
	});
}

bool UIU_CombatThreatSubsystem::HasImminentThreatAgainstActor(
	const AActor* DefenderActor,
	float MaxTimeWindowSeconds,
	float MaxPhysicalGapCm)
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(DefenderActor))
	{
		return false;
	}

	const double Now = World->GetTimeSeconds();
	PurgeExpiredThreats(Now);

	// 1. Evaluate Active Melee / Skill Windups (Monsters, Bosses, or Rival Players in PvP)
	for (const FIU_ActiveMeleeThreat& Threat : ActiveMeleeThreats)
	{
		const AActor* Attacker = Threat.Attacker.Get();
		if (!CanActorThreatenDefender(Attacker, DefenderActor))
		{
			continue;
		}

		const float TimeRemaining = (float)(Threat.HitWorldTime - Now);
		if (TimeRemaining <= 0.0f)
		{
			continue;
		}

		const float ProgressAlpha = FMath::Clamp(
			1.0f - (TimeRemaining / FMath::Max(Threat.WindupDuration, KINDA_SMALL_NUMBER)),
			0.0f,
			1.0f);

		const FVector CurrentHitboxPos = Threat.LiveHitboxComponent.IsValid()
			? Threat.LiveHitboxComponent->GetComponentLocation()
			: FMath::Lerp(Threat.WindupStartWorldPos, Threat.ImpactTargetWorldPos, ProgressAlpha);

		// Rule B: Physical gap between advancing attack hitbox and Defender hurtbox skin < 20 cm!
		const float CurrentSurfaceGapCm = ComputeHitboxToActorSurfaceGapCm(CurrentHitboxPos, Threat.HitboxRadius, DefenderActor);
		if (CurrentSurfaceGapCm < MaxPhysicalGapCm)
		{
			return true;
		}

		// Rule A: Attack will deal damage within <= 0.30s AND Defender is inside the impact danger zone!
		if (TimeRemaining <= MaxTimeWindowSeconds)
		{
			const float ImpactSurfaceGapCm = ComputeHitboxToActorSurfaceGapCm(Threat.ImpactTargetWorldPos, Threat.HitboxRadius, DefenderActor);
			if (ImpactSurfaceGapCm < MaxPhysicalGapCm)
			{
				return true;
			}
		}
	}

	// 2. Evaluate Active Hostile Projectiles (Enemy blasts OR Rival Player Ki Blasts in PvP)
	for (int32 Idx = ActiveProjectileThreats.Num() - 1; Idx >= 0; --Idx)
	{
		AIU_KiProjectile* Proj = ActiveProjectileThreats[Idx].Get();
		if (!IsValid(Proj) || Proj->IsActorBeingDestroyed())
		{
			ActiveProjectileThreats.RemoveAtSwap(Idx);
			continue;
		}

		if (!CanActorThreatenDefender(Proj->GetShooterActor(), DefenderActor))
		{
			continue;
		}

		const FVector ProjPos = Proj->GetActorLocation();
		const float ProjRadius = Proj->GetCollisionSphere()
			? Proj->GetCollisionSphere()->GetScaledSphereRadius()
			: Proj->CollisionRadius;

		// Rule B: Physical gap between projectile hitbox and Defender hurtbox skin < 20 cm!
		const float CurrentProjGapCm = ComputeHitboxToActorSurfaceGapCm(ProjPos, ProjRadius, DefenderActor);
		if (CurrentProjGapCm < MaxPhysicalGapCm)
		{
			return true;
		}

		// Rule A: Projectile will impact Defender along its velocity vector within <= 0.30s!
		const FVector ProjVel = Proj->GetVelocity();
		const float Speed = ProjVel.Size();
		if (Speed > 10.0f && MaxTimeWindowSeconds > 0.0f)
		{
			const FVector ToDefender = DefenderActor->GetActorLocation() - ProjPos;
			if (FVector::DotProduct(ProjVel, ToDefender) > 0.0f)
			{
				const FVector FutureEndPos = ProjPos + (ProjVel * MaxTimeWindowSeconds);
				const FVector ClosestRayPt = FMath::ClosestPointOnSegment(DefenderActor->GetActorLocation(), ProjPos, FutureEndPos);
				const float ProjectedImpactGapCm = ComputeHitboxToActorSurfaceGapCm(ClosestRayPt, ProjRadius, DefenderActor);
				if (ProjectedImpactGapCm <= 0.0f)
				{
					return true;
				}
			}
		}
	}

	return false;
}

float UIU_CombatThreatSubsystem::ComputeHitboxToActorSurfaceGapCm(
	const FVector& HitboxCenter,
	float HitboxRadius,
	const AActor* DefenderActor)
{
	if (!DefenderActor)
	{
		return FLT_MAX;
	}

	const UCapsuleComponent* Capsule = nullptr;
	if (const ACharacter* DefChar = Cast<ACharacter>(DefenderActor))
	{
		Capsule = DefChar->GetCapsuleComponent();
	}
	else
	{
		Capsule = DefenderActor->FindComponentByClass<UCapsuleComponent>();
	}

	if (Capsule)
	{
		const FVector CapCenter = Capsule->GetComponentLocation();
		const float CapRadius = Capsule->GetScaledCapsuleRadius();
		const float CapHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		const float HalfSegment = FMath::Max(0.0f, CapHalfHeight - CapRadius);
		const FVector UpAxis = Capsule->GetUpVector();

		const FVector SegBottom = CapCenter - (UpAxis * HalfSegment);
		const FVector SegTop = CapCenter + (UpAxis * HalfSegment);
		const FVector ClosestAxisPt = FMath::ClosestPointOnSegment(HitboxCenter, SegBottom, SegTop);

		const float CenterToAxisDist = FVector::Dist(HitboxCenter, ClosestAxisPt);
		return CenterToAxisDist - CapRadius - HitboxRadius;
	}

	return FVector::Dist(HitboxCenter, DefenderActor->GetActorLocation()) - 40.0f - HitboxRadius;
}

bool UIU_CombatThreatSubsystem::CanActorThreatenDefender(const AActor* Attacker, const AActor* Defender)
{
	if (!IsValid(Attacker) || !IsValid(Defender) || Attacker == Defender)
	{
		return false;
	}

	if (const UIU_AttributeComponent* AttackerAttr = Attacker->FindComponentByClass<UIU_AttributeComponent>())
	{
		if (AttackerAttr->IsDead())
		{
			return false;
		}
	}

	return true;
}

void UIU_CombatThreatSubsystem::PurgeExpiredThreats(double CurrentWorldTime)
{
	ActiveMeleeThreats.RemoveAllSwap([CurrentWorldTime](const FIU_ActiveMeleeThreat& Item)
	{
		return !Item.Attacker.IsValid() || CurrentWorldTime > (Item.HitWorldTime + 0.01);
	});
}
