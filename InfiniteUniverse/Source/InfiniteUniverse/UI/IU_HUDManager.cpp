// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "UI/IU_HUDManager.h"
#include "UI/IU_PlayerHUDWidget.h"
#include "Combat/IU_TargetingComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Engine/Canvas.h"
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

	// 1. Subtle center aim crosshair for free-aim Ki Blasts
	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	DrawLine(CenterX - 7.0f, CenterY, CenterX + 7.0f, CenterY, FLinearColor(1.0f, 1.0f, 1.0f, 0.55f), 1.5f);
	DrawLine(CenterX, CenterY - 7.0f, CenterX, CenterY + 7.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.55f), 1.5f);

	// 2. 3D Target Lock-On Diamond Reticle & Target HP Bar
	APawn* OwningPawn = GetOwningPawn();
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
