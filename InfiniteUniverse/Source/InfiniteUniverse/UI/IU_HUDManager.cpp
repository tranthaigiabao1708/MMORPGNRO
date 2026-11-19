// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "UI/IU_HUDManager.h"
#include "UI/IU_PlayerHUDWidget.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
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
			ActiveHUDWidget->BindToPlayerPawn(PC->GetPawn());
		}
	}
}

void AIU_HUDManager::RefreshCombatEventBindings(APawn* OwningPawn)
{
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
	NextBindingScanTime = Now + 0.50;

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

void AIU_HUDManager::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	UWorld* World = GetWorld();
	APawn* OwningPawn = GetOwningPawn();
	RefreshCombatEventBindings(OwningPawn);

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
					bEnemyInCrosshair = (HitAttr && HitDmg && !HitAttr->IsDead());
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

	// 2. World-Space Overhead Enemy HP Bars & Crimson "[ ! RMB EVADE ]" Windup Telegraph
	const FVector PlayerPos = OwningPawn->GetActorLocation();
	for (TActorIterator<AIU_EnemyCharacter> It(World); It; ++It)
	{
		AIU_EnemyCharacter* Enemy = *It;
		if (!IsValid(Enemy) || Enemy->IsDead() || Enemy == LockedTarget)
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

	// 4. Floating 3D->2D Damage Numbers & "PERFECT EVADE!" Popups
	if (HUDFont)
	{
		const double Now = World->GetTimeSeconds();
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

		// 5. Compact Bottom-Left Controls Quick-Bar
		if (bShowControlsLegend && Canvas->ClipY > 360.0f)
		{
			const float LegendX = 36.0f;
			const float LegendY = Canvas->ClipY - 52.0f;
			DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.62f), LegendX - 10.0f, LegendY - 6.0f, 890.0f, 30.0f);
			DrawText(
				TEXT("WASD: Move  |  Space x2: 3D Flight  |  Ctrl/Shift: Boost/Down  |  LMB: Combo  |  Q: Ki Blast  |  RMB: Dash / Mirage Evade  |  E: Lock-On  |  R: Blink Mode"),
				FLinearColor(0.86f, 0.93f, 1.0f, 0.90f),
				LegendX,
				LegendY,
				HUDFont,
				0.88f);
		}
	}
}
