// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "UI/IU_HUDManager.h"
#include "UI/IU_PlayerHUDWidget.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Combat/IU_TargetingComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
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

void AIU_HUDManager::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	APawn* OwningPawn = GetOwningPawn();

	// 1. Genshin-style crisp center aim reticle (highlights warm gold when an enemy is in crosshair sightline)
	bool bEnemyInCrosshair = false;
	if (const AIU_PlayerCharacter* PlayerChar = Cast<AIU_PlayerCharacter>(OwningPawn))
	{
		if (UWorld* World = GetWorld())
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
	const float Gap = 4.0f;
	const float Arm = 10.0f;

	DrawRect(AimColor, CenterX - 1.5f, CenterY - 1.5f, 3.0f, 3.0f);
	DrawLine(CenterX - Gap - Arm, CenterY, CenterX - Gap, CenterY, AimColor, 1.6f);
	DrawLine(CenterX + Gap, CenterY, CenterX + Gap + Arm, CenterY, AimColor, 1.6f);
	DrawLine(CenterX, CenterY - Gap - Arm, CenterX, CenterY - Gap, AimColor, 1.6f);
	DrawLine(CenterX, CenterY + Gap, CenterX, CenterY + Gap + Arm, AimColor, 1.6f);

	// 2. 3D Target Lock-On Diamond Reticle & Target HP Bar
	if (!OwningPawn)
	{
		return;
	}

	const UIU_TargetingComponent* TargetingComp = OwningPawn->FindComponentByClass<UIU_TargetingComponent>();
	if (!TargetingComp || !TargetingComp->IsLockedOn())
	{
		return;
	}

	AActor* Target = TargetingComp->GetLockedTarget();
	if (!Target)
	{
		return;
	}

	const FVector Projected = Project(TargetingComp->GetLockedTargetAimLocation());
	if (Projected.Z <= 0.0f)
	{
		return;
	}

	const float TX = Projected.X;
	const float TY = Projected.Y;
	const float Radius = 24.0f;
	const FLinearColor ReticleColor(1.0f, 0.25f, 0.20f, 0.95f);

	// Draw diamond brackets around locked target
	DrawLine(TX, TY - Radius, TX + Radius, TY, ReticleColor, 2.2f);
	DrawLine(TX + Radius, TY, TX, TY + Radius, ReticleColor, 2.2f);
	DrawLine(TX, TY + Radius, TX - Radius, TY, ReticleColor, 2.2f);
	DrawLine(TX - Radius, TY, TX, TY - Radius, ReticleColor, 2.2f);

	// Draw Target HP bar right above reticle
	if (const UIU_AttributeComponent* TargetAttr = Target->FindComponentByClass<UIU_AttributeComponent>())
	{
		const float BarW = 90.0f;
		const float BarH = 8.0f;
		const float BarX = TX - (BarW * 0.5f);
		const float BarY = TY - Radius - 16.0f;

		DrawRect(FLinearColor(0.05f, 0.05f, 0.05f, 0.80f), BarX - 1.0f, BarY - 1.0f, BarW + 2.0f, BarH + 2.0f);
		DrawRect(FLinearColor(0.95f, 0.22f, 0.18f, 0.95f), BarX, BarY, BarW * TargetAttr->GetHPPercent(), BarH);
	}
}
