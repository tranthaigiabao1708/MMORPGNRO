// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Characters/Enemy/IU_WorldBossCharacter.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Combat/IU_CombatThreatSubsystem.h"
#include "Core/IU_PlayerState.h"
#include "Core/IU_PlayerController.h"
#include "Network/IU_BackendSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "InfiniteUniverse.h"

AIU_WorldBossCharacter::AIU_WorldBossCharacter()
{
	SetActorScale3D(FVector(1.75f, 1.75f, 1.75f));

	DetectionRange = 2800.0f;
	LeashDisengageRange = 5200.0f;
	ChaseMoveSpeed = 620.0f;
	AttackRange = 340.0f;
	AttackReachCm = 360.0f;
	AttackHitboxRadius = 95.0f;
	AttackDamage = 26.0f;
	AttackWindupDuration = 0.65f;
	AttackRecoveryDuration = 0.85f;

	if (AttributeComponent)
	{
		AttributeComponent->MaxHP = BaseRaidMaxHP;
	}
	if (DamageComponent)
	{
		// Super Armor while Poise > 0: no knockback until staggered!
		DamageComponent->HitKnockbackSpeed = 0.0f;
	}

	TelegraphRingMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TelegraphRingMesh"));
	TelegraphRingMesh->SetupAttachment(GetCapsuleComponent());
	TelegraphRingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TelegraphRingMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -88.0f));
	TelegraphRingMesh->SetRelativeScale3D(FVector(18.0f, 18.0f, 0.04f));
	TelegraphRingMesh->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> DiscMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (DiscMesh.Succeeded())
	{
		TelegraphRingMesh->SetStaticMesh(DiscMesh.Object);
	}
}

void AIU_WorldBossCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AIU_WorldBossCharacter, CurrentPhase);
	DOREPLIFETIME(AIU_WorldBossCharacter, CurrentPoise);
	DOREPLIFETIME(AIU_WorldBossCharacter, bIsStaggered);
	DOREPLIFETIME(AIU_WorldBossCharacter, ActiveSpecialAction);
	DOREPLIFETIME(AIU_WorldBossCharacter, ActiveTelegraphBanner);
	DOREPLIFETIME(AIU_WorldBossCharacter, TopContributorsSummary);
}

void AIU_WorldBossCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (DamageComponent)
	{
		DamageComponent->OnDamageReceived.AddUniqueDynamic(this, &AIU_WorldBossCharacter::HandleBossDamageReceived);
	}
	OnEnemyDefeated.AddUniqueDynamic(this, &AIU_WorldBossCharacter::HandleBossDefeated);
}

void AIU_WorldBossCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (TelegraphRingMesh)
	{
		if (UMaterialInterface* CrimsonMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Characters/Enemy/Materials/M_EnemyVisor.M_EnemyVisor")))
		{
			TelegraphRingMesh->SetMaterial(0, CrimsonMat);
		}
	}

	if (HasAuthority())
	{
		CurrentPhase = 1;
		CurrentPoise = MaxPoise;
		bIsStaggered = false;
		bPoiseRecoveryImmune = false;
		FightStartedWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

		// Scale MaxHP dynamically with online players (+8% per extra player beyond 1)
		int32 ActivePlayers = 0;
		if (UWorld* W = GetWorld())
		{
			for (TActorIterator<AIU_PlayerCharacter> It(W); It; ++It)
			{
				if (IsValid(*It)) { ++ActivePlayers; }
			}
			W->GetTimerManager().SetTimer(
				SpecialCadenceTimerHandle,
				this,
				&AIU_WorldBossCharacter::TriggerSpecialMechanicsTick,
				6.2f,
				true,
				3.2f);
		}
		const float ScaledHP = BaseRaidMaxHP * (1.0f + DynamicCCUExtraPlayerScale * static_cast<float>(FMath::Max(0, ActivePlayers - 1)));
		if (AttributeComponent)
		{
			AttributeComponent->ApplyPersistedVitals(ScaledHP, ScaledHP, 200.0f, 200.0f, 1.0f, AttackDamage, 35.0f);
		}
	}
}

void AIU_WorldBossCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(SpecialCadenceTimerHandle);
		W->GetTimerManager().ClearTimer(SpecialImpactTimerHandle);
		W->GetTimerManager().ClearTimer(StaggerRecoverTimerHandle);
		W->GetTimerManager().ClearTimer(PoiseImmunityTimerHandle);
		W->GetTimerManager().ClearTimer(BossRespawnTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

float AIU_WorldBossCharacter::GetPhaseDamageMultiplier() const
{
	if (CurrentPhase >= 3) { return 1.30f; }
	if (CurrentPhase == 2) { return 1.15f; }
	return 1.00f;
}

bool AIU_WorldBossCharacter::DoesActorEvadeQuakeByAltitude(const FVector& DefenderWorldPos) const
{
	const float BossGroundZ = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	return (DefenderWorldPos.Z - BossGroundZ) >= SafeAltitudeZAboveGroundCm;
}

void AIU_WorldBossCharacter::ApplyPoiseDamageOnServer(float PoiseDmg)
{
	if (!HasAuthority() || IsDead() || bIsStaggered || bPoiseRecoveryImmune || PoiseDmg <= 0.0f)
	{
		return;
	}

	CurrentPoise = FMath::Max(0.0f, CurrentPoise - PoiseDmg);
	if (CurrentPoise <= 0.0f)
	{
		bIsStaggered = true;
		ActiveSpecialAction = EIU_BossSpecialAction::None;
		ActiveTelegraphBanner = TEXT("[PHA GIAP STAGGER 6.0s!] Boss bi choang -> Nhan 1.25x Sat Thuong!");
		OnRep_SpecialAction();
		CancelActiveAttack();

		if (UWorld* W = GetWorld())
		{
			W->GetTimerManager().ClearTimer(SpecialImpactTimerHandle);
			W->GetTimerManager().SetTimer(
				StaggerRecoverTimerHandle,
				this,
				&AIU_WorldBossCharacter::RecoverFromStagger,
				StaggerDurationSeconds,
				false);
		}
	}
	ForceNetUpdate();
}

void AIU_WorldBossCharacter::RecoverFromStagger()
{
	if (!HasAuthority())
	{
		return;
	}
	bIsStaggered = false;
	bPoiseRecoveryImmune = true;
	CurrentPoise = MaxPoise;
	ActiveTelegraphBanner.Empty();
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().SetTimer(
			PoiseImmunityTimerHandle,
			this,
			&AIU_WorldBossCharacter::EndPoiseImmunity,
			PoiseImmunitySeconds,
			false);
	}
	ForceNetUpdate();
}

void AIU_WorldBossCharacter::EndPoiseImmunity()
{
	bPoiseRecoveryImmune = false;
}

void AIU_WorldBossCharacter::EvaluatePhaseTransition(float CurrentHP, float CurrentMaxHP)
{
	if (!HasAuthority() || CurrentMaxHP <= 0.0f || CurrentHP <= 0.0f)
	{
		return;
	}
	const float Ratio = CurrentHP / CurrentMaxHP;
	int32 DesiredPhase = 1;
	if (Ratio <= 0.20f)
	{
		DesiredPhase = 3;
	}
	else if (Ratio <= 0.50f)
	{
		DesiredPhase = 2;
	}

	if (DesiredPhase > CurrentPhase)
	{
		CurrentPhase = DesiredPhase;
		if (CurrentPhase == 2)
		{
			ChaseMoveSpeed = 720.0f;
			ActiveTelegraphBanner = TEXT("[PHASE 2 - DUNG NHAM CUONG NO!] +15% Sat Thuong & Tia Quet 180 Do!");
		}
		else if (CurrentPhase == 3)
		{
			ChaseMoveSpeed = 820.0f;
			ActiveTelegraphBanner = TEXT("[PHASE 3 - HAO QUANG HUY DIET!] +30% Sat Thuong! Dung Dau Than & Don Dame!");
		}
		ForceNetUpdate();
	}
}

void AIU_WorldBossCharacter::HandleBossDamageReceived(const FIU_DamageSpec& DamageSpec, float RemainingHP)
{
	if (!HasAuthority())
	{
		return;
	}

	// Super Armor while Poise > 0: immediately clear hit-stun so normal strikes don't interrupt Boss!
	if (!bIsStaggered && CurrentPoise > 0.0f && DamageComponent && RemainingHP > 0.0f)
	{
		DamageComponent->ResetDamageState();
	}

	if (FightStartedWorldTime <= 0.0 && GetWorld())
	{
		FightStartedWorldTime = GetWorld()->GetTimeSeconds();
	}

	// Bonus +25% stagger vulnerability damage (global_constants.json: stagger_damage_multiplier = 1.25)
	double EffectiveContribution = FMath::Max(0.0f, DamageSpec.Amount);
	if (bIsStaggered && RemainingHP > 0.0f && AttributeComponent && DamageSpec.Amount > 0.0f)
	{
		const float ExtraBonus = DamageSpec.Amount * FMath::Max(0.0f, StaggerDamageMultiplier - 1.0f);
		if (ExtraBonus > 0.0f)
		{
			AttributeComponent->ModifyHP(-ExtraBonus);
			RemainingHP = AttributeComponent->GetHP();
			EffectiveContribution += ExtraBonus;
		}
	}

	// Deplete Poise when not staggered
	ApplyPoiseDamageOnServer(FMath::Max(18.0f, DamageSpec.Amount * 1.4f));

	// Record damage in server-authoritative contributor table
	if (AActor* Inst = DamageSpec.InstigatorActor.Get())
	{
		APawn* AttackerPawn = Cast<APawn>(Inst);
		if (!AttackerPawn)
		{
			AttackerPawn = Inst->GetInstigator();
		}
		if (AttackerPawn)
		{
			if (const AIU_PlayerState* PS = AttackerPawn->GetPlayerState<AIU_PlayerState>())
			{
				const FString Key = !PS->GetPersistedCharacterUUID().IsEmpty()
					? PS->GetPersistedCharacterUUID()
					: PS->GetPlayerName();
				if (!Key.IsEmpty())
				{
					FIU_BossDamageEntry& Entry = DamageByCharID.FindOrAdd(Key);
					Entry.CharacterID = PS->GetPersistedCharacterUUID();
					Entry.CharacterName = !PS->GetPersistedCharacterName().IsEmpty() ? PS->GetPersistedCharacterName() : PS->GetPlayerName();
					Entry.FencingEpoch = PS->GetActiveFencingEpoch();
					Entry.DamageDealt += EffectiveContribution;
					RebuildTopContributorsSummary();
				}
			}
		}
	}

	if (AttributeComponent)
	{
		EvaluatePhaseTransition(RemainingHP, AttributeComponent->GetMaxHP());
	}
}

void AIU_WorldBossCharacter::RebuildTopContributorsSummary()
{
	TArray<FIU_BossDamageEntry> Sorted;
	double Total = 0.0;
	for (const TPair<FString, FIU_BossDamageEntry>& Pair : DamageByCharID)
	{
		Sorted.Add(Pair.Value);
		Total += Pair.Value.DamageDealt;
	}
	if (Total <= 0.0 || Sorted.IsEmpty())
	{
		TopContributorsSummary.Empty();
		return;
	}
	Sorted.Sort([](const FIU_BossDamageEntry& A, const FIU_BossDamageEntry& B) { return A.DamageDealt > B.DamageDealt; });

	TArray<FString> Parts;
	const int32 ShowCount = FMath::Min(3, Sorted.Num());
	for (int32 i = 0; i < ShowCount; ++i)
	{
		const double Pct = (Sorted[i].DamageDealt / Total) * 100.0;
		Parts.Add(FString::Printf(TEXT("#%d %s (%.0f%%)"), i + 1, *Sorted[i].CharacterName, Pct));
	}
	TopContributorsSummary = FString::Join(Parts, TEXT("  |  "));
}

void AIU_WorldBossCharacter::TriggerSpecialMechanicsTick()
{
	UWorld* W = GetWorld();
	if (!W || !HasAuthority() || IsDead() || bIsStaggered || ActiveSpecialAction != EIU_BossSpecialAction::None)
	{
		return;
	}

	// Ensure at least one living player is within aggro radius before firing arena mechanics
	bool bAnyPlayerNearby = false;
	for (TActorIterator<AIU_PlayerCharacter> It(W); It; ++It)
	{
		if (IsValid(*It) && FVector::Dist(GetActorLocation(), It->GetActorLocation()) <= DetectionRange)
		{
			bAnyPlayerNearby = true;
			break;
		}
	}
	if (!bAnyPlayerNearby && !GIsAutomationTesting)
	{
		return;
	}

	if (CurrentPhase >= 2 && (FMath::RandBool()))
	{
		ActiveSpecialAction = EIU_BossSpecialAction::SweepingBeam;
		ActiveTelegraphBanner = TEXT("[CANH BAO TIA HUY DIET 180 DO!] Bam [RMB] Tan Anh Bo hoac Vong Ra Sau Lung Boss!");
	}
	else
	{
		ActiveSpecialAction = EIU_BossSpecialAction::GroundQuake;
		ActiveTelegraphBanner = TEXT("[DIA CHAN 360 DO!] Bam [6] / Space BAY LEN CAO > 4.5m DE NE 100% SAT THUONG!");
	}
	OnRep_SpecialAction();

	// Register threat with UIU_CombatThreatSubsystem so RMB Dodge can also trigger Perfect Evade!
	if (UIU_CombatThreatSubsystem* ThreatSub = W->GetSubsystem<UIU_CombatThreatSubsystem>())
	{
		ThreatSub->RegisterMeleeAttackWindup(this, GetActorLocation(), GetActorLocation(), QuakeRadiusCm * 0.5f, 1.85f, TelegraphRingMesh);
	}

	W->GetTimerManager().SetTimer(
		SpecialImpactTimerHandle,
		this,
		&AIU_WorldBossCharacter::ExecuteSpecialMechanicImpact,
		1.85f,
		false);
	ForceNetUpdate();
}

void AIU_WorldBossCharacter::OnRep_SpecialAction()
{
	if (TelegraphRingMesh)
	{
		TelegraphRingMesh->SetVisibility(ActiveSpecialAction != EIU_BossSpecialAction::None && !IsDead());
	}
}

void AIU_WorldBossCharacter::ExecuteSpecialMechanicImpact()
{
	UWorld* W = GetWorld();
	if (!W || !HasAuthority() || IsDead() || bIsStaggered)
	{
		ActiveSpecialAction = EIU_BossSpecialAction::None;
		OnRep_SpecialAction();
		return;
	}

	const EIU_BossSpecialAction FiredAction = ActiveSpecialAction;
	ActiveSpecialAction = EIU_BossSpecialAction::None;
	OnRep_SpecialAction();

	const FVector BossLoc = GetActorLocation();
	const FVector BossFwd = GetActorForwardVector().GetSafeNormal2D();
	const float RawSkillDmg = AttackDamage * GetPhaseDamageMultiplier() * ((FiredAction == EIU_BossSpecialAction::SweepingBeam) ? 1.85f : 1.65f);

	for (TActorIterator<AIU_PlayerCharacter> It(W); It; ++It)
	{
		AIU_PlayerCharacter* Player = *It;
		if (!IsValid(Player)) { continue; }
		UIU_DamageComponent* PlayerDmg = Player->GetDamageComponent();
		if (!PlayerDmg) { continue; }

		const FVector PPos = Player->GetActorLocation();
		if (FVector::Dist2D(BossLoc, PPos) > QuakeRadiusCm)
		{
			continue;
		}

		if (FiredAction == EIU_BossSpecialAction::GroundQuake)
		{
			// 3D Aerial Counterplay: Players flying >= 450 cm above ground completely evade the Ground Quake!
			if (DoesActorEvadeQuakeByAltitude(PPos))
			{
				continue;
			}
		}
		else if (FiredAction == EIU_BossSpecialAction::SweepingBeam)
		{
			const FVector DirToPlayer = (PPos - BossLoc).GetSafeNormal2D();
			if (FVector::DotProduct(BossFwd, DirToPlayer) < 0.0f)
			{
				// Behind Boss's 180-deg frontal arc -> safe!
				continue;
			}
		}

		FIU_DamageSpec Spec;
		Spec.Amount = RawSkillDmg;
		Spec.HitStunDuration = 0.28f;
		Spec.InstigatorActor = this;
		Spec.DamageCauser = this;
		Spec.HitDirection = (PPos - BossLoc).GetSafeNormal();
		PlayerDmg->ApplyDamageSpec(Spec);
	}

	ActiveTelegraphBanner.Empty();
	ForceNetUpdate();
}

void AIU_WorldBossCharacter::HandleBossDefeated(AIU_EnemyCharacter* DefeatedEnemy)
{
	if (!HasAuthority())
	{
		return;
	}

	ActiveSpecialAction = EIU_BossSpecialAction::None;
	ActiveTelegraphBanner = TEXT("[CHIEN THANG!] World Boss Highland Titan da bi ha guc! Dang chia thuong & Huy Chuong Pity...");
	OnRep_SpecialAction();

	UWorld* W = GetWorld();
	if (W)
	{
		W->GetTimerManager().ClearTimer(SpecialImpactTimerHandle);
		W->GetTimerManager().SetTimer(
			BossRespawnTimerHandle,
			this,
			&AIU_WorldBossCharacter::RespawnWorldBoss,
			BossRespawnSeconds,
			false);
	}

	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (Backend && Backend->IsBackendBridgeEnabled() && !DamageByCharID.IsEmpty())
	{
		TArray<FIU_BossParticipantReport> Reports;
		for (const TPair<FString, FIU_BossDamageEntry>& Pair : DamageByCharID)
		{
			if (!Pair.Value.CharacterID.IsEmpty() && Pair.Value.DamageDealt > 0.0)
			{
				FIU_BossParticipantReport R;
				R.CharacterID = Pair.Value.CharacterID;
				R.CharacterName = Pair.Value.CharacterName;
				R.FencingEpoch = Pair.Value.FencingEpoch;
				R.DamageDealt = static_cast<float>(Pair.Value.DamageDealt);
				Reports.Add(R);
			}
		}

		if (!Reports.IsEmpty())
		{
			const double NowSec = W ? W->GetTimeSeconds() : 0.0;
			const int32 DurationMs = FMath::Max(1000, FMath::RoundToInt((NowSec - FightStartedWorldTime) * 1000.0));
			const FString IdemKey = FString::Printf(
				TEXT("worldboss_%s_%s_gen%llu_%lld"),
				*BossEntityID,
				*Backend->GetServerInstanceID(),
				SpawnGeneration,
				FDateTime::UtcNow().ToUnixTimestamp());

			TWeakObjectPtr<UWorld> WeakW(W);
			Backend->SettleWorldBossKill(
				BossEntityID,
				DurationMs,
				IdemKey,
				Reports,
				[WeakW](bool bOK, const FIU_BossKillSettleSummary& Summary, const FString& ErrMsg)
				{
					if (!bOK)
					{
						UE_LOG(LogInfiniteUniverse, Warning, TEXT("[WorldBoss] SettleWorldBossKill failed: %s"), *ErrMsg);
						return;
					}
					UWorld* LiveW = WeakW.Get();
					if (!LiveW) { return; }

					for (FConstPlayerControllerIterator It = LiveW->GetPlayerControllerIterator(); It; ++It)
					{
						AIU_PlayerController* PC = Cast<AIU_PlayerController>(It->Get());
						if (!PC) { continue; }
						AIU_PlayerState* PS = PC->GetPlayerState<AIU_PlayerState>();
						if (!PS) { continue; }
						const FString CID = PS->GetPersistedCharacterUUID();
						for (const FIU_BossParticipantRewardView& Rw : Summary.Rewards)
						{
							if (Rw.bEligible && Rw.CharacterID == CID)
							{
								PS->SetAuthoritativeTitanTokens(Rw.NewTokenBalance);
								PC->Server_RequestRefreshInventory();
								PC->Server_RequestRefreshQuests();
								PC->Server_RequestRefreshPityShop();
								PC->Client_NotifyLootDropped(Rw.CoinsGranted, Rw.DroppedItemLabels, false);
								break;
							}
						}
					}
				});
		}
	}
}

void AIU_WorldBossCharacter::RespawnWorldBoss()
{
	if (!HasAuthority())
	{
		return;
	}
	++SpawnGeneration;
	DamageByCharID.Reset();
	TopContributorsSummary.Empty();
	CurrentPhase = 1;
	CurrentPoise = MaxPoise;
	bIsStaggered = false;
	bPoiseRecoveryImmune = false;
	ActiveSpecialAction = EIU_BossSpecialAction::None;
	ActiveTelegraphBanner.Empty();
	FightStartedWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	ResetEnemyAtHome();
	ForceNetUpdate();
}
