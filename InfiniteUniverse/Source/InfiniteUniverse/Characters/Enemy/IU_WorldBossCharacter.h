// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
#include "IU_WorldBossCharacter.generated.h"

UENUM(BlueprintType)
enum class EIU_BossSpecialAction : uint8
{
	None         = 0,
	GroundQuake  = 1, // 360-deg ground shockwave: players flying > SafeAltitudeZAboveGroundCm (450cm) dodge 100%!
	SweepingBeam = 2, // Phase 2+ 180-deg forward high-energy beam
	SkyLeapSlam  = 3  // Phase 2+ anti-air leap slam
};

struct FIU_BossDamageEntry
{
	FString CharacterID;
	FString CharacterName;
	int64 FencingEpoch = 0;
	double DamageDealt = 0.0;
};

/**
 * Step 6 3-Phase World Boss ("Highland Titan" — boss.highland_titan_01):
 * - Phase 1 (100% -> 50% HP): 1.00x Dmg/Speed + Ground Quake (360-deg, SafeAltitude > 450cm / 4.5m in 3D Flight!)
 * - Phase 2 (50% -> 20% HP) : 1.15x Dmg, 1.20x Speed + Sweeping Ki Beam & Volcanic Quake
 * - Phase 3 (<= 20% HP)     : 1.30x Dmg, 1.35x Speed ("CUONG NO HUY DIET" Overdrive Aura)
 * - Replicated Poise (1200 MaxPoise) & Stagger (6.0s at 1.25x vulnerability + 8.0s recovery immunity)
 * - Server-authoritative per-player damage contribution table -> settles >= 2% personal loot & boss_pity_tokens!
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_WorldBossCharacter : public AIU_EnemyCharacter
{
	GENERATED_BODY()

public:
	AIU_WorldBossCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Boss")
	FString BossEntityID = TEXT("boss.highland_titan_01");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Boss")
	FString BossDisplayName = TEXT("HIGHLAND TITAN");

	/** Base Boss HP (850.0f default in Greybox/Showcase raids so 2-player party traverses all 3 Phases cleanly; scales +8%/extra player). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Boss")
	float BaseRaidMaxHP = 850.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Boss")
	float DynamicCCUExtraPlayerScale = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Boss|Poise")
	float MaxPoise = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Boss|Poise")
	float StaggerDurationSeconds = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Boss|Poise")
	float PoiseImmunitySeconds = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Boss|Poise")
	float StaggerDamageMultiplier = 1.25f;

	/** Minimum height (cm) above Boss ground Z required to completely evade GroundQuake / VolcanicQuake (monsters.json: 450 cm = 4.5m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Boss|Mechanics")
	float SafeAltitudeZAboveGroundCm = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Boss|Mechanics")
	float QuakeRadiusCm = 2200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Boss|Mechanics")
	float BossRespawnSeconds = 45.0f;

	// ========================================================================
	// Pure Replicated State Getters (for Top-Center Boss Raid HUD & Tests)
	// ========================================================================

	UFUNCTION(BlueprintPure, Category = "IU|Boss")
	int32 GetBossPhase() const { return CurrentPhase; }

	UFUNCTION(BlueprintPure, Category = "IU|Boss")
	float GetCurrentPoise() const { return CurrentPoise; }

	UFUNCTION(BlueprintPure, Category = "IU|Boss")
	float GetMaxPoise() const { return MaxPoise; }

	UFUNCTION(BlueprintPure, Category = "IU|Boss")
	bool IsBossStaggered() const { return bIsStaggered; }

	UFUNCTION(BlueprintPure, Category = "IU|Boss")
	EIU_BossSpecialAction GetActiveSpecialAction() const { return ActiveSpecialAction; }

	UFUNCTION(BlueprintPure, Category = "IU|Boss")
	FString GetActiveTelegraphBanner() const { return ActiveTelegraphBanner; }

	UFUNCTION(BlueprintPure, Category = "IU|Boss")
	FString GetTopContributorsText() const { return TopContributorsSummary; }

	UFUNCTION(BlueprintPure, Category = "IU|Boss")
	float GetPhaseDamageMultiplier() const;

	/** Server/Test helper: evaluates whether a defender at WorldPos dodges GroundQuake via 3D altitude (> 450cm). */
	UFUNCTION(BlueprintPure, Category = "IU|Boss")
	bool DoesActorEvadeQuakeByAltitude(const FVector& DefenderWorldPos) const;

	/** Server/Test helper: forces a GroundQuake telegraph immediately for deterministic testing & showcase verification. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IU|Boss")
	void TriggerSpecialMechanicsTick();

	/** Server/Test helper: applies poise damage directly and triggers 6.0s Stagger when Poise hits 0. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IU|Boss")
	void ApplyPoiseDamageOnServer(float PoiseDmg);

protected:
	/** Expanding crimson ground shockwave disc shown during GroundQuake / SweepingBeam telegraphs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|Boss")
	TObjectPtr<UStaticMeshComponent> TelegraphRingMesh;

private:
	UFUNCTION()
	void HandleBossDamageReceived(const FIU_DamageSpec& DamageSpec, float RemainingHP);

	UFUNCTION()
	void HandleBossDefeated(AIU_EnemyCharacter* DefeatedEnemy);

	UFUNCTION()
	void OnRep_SpecialAction();

	void EvaluatePhaseTransition(float CurrentHP, float CurrentMaxHP);
	void RecoverFromStagger();
	void EndPoiseImmunity();
	void ExecuteSpecialMechanicImpact();
	void RespawnWorldBoss();
	void RebuildTopContributorsSummary();

	UPROPERTY(Replicated)
	int32 CurrentPhase = 1;

	UPROPERTY(Replicated)
	float CurrentPoise = 300.0f;

	UPROPERTY(Replicated)
	bool bIsStaggered = false;

	bool bPoiseRecoveryImmune = false;

	UPROPERTY(ReplicatedUsing = OnRep_SpecialAction)
	EIU_BossSpecialAction ActiveSpecialAction = EIU_BossSpecialAction::None;

	UPROPERTY(Replicated)
	FString ActiveTelegraphBanner;

	UPROPERTY(Replicated)
	FString TopContributorsSummary;

	double FightStartedWorldTime = 0.0;
	uint64 SpawnGeneration = 1;
	TMap<FString, FIU_BossDamageEntry> DamageByCharID;

	FTimerHandle SpecialCadenceTimerHandle;
	FTimerHandle SpecialImpactTimerHandle;
	FTimerHandle StaggerRecoverTimerHandle;
	FTimerHandle PoiseImmunityTimerHandle;
	FTimerHandle BossRespawnTimerHandle;
};
