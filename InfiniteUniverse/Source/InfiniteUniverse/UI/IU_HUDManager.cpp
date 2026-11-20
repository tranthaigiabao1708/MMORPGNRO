// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "UI/IU_HUDManager.h"
#include "UI/IU_PlayerHUDWidget.h"
#include "Core/IU_GameState.h"
#include "Core/IU_PlayerState.h"
#include "Core/IU_PlayerController.h"
#include "Core/IU_ZonePortalActor.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
#include "Characters/Enemy/IU_WorldBossCharacter.h"
#include "Movement/IU_MovementComponent.h"
#include "Movement/IU_FlightComponent.h"
#include "Combat/IU_TargetingComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Combat/IU_CombatComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMemory.h"

AIU_HUDManager::AIU_HUDManager()
{
	PlayerHUDWidgetClass = UIU_PlayerHUDWidget::StaticClass();
}

void AIU_HUDManager::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PC = GetOwningPlayerController();
	if (PC && PC->IsLocalController())
	{
		TSubclassOf<UIU_PlayerHUDWidget> WidgetClassToSpawn = PlayerHUDWidgetClass;
		if (!WidgetClassToSpawn)
		{
			WidgetClassToSpawn = UIU_PlayerHUDWidget::StaticClass();
		}

		ActiveHUDWidget = CreateWidget<UIU_PlayerHUDWidget>(PC, WidgetClassToSpawn);
		if (ActiveHUDWidget)
		{
			ActiveHUDWidget->AddToViewport(0);
			if (APawn* InitialPawn = PC->GetPawn())
			{
				ActiveHUDWidget->BindToPlayerPawn(InitialPawn);
				BoundHUDWidgetPawn = InitialPawn;
			}
		}
	}

	RefreshCombatEventBindings(GetOwningPawn());
}

void AIU_HUDManager::SetShowcaseStepBanner(const FString& InBannerText)
{
	ShowcaseStepBannerText = InBannerText;
}

void AIU_HUDManager::NotifyLootPopup(int64 CoinsGranted, const TArray<FString>& DroppedItemLabels, bool bBagFullSkipped)
{
	UWorld* World = GetWorld();
	APawn* OwningPawn = GetOwningPawn();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	if (OwningPawn && CoinsGranted > 0)
	{
		FIU_FloatingCombatText CoinPop;
		CoinPop.WorldLocation = OwningPawn->GetActorLocation() + FVector(FMath::FRandRange(-22.0f, 22.0f), FMath::FRandRange(-22.0f, 22.0f), 108.0f);
		CoinPop.Label = FString::Printf(TEXT("+%lld VANG!"), CoinsGranted);
		CoinPop.Color = FLinearColor(1.0f, 0.86f, 0.16f, 1.0f);
		CoinPop.SpawnWorldTime = Now;
		CoinPop.Duration = 1.45f;
		CoinPop.Scale = 1.42f;
		FloatingTexts.Add(CoinPop);
	}

	if (OwningPawn && DroppedItemLabels.Num() > 0)
	{
		FIU_FloatingCombatText ItemPop;
		ItemPop.WorldLocation = OwningPawn->GetActorLocation() + FVector(0.0f, 0.0f, 126.0f);
		ItemPop.Label = FString::Printf(TEXT("LOOT: %s"), *DroppedItemLabels[0]);
		ItemPop.Color = FLinearColor(0.45f, 1.0f, 0.65f, 1.0f);
		ItemPop.SpawnWorldTime = Now;
		ItemPop.Duration = 1.65f;
		ItemPop.Scale = 1.35f;
		FloatingTexts.Add(ItemPop);
	}

	FString Summary = FString::Printf(TEXT("[ LOOT DROP ]  +%lld Vang"), CoinsGranted);
	if (DroppedItemLabels.Num() > 0)
	{
		Summary += FString::Printf(TEXT("  |  %s"), *FString::Join(DroppedItemLabels, TEXT(", ")));
	}
	if (bBagFullSkipped)
	{
		Summary += TEXT("  [TUI DO DAY 48/48!]");
	}
	ActiveCenterToastText = Summary;
	ActiveCenterToastColor = bBagFullSkipped
		? FLinearColor(1.0f, 0.52f, 0.22f, 1.0f)
		: FLinearColor(1.0f, 0.88f, 0.25f, 1.0f);
	CenterToastExpireTime = Now + 3.2;
}

void AIU_HUDManager::NotifyInventoryToast(const FString& Message, bool bSuccess)
{
	UWorld* World = GetWorld();
	if (!World || Message.IsEmpty())
	{
		return;
	}
	ActiveCenterToastText = FString::Printf(TEXT("[ HANH TRANG ]  %s"), *Message);
	ActiveCenterToastColor = bSuccess
		? FLinearColor(0.40f, 1.0f, 0.70f, 1.0f)
		: FLinearColor(1.0f, 0.42f, 0.32f, 1.0f);
	CenterToastExpireTime = World->GetTimeSeconds() + 3.2;
}

void AIU_HUDManager::RefreshCombatEventBindings(APawn* OwningPawn)
{
	if (ActiveHUDWidget && OwningPawn)
	{
		if (BoundHUDWidgetPawn.Get() != OwningPawn)
		{
			ActiveHUDWidget->BindToPlayerPawn(OwningPawn);
			BoundHUDWidgetPawn = OwningPawn;
		}
		else
		{
			ActiveHUDWidget->EnsurePlayerStateBound(OwningPawn);
		}
	}

	if (OwningPawn)
	{
		if (AIU_PlayerState* LocalPS = OwningPawn->GetPlayerState<AIU_PlayerState>())
		{
			if (ObservedLocalPlayerState.Get() != LocalPS)
			{
				ObservedLocalPlayerState = LocalPS;
				LastObservedPotentialPoints = LocalPS->GetPotentialPoints();
				LocalPS->OnCharacterLevelUp.AddUniqueDynamic(this, &AIU_HUDManager::HandleLocalCharacterLevelUp);
				LocalPS->OnCharacterPotentialChanged.AddUniqueDynamic(this, &AIU_HUDManager::HandleLocalPotentialChanged);
			}
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	if (Now < NextBindingScanTime)
	{
		return;
	}
	NextBindingScanTime = Now + 0.35;

	if (OwningPawn)
	{
		if (UIU_CombatComponent* PlayerCombat = OwningPawn->FindComponentByClass<UIU_CombatComponent>())
		{
			if (ObservedPlayerCombat.Get() != PlayerCombat)
			{
				ObservedPlayerCombat = PlayerCombat;
				PlayerCombat->OnCombatStateChanged.AddUniqueDynamic(this, &AIU_HUDManager::HandlePlayerCombatStateChanged);
			}
		}
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Act = *It;
		if (!IsValid(Act))
		{
			continue;
		}
		if (UIU_DamageComponent* DmgComp = Act->FindComponentByClass<UIU_DamageComponent>())
		{
			if (!ObservedDamageComponents.Contains(DmgComp))
			{
				ObservedDamageComponents.Add(DmgComp);
				DmgComp->OnDamageReceived.AddUniqueDynamic(this, &AIU_HUDManager::HandleAnyActorDamaged);
			}
		}
		if (UIU_AttributeComponent* AttrComp = Act->FindComponentByClass<UIU_AttributeComponent>())
		{
			if (!ObservedAttributeComponents.Contains(AttrComp))
			{
				ObservedAttributeComponents.Add(AttrComp);
				AttrComp->OnHealthDepleted.AddUniqueDynamic(this, &AIU_HUDManager::HandleAnyActorHealthDepleted);
			}
		}
	}
}

void AIU_HUDManager::HandleAnyActorDamaged(const FIU_DamageSpec& DamageSpec, float RemainingHP)
{
	UWorld* World = GetWorld();
	if (!World || DamageSpec.Amount <= 0.0f)
	{
		return;
	}

	const APawn* OwningPawn = GetOwningPawn();
	const bool bPlayerDealtHit = (OwningPawn && DamageSpec.InstigatorActor.Get() == OwningPawn);
	const bool bHeavyHit = DamageSpec.Amount >= 25.0f;

	FIU_FloatingCombatText Entry;
	Entry.WorldLocation = DamageSpec.HitLocation + FVector(FMath::FRandRange(-18.0f, 18.0f), FMath::FRandRange(-18.0f, 18.0f), 42.0f);
	Entry.Label = bHeavyHit
		? FString::Printf(TEXT("-%.0f!"), DamageSpec.Amount)
		: FString::Printf(TEXT("-%.0f"), DamageSpec.Amount);
	Entry.Color = bPlayerDealtHit
		? (bHeavyHit ? FLinearColor(1.0f, 0.78f, 0.12f, 1.0f) : FLinearColor(1.0f, 0.95f, 0.65f, 1.0f))
		: FLinearColor(1.0f, 0.25f, 0.22f, 1.0f);
	Entry.SpawnWorldTime = World->GetTimeSeconds();
	Entry.Duration = bHeavyHit ? 0.95f : 0.78f;
	Entry.Scale = bHeavyHit ? 1.55f : 1.25f;

	if (FloatingTexts.Num() > 24)
	{
		FloatingTexts.RemoveAt(0);
	}
	FloatingTexts.Add(Entry);
}

void AIU_HUDManager::HandleAnyActorHealthDepleted(AActor* DepletedActor)
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(DepletedActor))
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const APawn* OwningPawn = GetOwningPawn();

	if (DepletedActor == OwningPawn)
	{
		++PlayerDefeatedCount;
		ActiveCenterToastText = TEXT("[ PLAYER DEFEATED ]  HP Depleted — Auto-Respawning at Checkpoint...");
		ActiveCenterToastColor = FLinearColor(1.0f, 0.22f, 0.20f, 1.0f);
		CenterToastExpireTime = Now + 3.0;
	}
	else if (DepletedActor->IsA<AIU_PlayerCharacter>())
	{
		ActiveCenterToastText = TEXT("[ SPAR RIVAL DEFEATED ]  Opponent Knocked Out in Martial Arena!");
		ActiveCenterToastColor = FLinearColor(1.0f, 0.78f, 0.18f, 1.0f);
		CenterToastExpireTime = Now + 2.4;

		FIU_FloatingCombatText RivalPopup;
		RivalPopup.WorldLocation = DepletedActor->GetActorLocation() + FVector(0.0f, 0.0f, 95.0f);
		RivalPopup.Label = TEXT("RIVAL DEFEATED!");
		RivalPopup.Color = FLinearColor(1.0f, 0.80f, 0.20f, 1.0f);
		RivalPopup.SpawnWorldTime = Now;
		RivalPopup.Duration = 1.25f;
		RivalPopup.Scale = 1.45f;
		FloatingTexts.Add(RivalPopup);
	}
	else
	{
		++DefeatedEnemyCount;
		ActiveCenterToastText = FString::Printf(TEXT("[ ENEMY DEFEATED ]  Target Eliminated (Total Kills: %d)"), DefeatedEnemyCount);
		ActiveCenterToastColor = FLinearColor(0.30f, 1.0f, 0.52f, 1.0f);
		CenterToastExpireTime = Now + 2.4;

		FIU_FloatingCombatText KillPopup;
		KillPopup.WorldLocation = DepletedActor->GetActorLocation() + FVector(0.0f, 0.0f, 95.0f);
		KillPopup.Label = TEXT("ENEMY DEFEATED!");
		KillPopup.Color = FLinearColor(0.32f, 1.0f, 0.50f, 1.0f);
		KillPopup.SpawnWorldTime = Now;
		KillPopup.Duration = 1.25f;
		KillPopup.Scale = 1.45f;
		FloatingTexts.Add(KillPopup);
	}
}

void AIU_HUDManager::HandlePlayerCombatStateChanged(EIU_CombatState PreviousState, EIU_CombatState NewState)
{
	UWorld* World = GetWorld();
	APawn* OwningPawn = GetOwningPawn();
	if (!World || !OwningPawn || NewState != EIU_CombatState::Dodging)
	{
		return;
	}

	const UIU_CombatComponent* Combat = OwningPawn->FindComponentByClass<UIU_CombatComponent>();
	if (Combat && Combat->IsLastDodgeCombatEvade())
	{
		FIU_FloatingCombatText EvadeBanner;
		EvadeBanner.WorldLocation = OwningPawn->GetActorLocation() + FVector(0.0f, 0.0f, 105.0f);
		EvadeBanner.Label = TEXT("PERFECT EVADE!");
		EvadeBanner.Color = FLinearColor(0.20f, 0.92f, 1.0f, 1.0f);
		EvadeBanner.SpawnWorldTime = World->GetTimeSeconds();
		EvadeBanner.Duration = 1.05f;
		EvadeBanner.Scale = 1.50f;
		FloatingTexts.Add(EvadeBanner);
	}
}

void AIU_HUDManager::HandleLocalCharacterLevelUp(int32 OldLevel, int32 NewLevel)
{
	UWorld* World = GetWorld();
	APawn* OwningPawn = GetOwningPawn();
	if (!World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	ActiveCenterToastText = FString::Printf(TEXT("[ LEVEL UP!  LV. %d -> LV. %d ]  Bam TAB -> Ky Nang de cong Tiem Nang!"), OldLevel, NewLevel);
	ActiveCenterToastColor = FLinearColor(1.0f, 0.88f, 0.22f, 1.0f);
	CenterToastExpireTime = Now + 4.0;

	if (OwningPawn)
	{
		FIU_FloatingCombatText LvPopup;
		LvPopup.WorldLocation = OwningPawn->GetActorLocation() + FVector(0.0f, 0.0f, 118.0f);
		LvPopup.Label = FString::Printf(TEXT("LEVEL UP! (LV. %d)"), NewLevel);
		LvPopup.Color = FLinearColor(1.0f, 0.90f, 0.18f, 1.0f);
		LvPopup.SpawnWorldTime = Now;
		LvPopup.Duration = 1.80f;
		LvPopup.Scale = 1.65f;
		FloatingTexts.Add(LvPopup);
	}
}

void AIU_HUDManager::HandleLocalPotentialChanged(int64 PotentialPoints, int64 NextHPCost, int64 NextKiCost, int64 NextAtkCost)
{
	UWorld* World = GetWorld();
	APawn* OwningPawn = GetOwningPawn();
	if (!World)
	{
		LastObservedPotentialPoints = PotentialPoints;
		return;
	}

	const int64 PrevTN = LastObservedPotentialPoints;
	LastObservedPotentialPoints = PotentialPoints;
	if (PrevTN < 0 || World->GetTimeSeconds() < 1.25)
	{
		return;
	}

	const int64 DeltaTN = PotentialPoints - PrevTN;
	const double Now = World->GetTimeSeconds();
	if (DeltaTN > 0 && OwningPawn)
	{
		FIU_FloatingCombatText PotPopup;
		PotPopup.WorldLocation = OwningPawn->GetActorLocation() + FVector(FMath::FRandRange(-28.0f, 28.0f), FMath::FRandRange(-28.0f, 28.0f), 98.0f);
		PotPopup.Label = FString::Printf(TEXT("+%lld SM & TN"), DeltaTN);
		PotPopup.Color = FLinearColor(0.32f, 1.0f, 0.78f, 1.0f);
		PotPopup.SpawnWorldTime = Now;
		PotPopup.Duration = 1.05f;
		PotPopup.Scale = 1.25f;
		if (FloatingTexts.Num() > 28)
		{
			FloatingTexts.RemoveAt(0);
		}
		FloatingTexts.Add(PotPopup);
	}
	else if (DeltaTN < 0 && OwningPawn)
	{
		const UIU_AttributeComponent* Attr = OwningPawn->FindComponentByClass<UIU_AttributeComponent>();
		const float MaxHP = Attr ? Attr->GetMaxHP() : 200.0f;
		const float MaxKi = Attr ? Attr->GetMaxKi() : 200.0f;
		const float Atk = Attr ? Attr->GetAttackPower() : 100.0f;

		ActiveCenterToastText = FString::Printf(
			TEXT("[ CONG TIEM NANG (-%lld TN) ]  HP %.0f | KI %.0f | SucDanh %.0f | Con %lld TN"),
			-DeltaTN, MaxHP, MaxKi, Atk, PotentialPoints);
		ActiveCenterToastColor = FLinearColor(0.38f, 1.0f, 0.72f, 1.0f);
		CenterToastExpireTime = Now + 3.6;

		FIU_FloatingCombatText UpgPopup;
		UpgPopup.WorldLocation = OwningPawn->GetActorLocation() + FVector(0.0f, 0.0f, 114.0f);
		UpgPopup.Label = FString::Printf(TEXT("CHI SO TANG! (-%lld TN)"), -DeltaTN);
		UpgPopup.Color = FLinearColor(0.35f, 1.0f, 0.65f, 1.0f);
		UpgPopup.SpawnWorldTime = Now;
		UpgPopup.Duration = 1.50f;
		UpgPopup.Scale = 1.50f;
		FloatingTexts.Add(UpgPopup);
	}
}

void AIU_HUDManager::DrawHUD()
{
	APawn* OwningPawn = GetOwningPawn();
	RefreshCombatEventBindings(OwningPawn);

	if (!Canvas)
	{
		return;
	}

	Super::DrawHUD();

	UWorld* World = GetWorld();

	if (World)
	{
		const float RawDt = FMath::Clamp(World->GetDeltaSeconds(), 0.0005f, 0.25f);
		SmoothedFrameDeltaSeconds = FMath::FInterpTo(SmoothedFrameDeltaSeconds, RawDt, RawDt, 8.0f);
	}

	UFont* HUDFont = GEngine ? GEngine->GetMediumFont() : nullptr;

	// 1. Genshin-style crisp center aim reticle (highlights warm gold when an enemy is in crosshair sightline)
	bool bEnemyInCrosshair = false;
	if (const AIU_PlayerCharacter* PlayerChar = Cast<AIU_PlayerCharacter>(OwningPawn))
	{
		if (World)
		{
			FVector CamOrigin, CamDir;
			PlayerChar->GetCameraAimRay(CamOrigin, CamDir);
			FCollisionQueryParams AimParams(SCENE_QUERY_STAT(IUHUDCrosshairCheck), false, PlayerChar);
			FCollisionObjectQueryParams PawnObj;
			PawnObj.AddObjectTypesToQuery(ECC_Pawn);
			FHitResult Hit;
			if (World->SweepSingleByObjectType(
				Hit,
				CamOrigin + (CamDir * 120.0f),
				CamOrigin + (CamDir * 6000.0f),
				FQuat::Identity,
				PawnObj,
				FCollisionShape::MakeSphere(32.0f),
				AimParams))
			{
				if (const AActor* HitActor = Hit.GetActor())
				{
					const UIU_AttributeComponent* HitAttr = HitActor->FindComponentByClass<UIU_AttributeComponent>();
					const UIU_DamageComponent* HitDmg = HitActor->FindComponentByClass<UIU_DamageComponent>();
					bool bHostileTarget = (HitAttr && HitDmg && !HitAttr->IsDead());
					if (bHostileTarget && HitActor->IsA<AIU_PlayerCharacter>())
					{
						if (const AIU_GameState* GS = World->GetGameState<AIU_GameState>())
						{
							bHostileTarget = GS->ArePlayersAllowedToSpar(PlayerChar, HitActor);
						}
					}
					bEnemyInCrosshair = bHostileTarget;
				}
			}
		}
	}

	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	const FLinearColor AimColor = bEnemyInCrosshair
		? FLinearColor(1.0f, 0.78f, 0.18f, 0.95f)
		: FLinearColor(0.95f, 0.98f, 1.0f, 0.72f);
	const float Gap = bEnemyInCrosshair ? 5.5f : 4.0f;
	const float Arm = 10.0f;

	DrawRect(AimColor, CenterX - 1.5f, CenterY - 1.5f, 3.0f, 3.0f);
	DrawLine(CenterX - Gap - Arm, CenterY, CenterX - Gap, CenterY, AimColor, 1.7f);
	DrawLine(CenterX + Gap, CenterY, CenterX + Gap + Arm, CenterY, AimColor, 1.7f);
	DrawLine(CenterX, CenterY - Gap - Arm, CenterX, CenterY - Gap, AimColor, 1.7f);
	DrawLine(CenterX, CenterY + Gap, CenterX, CenterY + Gap + Arm, AimColor, 1.7f);

	if (!OwningPawn || !World)
	{
		return;
	}

	const UIU_TargetingComponent* TargetingComp = OwningPawn->FindComponentByClass<UIU_TargetingComponent>();
	AActor* LockedTarget = (TargetingComp && TargetingComp->IsLockedOn()) ? TargetingComp->GetLockedTarget() : nullptr;

	// 2. World-Space Overhead Enemy HP Bars & Crimson "[ ! RMB EVADE ]" Windup Telegraph + AI Count
	int32 LivingEnemyCount = 0;
	int32 TotalEnemyActors = 0;
	const FVector PlayerPos = OwningPawn->GetActorLocation();

	for (TActorIterator<AIU_EnemyCharacter> It(World); It; ++It)
	{
		AIU_EnemyCharacter* Enemy = *It;
		if (!IsValid(Enemy))
		{
			continue;
		}
		++TotalEnemyActors;
		if (Enemy->IsDead())
		{
			continue;
		}
		++LivingEnemyCount;

		if (Enemy == LockedTarget)
		{
			continue;
		}

		const double DistSq = FVector::DistSquared(PlayerPos, Enemy->GetActorLocation());
		if (DistSq > FMath::Square(2400.0))
		{
			continue;
		}

		const UIU_AttributeComponent* EAttr = Enemy->GetAttributeComponent();
		const bool bDamagedOrAggro = (EAttr && EAttr->GetHPPercent() < 0.999f) ||
			(Enemy->GetEnemyAIState() != EIU_EnemyAIState::Idle);
		if (!bDamagedOrAggro)
		{
			continue;
		}

		const FVector HeadProj = Project(Enemy->GetActorLocation() + FVector(0.0f, 0.0f, 112.0f));
		if (HeadProj.Z <= 0.0f || HeadProj.X < 0.0f || HeadProj.X > Canvas->ClipX)
		{
			continue;
		}

		const float BarW = 74.0f;
		const float BarH = 6.0f;
		const float BarX = HeadProj.X - (BarW * 0.5f);
		const float BarY = HeadProj.Y;

		DrawRect(FLinearColor(0.04f, 0.04f, 0.06f, 0.78f), BarX - 1.0f, BarY - 1.0f, BarW + 2.0f, BarH + 2.0f);
		if (EAttr)
		{
			DrawRect(FLinearColor(0.92f, 0.20f, 0.22f, 0.92f), BarX, BarY, BarW * EAttr->GetHPPercent(), BarH);
		}

		if (Enemy->IsWindingUpAttack() && HUDFont)
		{
			DrawText(TEXT("[ ! RMB EVADE ]"), FLinearColor(1.0f, 0.32f, 0.12f, 1.0f), BarX - 14.0f, BarY - 22.0f, HUDFont, 1.05f);
		}
	}

	// 2b. World-Space Overhead HP Bars, [TAG] Guild Nameplates, NRO 3D Speech Bubbles & Tactical Party Pings (Step 2 + Step 5)
	const AIU_GameState* GameState = World->GetGameState<AIU_GameState>();
	const AIU_PlayerState* LocalIUPS = OwningPawn ? OwningPawn->GetPlayerState<AIU_PlayerState>() : nullptr;
	const FString LocalPartyID = LocalIUPS ? LocalIUPS->GetPartyID() : TEXT("");
	const float ServerNow = GameState ? GameState->GetServerWorldTimeSeconds() : static_cast<float>(World->GetTimeSeconds());

	for (TActorIterator<AIU_PlayerCharacter> It(World); It; ++It)
	{
		AIU_PlayerCharacter* OtherPlayer = *It;
		if (!IsValid(OtherPlayer))
		{
			continue;
		}
		const UIU_AttributeComponent* PAttr = OtherPlayer->GetAttributeComponent();
		const AIU_PlayerState* OtherIUPS = OtherPlayer->GetPlayerState<AIU_PlayerState>();

		// Render NRO 3D Overhead Speech Bubble above BOTH Local & Remote players when active!
		if (OtherIUPS && HUDFont && ServerNow < OtherIUPS->GetOverheadChatExpireServerTime())
		{
			const FString BubbleMsg = OtherIUPS->GetOverheadChatBubbleText();
			if (!BubbleMsg.IsEmpty() && FVector::DistSquared(PlayerPos, OtherPlayer->GetActorLocation()) <= FMath::Square(6200.0))
			{
				const FVector BubbleWorld = OtherPlayer->GetActorLocation() + FVector(0.0f, 0.0f, (OtherPlayer == OwningPawn) ? 132.0f : 152.0f);
				const FVector BubbleProj = Project(BubbleWorld);
				if (BubbleProj.Z > 0.0f && BubbleProj.X >= 10.0f && BubbleProj.X <= Canvas->ClipX - 10.0f)
				{
					float StrW = 0.0f, StrH = 0.0f;
					Canvas->StrLen(HUDFont, BubbleMsg, StrW, StrH);
					const float Scale = 0.86f;
					const float BoxW = FMath::Clamp(StrW * Scale + 20.0f, 84.0f, 360.0f);
					const float BoxH = 24.0f;
					const float BX = FMath::Clamp(BubbleProj.X - BoxW * 0.5f, 8.0f, Canvas->ClipX - BoxW - 8.0f);
					const float BY = BubbleProj.Y - BoxH;

					const FString Ch = OtherIUPS->GetOverheadChatChannel();
					const FLinearColor FillCol = (Ch == TEXT("PARTY"))
						? FLinearColor(0.84f, 0.98f, 1.0f, 0.96f)
						: (Ch == TEXT("GUILD"))
						? FLinearColor(1.0f, 0.95f, 0.76f, 0.96f)
						: FLinearColor(0.99f, 0.97f, 0.90f, 0.96f);

					// Crisp NRO speech bubble frame + bottom pointer stem
					DrawRect(FLinearColor(0.14f, 0.08f, 0.03f, 0.95f), BX - 2.0f, BY - 2.0f, BoxW + 4.0f, BoxH + 4.0f);
					DrawRect(FillCol, BX, BY, BoxW, BoxH);
					DrawRect(FLinearColor(0.14f, 0.08f, 0.03f, 0.95f), BubbleProj.X - 4.0f, BY + BoxH, 8.0f, 6.0f);
					DrawRect(FillCol, BubbleProj.X - 2.5f, BY + BoxH - 1.0f, 5.0f, 5.0f);
					DrawText(BubbleMsg, FLinearColor(0.10f, 0.06f, 0.02f, 1.0f), BX + 9.0f, BY + 3.0f, HUDFont, Scale);
				}
			}
		}

		if (OtherPlayer == OwningPawn || !PAttr || PAttr->IsDead() || OtherPlayer == LockedTarget)
		{
			continue;
		}

		const double DistSq = FVector::DistSquared(PlayerPos, OtherPlayer->GetActorLocation());
		if (DistSq > FMath::Square(3500.0))
		{
			continue;
		}

		const FVector HeadProj = Project(OtherPlayer->GetActorLocation() + FVector(0.0f, 0.0f, 116.0f));
		if (HeadProj.Z <= 0.0f || HeadProj.X < 0.0f || HeadProj.X > Canvas->ClipX)
		{
			continue;
		}

		const bool bSameParty = (!LocalPartyID.IsEmpty() && OtherIUPS && OtherIUPS->GetPartyID() == LocalPartyID);
		const bool bCanSpar = !bSameParty && (GameState ? GameState->ArePlayersAllowedToSpar(OwningPawn, OtherPlayer) : true);
		const float BarW = 86.0f;
		const float BarH = 7.0f;
		const float BarX = HeadProj.X - (BarW * 0.5f);
		const float BarY = HeadProj.Y;

		DrawRect(FLinearColor(0.03f, 0.04f, 0.06f, 0.80f), BarX - 1.0f, BarY - 1.0f, BarW + 2.0f, BarH + 2.0f);
		const FLinearColor PlayerBarCol = bSameParty
			? FLinearColor(0.24f, 0.98f, 0.48f, 0.96f)
			: (bCanSpar ? FLinearColor(1.0f, 0.55f, 0.12f, 0.95f) : FLinearColor(0.15f, 0.90f, 0.85f, 0.92f));
		DrawRect(PlayerBarCol, BarX, BarY, BarW * PAttr->GetHPPercent(), BarH);

		if (HUDFont)
		{
			int32 PlayerID = 0;
			int32 OtherLv = 1;
			FString OtherName, OtherGuildTag;
			if (const APlayerState* OtherPS = OtherPlayer->GetPlayerState())
			{
				PlayerID = OtherPS->GetPlayerId();
				if (OtherIUPS)
				{
					OtherLv = OtherIUPS->GetCharacterLevel();
					OtherName = OtherIUPS->GetPersistedCharacterName();
					OtherGuildTag = OtherIUPS->GetGuildTag();
				}
			}
			FString RoleTag;
			if (!OtherName.IsEmpty())
			{
				const FString GPrefix = OtherGuildTag.IsEmpty() ? TEXT("") : FString::Printf(TEXT("[%s] "), *OtherGuildTag);
				const FString PSuffix = bSameParty ? TEXT(" [PT]") : TEXT("");
				RoleTag = FString::Printf(TEXT("%sLv.%d %s%s"), *GPrefix, OtherLv, *OtherName, *PSuffix);
			}
			else
			{
				RoleTag = bCanSpar
					? FString::Printf(TEXT("[SPAR RIVAL #%d]"), PlayerID)
					: FString::Printf(TEXT("[CO-OP ALLY #%d]"), PlayerID);
			}
			DrawText(RoleTag, PlayerBarCol, BarX - 24.0f, BarY - 20.0f, HUDFont, 0.88f);
		}
	}

	// 2c. Render 3D Tactical Party Pings (Alt + LMB, 6.0s Sky Beam + Distance Readout)
	if (GameState && HUDFont)
	{
		const FString SoloPingKey = LocalIUPS ? FString::Printf(TEXT("SOLO:%s"), *LocalIUPS->GetPersistedCharacterUUID()) : TEXT("");
		for (const FIU_TacticalPingEntry& Ping : GameState->ActiveTacticalPings)
		{
			if (ServerNow >= Ping.ExpireServerTime)
			{
				continue;
			}
			const bool bVisibleToMe = (!LocalPartyID.IsEmpty() && Ping.PartyID == LocalPartyID) ||
				(!SoloPingKey.IsEmpty() && Ping.PartyID == SoloPingKey);
			if (!bVisibleToMe)
			{
				continue;
			}

			const FVector BaseProj = Project(Ping.WorldLocation);
			const FVector TopProj = Project(Ping.WorldLocation + FVector(0.0f, 0.0f, 360.0f));
			if (BaseProj.Z > 0.0f && TopProj.Z > 0.0f)
			{
				const FLinearColor BeamOuter(0.18f, 0.92f, 1.0f, 0.88f);
				const FLinearColor BeamCore(1.0f, 0.96f, 0.55f, 0.98f);
				DrawLine(BaseProj.X, BaseProj.Y, TopProj.X, TopProj.Y, BeamOuter, 4.5f);
				DrawLine(BaseProj.X, BaseProj.Y, TopProj.X, TopProj.Y, BeamCore, 1.8f);

				const float R = 10.0f;
				DrawLine(BaseProj.X, BaseProj.Y - R, BaseProj.X + R, BaseProj.Y, BeamCore, 2.0f);
				DrawLine(BaseProj.X + R, BaseProj.Y, BaseProj.X, BaseProj.Y + R, BeamCore, 2.0f);
				DrawLine(BaseProj.X, BaseProj.Y + R, BaseProj.X - R, BaseProj.Y, BeamCore, 2.0f);
				DrawLine(BaseProj.X - R, BaseProj.Y, BaseProj.X, BaseProj.Y - R, BeamCore, 2.0f);

				const float DistMeters = FVector::Dist(PlayerPos, Ping.WorldLocation) / 100.0f;
				const FString PingTxt = FString::Printf(TEXT("[PING: %s | %.0fm]"), *Ping.SenderName, DistMeters);
				DrawRect(FLinearColor(0.02f, 0.08f, 0.14f, 0.86f), TopProj.X - 68.0f, TopProj.Y - 22.0f, 138.0f, 20.0f);
				DrawText(PingTxt, BeamCore, TopProj.X - 62.0f, TopProj.Y - 20.0f, HUDFont, 0.78f);
			}
		}
	}

	// 3. 3D Target Lock-On Diamond Reticle & Locked Target HP Bar
	if (LockedTarget)
	{
		const FVector Projected = Project(TargetingComp->GetLockedTargetAimLocation());
		if (Projected.Z > 0.0f)
		{
			const float TX = Projected.X;
			const float TY = Projected.Y;
			const float Radius = 24.0f;
			const FLinearColor ReticleColor(1.0f, 0.25f, 0.20f, 0.95f);

			DrawLine(TX, TY - Radius, TX + Radius, TY, ReticleColor, 2.2f);
			DrawLine(TX + Radius, TY, TX, TY + Radius, ReticleColor, 2.2f);
			DrawLine(TX, TY + Radius, TX - Radius, TY, ReticleColor, 2.2f);
			DrawLine(TX - Radius, TY, TX, TY - Radius, ReticleColor, 2.2f);

			if (const UIU_AttributeComponent* TargetAttr = LockedTarget->FindComponentByClass<UIU_AttributeComponent>())
			{
				const float BarW = 96.0f;
				const float BarH = 8.0f;
				const float BarX = TX - (BarW * 0.5f);
				const float BarY = TY - Radius - 16.0f;

				DrawRect(FLinearColor(0.05f, 0.05f, 0.05f, 0.82f), BarX - 1.0f, BarY - 1.0f, BarW + 2.0f, BarH + 2.0f);
				DrawRect(FLinearColor(0.95f, 0.22f, 0.18f, 0.95f), BarX, BarY, BarW * TargetAttr->GetHPPercent(), BarH);
			}

			if (const AIU_EnemyCharacter* LockedEnemy = Cast<AIU_EnemyCharacter>(LockedTarget))
			{
				if (LockedEnemy->IsWindingUpAttack() && HUDFont)
				{
					DrawText(TEXT("[ ! RMB EVADE ]"), FLinearColor(1.0f, 0.35f, 0.10f, 1.0f), TX - 56.0f, TY - Radius - 38.0f, HUDFont, 1.15f);
				}
			}
		}
	}

	if (!HUDFont)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const AIU_PlayerController* LocalIUPC = Cast<AIU_PlayerController>(GetOwningPlayerController());
	const bool bProfileOrTradeModalOpen = LocalIUPC && (LocalIUPC->IsInventoryPanelVisible() || LocalIUPC->IsPlayerTradePanelVisible());

	// 3b. Render 3D NRO Capsule Spaceship / Zone Warp Portal Overhead Beacons (Step 6)
	if (!bProfileOrTradeModalOpen)
	{
		for (TActorIterator<AIU_ZonePortalActor> GateIt(World); GateIt; ++GateIt)
		{
			const AIU_ZonePortalActor* Gate = *GateIt;
			if (!IsValid(Gate)) { continue; }
			const float DistM = FVector::Dist(PlayerPos, Gate->GetActorLocation()) / 100.0f;
			if (DistM > 55.0f) { continue; }
			const FVector GateProj = Project(Gate->GetActorLocation() + FVector(0.0f, 0.0f, 155.0f));
			if (GateProj.Z > 0.0f && GateProj.X >= 30.0f && GateProj.X <= Canvas->ClipX - 30.0f)
			{
				const FString GateTxt = FString::Printf(TEXT("[ %s | %.0fm ]"), *Gate->PortalTitle, DistM);
				float GW = 0.0f, GH = 0.0f;
				Canvas->StrLen(HUDFont, GateTxt, GW, GH);
				const float Scale = 0.78f;
				const float BoxW = FMath::Clamp(GW * Scale + 16.0f, 140.0f, 380.0f);
				const float GX = FMath::Clamp(GateProj.X - BoxW * 0.5f, 8.0f, Canvas->ClipX - BoxW - 8.0f);
				const float GY = GateProj.Y - 22.0f;
				DrawRect(FLinearColor(0.03f, 0.14f, 0.24f, 0.88f), GX, GY, BoxW, 22.0f);
				DrawText(GateTxt, FLinearColor(0.42f, 0.96f, 1.0f, 1.0f), GX + 8.0f, GY + 2.0f, HUDFont, Scale);
			}
		}
	}

	// 3c. Step 6 World Boss 3-Phase Raid HUD (HP + Poise/Stagger + Aerial Quake Telegraph + Top-3 Contributors)
	if (!bProfileOrTradeModalOpen)
	{
		const AIU_WorldBossCharacter* NearbyBoss = nullptr;
		for (TActorIterator<AIU_WorldBossCharacter> BossIt(World); BossIt; ++BossIt)
		{
			const AIU_WorldBossCharacter* Cand = *BossIt;
			if (IsValid(Cand) && FVector::Dist(PlayerPos, Cand->GetActorLocation()) <= 4500.0f)
			{
				NearbyBoss = Cand;
				break;
			}
		}

		if (NearbyBoss)
		{
			const UIU_AttributeComponent* BAttr = NearbyBoss->GetAttributeComponent();
			const float CurHP = BAttr ? BAttr->GetHP() : 0.0f;
			const float MaxHP = BAttr ? FMath::Max(1.0f, BAttr->GetMaxHP()) : 850.0f;
			const float HPPct = FMath::Clamp(CurHP / MaxHP, 0.0f, 1.0f);
			const float PoisePct = FMath::Clamp(NearbyBoss->GetCurrentPoise() / FMath::Max(1.0f, NearbyBoss->GetMaxPoise()), 0.0f, 1.0f);
			const int32 PhaseNum = NearbyBoss->GetBossPhase();
			const bool bStaggered = NearbyBoss->IsBossStaggered();

			// On 960x540 split-screen, dock cleanly below Top-Right Telemetry (X=528..936, Y=186..268) so 0px overlap with Vitals/Party/Banners!
			const bool bSmallViewport = (Canvas->ClipX < 1200.0f);
			const float RaidW = bSmallViewport ? 408.0f : 480.0f;
			const float RaidH = 84.0f;
			const float RaidX = bSmallViewport ? (Canvas->ClipX - RaidW - 24.0f) : (CenterX - RaidW * 0.5f);
			const float RaidY = bSmallViewport ? 186.0f : 86.0f;

			DrawRect(FLinearColor(0.14f, 0.05f, 0.04f, 0.90f), RaidX - 2.0f, RaidY - 2.0f, RaidW + 4.0f, RaidH + 4.0f);
			DrawRect(FLinearColor(0.24f, 0.12f, 0.07f, 0.92f), RaidX, RaidY, RaidW, RaidH);

			const FLinearColor TitleCol = (PhaseNum >= 3)
				? FLinearColor(1.0f, 0.36f, 0.22f, 1.0f)
				: (PhaseNum == 2)
				? FLinearColor(1.0f, 0.76f, 0.18f, 1.0f)
				: FLinearColor(1.0f, 0.94f, 0.62f, 1.0f);
			const FString BossHeader = NearbyBoss->IsDead()
				? FString::Printf(TEXT("[DA HA GUC!] WORLD BOSS %s"), *NearbyBoss->BossDisplayName)
				: FString::Printf(TEXT("[PHASE %d/3] %s  |  HP: %.0f / %.0f (%.0f%%)"), PhaseNum, *NearbyBoss->BossDisplayName, CurHP, MaxHP, HPPct * 100.0f);
			DrawText(BossHeader, TitleCol, RaidX + 8.0f, RaidY + 4.0f, HUDFont, 0.78f);

			// Crimson Boss HP Bar with Phase 2 (50%) & Phase 3 (20%) threshold ticks
			const float BarX = RaidX + 8.0f;
			const float BarW = RaidW - 16.0f;
			DrawRect(FLinearColor(0.06f, 0.03f, 0.03f, 0.95f), BarX, RaidY + 24.0f, BarW, 11.0f);
			const FLinearColor HPFill = (PhaseNum >= 3) ? FLinearColor(1.0f, 0.18f, 0.14f, 0.98f) : FLinearColor(0.90f, 0.22f, 0.18f, 0.98f);
			DrawRect(HPFill, BarX, RaidY + 24.0f, BarW * HPPct, 11.0f);
			DrawRect(FLinearColor(1.0f, 0.95f, 0.75f, 0.85f), BarX + BarW * 0.50f, RaidY + 23.0f, 2.0f, 13.0f);
			DrawRect(FLinearColor(1.0f, 0.95f, 0.75f, 0.85f), BarX + BarW * 0.20f, RaidY + 23.0f, 2.0f, 13.0f);

			// Poise / Stagger Bar (Cyan-White flashing when Staggered 1.25x, Amber-Gold while Super Armor active)
			DrawRect(FLinearColor(0.05f, 0.06f, 0.08f, 0.95f), BarX, RaidY + 38.0f, BarW, 7.0f);
			const FLinearColor PoiseCol = bStaggered
				? FLinearColor(0.25f, 0.96f, 1.0f, 1.0f)
				: FLinearColor(0.98f, 0.78f, 0.20f, 0.96f);
			DrawRect(PoiseCol, BarX, RaidY + 38.0f, BarW * (bStaggered ? 1.0f : PoisePct), 7.0f);

			const FString Banner = NearbyBoss->GetActiveTelegraphBanner();
			const FString TopDmg = NearbyBoss->GetTopContributorsText();
			if (!Banner.IsEmpty())
			{
				const FLinearColor WarnCol = bStaggered
					? FLinearColor(0.38f, 1.0f, 0.82f, 1.0f)
					: FLinearColor(1.0f, 0.88f, 0.25f, 1.0f);
				DrawText(Banner.Left(62), WarnCol, BarX, RaidY + 47.0f, HUDFont, 0.72f);
			}
			const FString ContribLine = TopDmg.IsEmpty()
				? TEXT("Top Sat Thuong (>=2% nhan Loot & Huy Chuong Titan): Chua ghi nhan")
				: FString::Printf(TEXT("Top Dame: %s"), *TopDmg);
			DrawText(ContribLine.Left(64), FLinearColor(0.88f, 0.95f, 1.0f, 0.95f), BarX, RaidY + 64.0f, HUDFont, 0.70f);
		}
	}

	// 4. Center-Top Death / Victory Notification Banner & Optional Auto-Demo Stage Header
	if (!bProfileOrTradeModalOpen && !ShowcaseStepBannerText.IsEmpty())
	{
		const float MaxSmallW = FMath::Max(320.0f, Canvas->ClipX - 472.0f);
		const float BannerW = (Canvas->ClipX < 1200.0f) ? FMath::Min(480.0f, MaxSmallW) : FMath::Min(560.0f, Canvas->ClipX - 40.0f);
		const float BannerX = (Canvas->ClipX < 1200.0f) ? 36.0f : (CenterX - BannerW * 0.5f);
		const float BannerTopY = (Canvas->ClipX < 1200.0f) ? 220.0f : 18.0f;
		DrawRect(FLinearColor(0.02f, 0.06f, 0.14f, 0.84f), BannerX, BannerTopY, BannerW, 28.0f);
		DrawText(ShowcaseStepBannerText, FLinearColor(0.45f, 0.92f, 1.0f, 1.0f), BannerX + 10.0f, BannerTopY + 4.0f, HUDFont, 0.82f);
	}

	if (!bProfileOrTradeModalOpen && Now < CenterToastExpireTime && !ActiveCenterToastText.IsEmpty())
	{
		const float MaxSmallToastW = FMath::Max(320.0f, Canvas->ClipX - 472.0f);
		const float ToastW = (Canvas->ClipX < 1200.0f) ? FMath::Min(480.0f, MaxSmallToastW) : FMath::Min(520.0f, Canvas->ClipX - 40.0f);
		const float ToastX = (Canvas->ClipX < 1200.0f) ? 36.0f : (CenterX - ToastW * 0.5f);
		const float BannerY = (Canvas->ClipX < 1200.0f) ? 252.0f : (ShowcaseStepBannerText.IsEmpty() ? 32.0f : 54.0f);
		DrawRect(FLinearColor(0.04f, 0.04f, 0.06f, 0.84f), ToastX, BannerY, ToastW, 28.0f);
		DrawText(ActiveCenterToastText, ActiveCenterToastColor, ToastX + 10.0f, BannerY + 4.0f, HUDFont, 0.82f);
	}

	// 5. Floating 3D->2D Damage Numbers, "PERFECT EVADE!" & "ENEMY DEFEATED!" Popups
	for (int32 i = FloatingTexts.Num() - 1; i >= 0; --i)
	{
		const FIU_FloatingCombatText& Item = FloatingTexts[i];
		const float Age = static_cast<float>(Now - Item.SpawnWorldTime);
		if (Age >= Item.Duration)
		{
			FloatingTexts.RemoveAt(i);
			continue;
		}

		const float Alpha = FMath::Clamp(Age / FMath::Max(Item.Duration, 0.05f), 0.0f, 1.0f);
		const FVector WorldPos = Item.WorldLocation + FVector(0.0f, 0.0f, 55.0f * Alpha);
		const FVector ScreenPos = Project(WorldPos);
		if (ScreenPos.Z > 0.0f)
		{
			FLinearColor DrawCol = Item.Color;
			DrawCol.A *= (1.0f - FMath::Pow(Alpha, 2.0f));
			DrawText(Item.Label, DrawCol, ScreenPos.X - 18.0f, ScreenPos.Y - 10.0f, HUDFont, Item.Scale);
		}
	}

	// 6. Live Gameplay & Multiplayer Debug Telemetry Overlay (Top-Right, toggle with F3 — Criterion 1.7 & Step 2..4)
	if (bShowDebugTelemetry && Canvas->ClipX > 640.0f)
	{
		const float PanelW = 408.0f;
		const float PanelH = 156.0f;
		const float PX = Canvas->ClipX - PanelW - 24.0f;
		const float PY = 24.0f;

		DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.74f), PX, PY, PanelW, PanelH);

		const float FPS = 1.0f / FMath::Max(SmoothedFrameDeltaSeconds, 0.0005f);
		const float FrameMs = SmoothedFrameDeltaSeconds * 1000.0f;
		const double UsedMemMB = static_cast<double>(FPlatformMemory::GetStats().UsedPhysical) / (1024.0 * 1024.0);

		const FVector Vel = OwningPawn->GetVelocity();
		float WalkCfg = 520.0f, SprintCfg = 880.0f, FlyCfg = 1575.0f, AtkMul = 1.0f, FlatAtk = 100.0f, FlatDef = 20.0f;
		if (const AIU_PlayerCharacter* PChar = Cast<AIU_PlayerCharacter>(OwningPawn))
		{
			if (const UIU_MovementComponent* MC = PChar->GetIUMovementComponent())
			{
				WalkCfg = MC->WalkSpeed;
				SprintCfg = MC->SprintSpeed;
			}
			if (const UIU_FlightComponent* FC = PChar->GetFlightComponent())
			{
				FlyCfg = FC->FlightSpeed;
			}
			if (const UIU_AttributeComponent* AC = PChar->GetAttributeComponent())
			{
				AtkMul = AC->GetAttackPowerMultiplier();
				FlatAtk = AC->GetAttackPower();
				FlatDef = AC->GetDefense();
			}
		}

		const ENetMode NetMode = World->GetNetMode();
		const TCHAR* NetModeStr = (NetMode == NM_Client) ? TEXT("DedClient")
			: (NetMode == NM_DedicatedServer) ? TEXT("DedServer")
			: (NetMode == NM_ListenServer) ? TEXT("ListenSrv")
			: TEXT("Standalone");

		float PingMs = 0.0f;
		int32 RepEnemyKills = DefeatedEnemyCount;
		int32 RepPvPKills = 0;
		int32 RepDeaths = PlayerDefeatedCount;
		int32 CharLv = 1, EqCount = 0, KhuID = 1, TitanHC = 0;
		int64 CurEXP = 0, NextEXP = 100, PoolTN = 0, CoinBal = 0, EpochNum = 0;
		FString CharDisplayName = TEXT("OfflineDev");
		if (const APlayerState* RawPS = OwningPawn->GetPlayerState())
		{
			PingMs = RawPS->GetPingInMilliseconds();
			if (const AIU_PlayerState* IUPS = Cast<AIU_PlayerState>(RawPS))
			{
				RepEnemyKills = FMath::Max(RepEnemyKills, IUPS->GetEnemyKills());
				RepPvPKills = IUPS->GetPvPKills();
				RepDeaths = FMath::Max(RepDeaths, IUPS->GetDeaths());
				CharLv = IUPS->GetCharacterLevel();
				CurEXP = IUPS->GetCurrentEXP();
				NextEXP = IUPS->GetNextLevelEXP();
				PoolTN = IUPS->GetPotentialPoints();
				CoinBal = IUPS->GetStandardCoin();
				EqCount = IUPS->GetEquippedItemCount();
				KhuID = IUPS->GetCurrentChannelID();
				TitanHC = IUPS->GetPityTitanTokens();
				EpochNum = IUPS->GetActiveFencingEpoch();
				if (!IUPS->GetPersistedCharacterName().IsEmpty())
				{
					CharDisplayName = IUPS->GetPersistedCharacterName();
				}
			}
		}

		const int32 OnlineCount = GameState ? FMath::Max(1, GameState->PlayerArray.Num()) : 1;
		const bool bInPvPArena = GameState && (GameState->bAllowGlobalPvPSparring || GameState->IsLocationInMartialArena(PlayerPos));
		const FString ZoneLabel = bInPvPArena
			? FString::Printf(TEXT("[KHU %d - PVP]"), KhuID)
			: FString::Printf(TEXT("[KHU %d - PVE]"), KhuID);

		const FLinearColor HeaderCol(0.55f, 0.90f, 1.0f, 0.95f);
		const FLinearColor NetCol = bInPvPArena ? FLinearColor(1.0f, 0.72f, 0.25f, 0.95f) : FLinearColor(0.35f, 0.95f, 0.80f, 0.95f);
		const FLinearColor LineCol(0.90f, 0.95f, 0.98f, 0.92f);

		DrawText(FString::Printf(TEXT("[DEBUG TELEMETRY - F3]  %.0f FPS (%.2f ms) | RAM %.0f MB"), FPS, FrameMs, UsedMemMB), HeaderCol, PX + 10.0f, PY + 8.0f, HUDFont, 0.82f);
		DrawText(FString::Printf(TEXT("Net: %s | Ping: %.0f ms | Online: %d | %s"), NetModeStr, PingMs, OnlineCount, *ZoneLabel), NetCol, PX + 10.0f, PY + 31.0f, HUDFont, 0.80f);
		DrawText(FString::Printf(TEXT("Pos: (%.0f, %.0f, %.0f) | Spd2D: %.0f | Spd3D: %.0f cm/s"), PlayerPos.X, PlayerPos.Y, PlayerPos.Z, Vel.Size2D(), Vel.Size()), LineCol, PX + 10.0f, PY + 54.0f, HUDFont, 0.80f);
		DrawText(FString::Printf(TEXT("Cfg: Atk %.0f (x%.2f) | Giap %.0f | Equip %d/8 | Fly %.0f"), FlatAtk, AtkMul, FlatDef, EqCount, FlyCfg), LineCol, PX + 10.0f, PY + 77.0f, HUDFont, 0.78f);
		DrawText(FString::Printf(TEXT("AI: %d/%d Alive | PvE Kills: %d | PvP Kills: %d | Deaths: %d"), LivingEnemyCount, TotalEnemyActors, RepEnemyKills, RepPvPKills, RepDeaths), LineCol, PX + 10.0f, PY + 100.0f, HUDFont, 0.80f);
		DrawText(FString::Printf(TEXT("DB: %s Lv.%d | TN:%lld | Vang:%lld | HC:%d | Ep#%lld"), *CharDisplayName, CharLv, PoolTN, CoinBal, TitanHC, EpochNum), FLinearColor(1.0f, 0.88f, 0.42f, 0.95f), PX + 10.0f, PY + 123.0f, HUDFont, 0.75f);
	}
}
