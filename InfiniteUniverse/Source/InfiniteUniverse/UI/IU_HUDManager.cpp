// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "UI/IU_HUDManager.h"
#include "UI/IU_PlayerHUDWidget.h"
#include "Core/IU_GameState.h"
#include "Core/IU_PlayerState.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
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
				LocalPS->OnCharacterLevelUp.AddUniqueDynamic(this, &AIU_HUDManager::HandleLocalCharacterLevelUp);
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
	ActiveCenterToastText = FString::Printf(TEXT("[ LEVEL UP!  LV. %d -> LV. %d ]  MaxHP, MaxKi & Attack Power Increased!"), OldLevel, NewLevel);
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

	// 2b. World-Space Overhead HP Bars & Co-op / PvP Sparring Tags for Remote Players (Step 2 Multiplayer)
	const AIU_GameState* GameState = World->GetGameState<AIU_GameState>();
	for (TActorIterator<AIU_PlayerCharacter> It(World); It; ++It)
	{
		AIU_PlayerCharacter* OtherPlayer = *It;
		if (!IsValid(OtherPlayer) || OtherPlayer == OwningPawn)
		{
			continue;
		}
		const UIU_AttributeComponent* PAttr = OtherPlayer->GetAttributeComponent();
		if (!PAttr || PAttr->IsDead() || OtherPlayer == LockedTarget)
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

		const bool bCanSpar = GameState ? GameState->ArePlayersAllowedToSpar(OwningPawn, OtherPlayer) : true;
		const float BarW = 84.0f;
		const float BarH = 7.0f;
		const float BarX = HeadProj.X - (BarW * 0.5f);
		const float BarY = HeadProj.Y;

		DrawRect(FLinearColor(0.03f, 0.04f, 0.06f, 0.80f), BarX - 1.0f, BarY - 1.0f, BarW + 2.0f, BarH + 2.0f);
		const FLinearColor PlayerBarCol = bCanSpar
			? FLinearColor(1.0f, 0.55f, 0.12f, 0.95f)
			: FLinearColor(0.15f, 0.90f, 0.85f, 0.92f);
		DrawRect(PlayerBarCol, BarX, BarY, BarW * PAttr->GetHPPercent(), BarH);

		if (HUDFont)
		{
			int32 PlayerID = 0;
			int32 OtherLv = 1;
			FString OtherName;
			if (const APlayerState* OtherPS = OtherPlayer->GetPlayerState())
			{
				PlayerID = OtherPS->GetPlayerId();
				if (const AIU_PlayerState* OtherIUPS = Cast<AIU_PlayerState>(OtherPS))
				{
					OtherLv = OtherIUPS->GetCharacterLevel();
					OtherName = OtherIUPS->GetPersistedCharacterName();
				}
			}
			FString RoleTag;
			if (!OtherName.IsEmpty())
			{
				RoleTag = FString::Printf(TEXT("[Lv.%d %s]"), OtherLv, *OtherName);
			}
			else
			{
				RoleTag = bCanSpar
					? FString::Printf(TEXT("[SPAR RIVAL #%d]"), PlayerID)
					: FString::Printf(TEXT("[CO-OP ALLY #%d]"), PlayerID);
			}
			DrawText(RoleTag, PlayerBarCol, BarX - 18.0f, BarY - 20.0f, HUDFont, 0.88f);
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

	// 4. Center-Top Death / Victory Notification Banner & Optional Auto-Demo Stage Header
	if (!ShowcaseStepBannerText.IsEmpty())
	{
		const float MaxSmallW = FMath::Max(320.0f, Canvas->ClipX - 472.0f);
		const float BannerW = (Canvas->ClipX < 1200.0f) ? FMath::Min(480.0f, MaxSmallW) : FMath::Min(560.0f, Canvas->ClipX - 40.0f);
		const float BannerX = (Canvas->ClipX < 1200.0f) ? 36.0f : (CenterX - BannerW * 0.5f);
		const float BannerTopY = (Canvas->ClipX < 1200.0f) ? 194.0f : 18.0f;
		DrawRect(FLinearColor(0.02f, 0.06f, 0.14f, 0.84f), BannerX, BannerTopY, BannerW, 28.0f);
		DrawText(ShowcaseStepBannerText, FLinearColor(0.45f, 0.92f, 1.0f, 1.0f), BannerX + 10.0f, BannerTopY + 4.0f, HUDFont, 0.84f);
	}

	if (Now < CenterToastExpireTime && !ActiveCenterToastText.IsEmpty())
	{
		const float MaxSmallToastW = FMath::Max(320.0f, Canvas->ClipX - 472.0f);
		const float ToastW = (Canvas->ClipX < 1200.0f) ? FMath::Min(480.0f, MaxSmallToastW) : FMath::Min(490.0f, Canvas->ClipX - 40.0f);
		const float ToastX = (Canvas->ClipX < 1200.0f) ? 36.0f : (CenterX - ToastW * 0.5f);
		const float BannerY = (Canvas->ClipX < 1200.0f) ? 226.0f : (ShowcaseStepBannerText.IsEmpty() ? 32.0f : 54.0f);
		DrawRect(FLinearColor(0.04f, 0.04f, 0.06f, 0.84f), ToastX, BannerY, ToastW, 28.0f);
		DrawText(ActiveCenterToastText, ActiveCenterToastColor, ToastX + 10.0f, BannerY + 4.0f, HUDFont, 0.84f);
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

	// 6. Live Gameplay & Multiplayer Debug Telemetry Overlay (Top-Right, toggle with F3 — Criterion 1.7 & Step 2)
	if (bShowDebugTelemetry && Canvas->ClipX > 640.0f)
	{
		const float PanelW = 398.0f;
		const float PanelH = 152.0f;
		const float PX = Canvas->ClipX - PanelW - 24.0f;
		const float PY = 24.0f;

		DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.74f), PX, PY, PanelW, PanelH);

		const float FPS = 1.0f / FMath::Max(SmoothedFrameDeltaSeconds, 0.0005f);
		const float FrameMs = SmoothedFrameDeltaSeconds * 1000.0f;
		const double UsedMemMB = static_cast<double>(FPlatformMemory::GetStats().UsedPhysical) / (1024.0 * 1024.0);

		const FVector Vel = OwningPawn->GetVelocity();
		float WalkCfg = 520.0f, SprintCfg = 880.0f, FlyCfg = 1575.0f, AtkMul = 1.0f;
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
		int32 CharLv = 1;
		int64 CurEXP = 0, NextEXP = 100, EpochNum = 0;
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
				EpochNum = IUPS->GetActiveFencingEpoch();
				if (!IUPS->GetPersistedCharacterName().IsEmpty())
				{
					CharDisplayName = IUPS->GetPersistedCharacterName();
				}
			}
		}

		const int32 OnlineCount = GameState ? FMath::Max(1, GameState->PlayerArray.Num()) : 1;
		const bool bInPvPArena = GameState && (GameState->bAllowGlobalPvPSparring || GameState->IsLocationInMartialArena(PlayerPos));
		const TCHAR* ZoneLabel = bInPvPArena ? TEXT("[PVP ARENA]") : TEXT("[CO-OP PVE]");

		const FLinearColor HeaderCol(0.55f, 0.90f, 1.0f, 0.95f);
		const FLinearColor NetCol = bInPvPArena ? FLinearColor(1.0f, 0.72f, 0.25f, 0.95f) : FLinearColor(0.35f, 0.95f, 0.80f, 0.95f);
		const FLinearColor LineCol(0.90f, 0.95f, 0.98f, 0.92f);

		DrawText(FString::Printf(TEXT("[DEBUG TELEMETRY - F3]  %.0f FPS (%.2f ms) | RAM %.0f MB"), FPS, FrameMs, UsedMemMB), HeaderCol, PX + 10.0f, PY + 8.0f, HUDFont, 0.82f);
		DrawText(FString::Printf(TEXT("Net: %s | Ping: %.0f ms | Online: %d | %s"), NetModeStr, PingMs, OnlineCount, ZoneLabel), NetCol, PX + 10.0f, PY + 31.0f, HUDFont, 0.80f);
		DrawText(FString::Printf(TEXT("Pos: (%.0f, %.0f, %.0f) | Spd2D: %.0f | Spd3D: %.0f cm/s"), PlayerPos.X, PlayerPos.Y, PlayerPos.Z, Vel.Size2D(), Vel.Size()), LineCol, PX + 10.0f, PY + 54.0f, HUDFont, 0.80f);
		DrawText(FString::Printf(TEXT("Cfg: Atk x%.2f | Walk %.0f | Sprint %.0f | Fly %.0f"), AtkMul, WalkCfg, SprintCfg, FlyCfg), LineCol, PX + 10.0f, PY + 77.0f, HUDFont, 0.80f);
		DrawText(FString::Printf(TEXT("AI: %d/%d Alive | PvE Kills: %d | PvP Kills: %d | Deaths: %d"), LivingEnemyCount, TotalEnemyActors, RepEnemyKills, RepPvPKills, RepDeaths), LineCol, PX + 10.0f, PY + 100.0f, HUDFont, 0.80f);
		DrawText(FString::Printf(TEXT("DB: %s Lv.%d (%lld/%lld EXP) | Epoch #%lld (F5: Auth)"), *CharDisplayName, CharLv, CurEXP, NextEXP, EpochNum), FLinearColor(1.0f, 0.88f, 0.42f, 0.95f), PX + 10.0f, PY + 123.0f, HUDFont, 0.78f);
	}

	// 7. Compact Bottom-Left Controls Quick-Bar
	if (bShowControlsLegend && Canvas->ClipY > 360.0f)
	{
		const float LegendX = 36.0f;
		const float LegendY = Canvas->ClipY - 52.0f;
		DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.62f), LegendX - 10.0f, LegendY - 6.0f, 990.0f, 30.0f);
		DrawText(
			TEXT("WASD: Move | Space x2: 3D Flight | Ctrl/Shift: Boost/Down | LMB: Combo | Q: Ki Blast | RMB: Dash/Evade | E: Lock-On | V: Blink | F3: Debug | F4: PvP"),
			FLinearColor(0.86f, 0.93f, 1.0f, 0.90f),
			LegendX,
			LegendY,
			HUDFont,
			0.84f);
	}
}
