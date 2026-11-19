// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "UI/IU_PlayerHUDWidget.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Combat/IU_CombatComponent.h"
#include "Movement/IU_FlightComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

TSharedRef<SWidget> UIU_PlayerHUDWidget::RebuildWidget()
{
	// Construct clean default UMG bars programmatically if WidgetTree has no root yet
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
		WidgetTree->RootWidget = RootCanvas;

		UVerticalBox* VitalsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("VitalsBox"));
		if (UCanvasPanelSlot* BoxSlot = RootCanvas->AddChildToCanvas(VitalsBox))
		{
			BoxSlot->SetAnchors(FAnchors(0.0f, 0.0f));
			BoxSlot->SetPosition(FVector2D(36.0f, 32.0f));
			BoxSlot->SetSize(FVector2D(360.0f, 150.0f));
		}

		HealthValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HealthValueText"));
		HealthValueText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 1.0f, 0.95f)));
		VitalsBox->AddChildToVerticalBox(HealthValueText);

		HealthProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HealthProgressBar"));
		HealthProgressBar->SetFillColorAndOpacity(FLinearColor(0.18f, 0.85f, 0.32f, 1.0f));
		if (UVerticalBoxSlot* HPSlot = VitalsBox->AddChildToVerticalBox(HealthProgressBar))
		{
			HPSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 10.0f));
		}

		KiValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("KiValueText"));
		KiValueText->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.95f, 1.0f)));
		VitalsBox->AddChildToVerticalBox(KiValueText);

		KiProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("KiProgressBar"));
		KiProgressBar->SetFillColorAndOpacity(FLinearColor(0.10f, 0.68f, 1.0f, 1.0f));
		if (UVerticalBoxSlot* KiSlot = VitalsBox->AddChildToVerticalBox(KiProgressBar))
		{
			KiSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 10.0f));
		}

		StatusReadoutText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusReadoutText"));
		StatusReadoutText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.88f, 0.35f)));
		VitalsBox->AddChildToVerticalBox(StatusReadoutText);
	}

	return Super::RebuildWidget();
}

void UIU_PlayerHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindToPlayerPawn(GetOwningPlayerPawn());
}

void UIU_PlayerHUDWidget::NativeDestruct()
{
	UnbindCurrentComponents();
	Super::NativeDestruct();
}

void UIU_PlayerHUDWidget::BindToPlayerPawn(APawn* NewPawn)
{
	UnbindCurrentComponents();
	if (!NewPawn)
	{
		return;
	}

	if (UIU_AttributeComponent* Attr = NewPawn->FindComponentByClass<UIU_AttributeComponent>())
	{
		BoundAttributeComp = Attr;
		Attr->OnHPChanged.AddDynamic(this, &UIU_PlayerHUDWidget::HandleHPChanged);
		Attr->OnKiChanged.AddDynamic(this, &UIU_PlayerHUDWidget::HandleKiChanged);

		HandleHPChanged(Attr->GetHP(), Attr->GetMaxHP(), 0.0f);
		HandleKiChanged(Attr->GetKi(), Attr->GetMaxKi(), 0.0f);
	}

	if (UIU_CombatComponent* Combat = NewPawn->FindComponentByClass<UIU_CombatComponent>())
	{
		BoundCombatComp = Combat;
		Combat->OnCombatStateChanged.AddDynamic(this, &UIU_PlayerHUDWidget::HandleCombatStateChanged);
		Combat->OnComboStepExecuted.AddDynamic(this, &UIU_PlayerHUDWidget::HandleComboStepExecuted);
		Combat->OnMobilityStyleChanged.AddDynamic(this, &UIU_PlayerHUDWidget::HandleMobilityStyleChanged);
		DisplayedCombatState = Combat->GetCombatState();
		DisplayedComboStep = Combat->GetCurrentComboStep();
		DisplayedMobilityStyle = Combat->GetMobilityStyle();
	}

	if (UIU_FlightComponent* Flight = NewPawn->FindComponentByClass<UIU_FlightComponent>())
	{
		BoundFlightComp = Flight;
		Flight->OnFlightStateChanged.AddDynamic(this, &UIU_PlayerHUDWidget::HandleFlightStateChanged);
		bDisplayedFlying = Flight->IsFlying();
	}

	RefreshStatusText();
}

void UIU_PlayerHUDWidget::UnbindCurrentComponents()
{
	if (UIU_AttributeComponent* Attr = BoundAttributeComp.Get())
	{
		Attr->OnHPChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleHPChanged);
		Attr->OnKiChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleKiChanged);
	}
	if (UIU_CombatComponent* Combat = BoundCombatComp.Get())
	{
		Combat->OnCombatStateChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCombatStateChanged);
		Combat->OnComboStepExecuted.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleComboStepExecuted);
		Combat->OnMobilityStyleChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleMobilityStyleChanged);
	}
	if (UIU_FlightComponent* Flight = BoundFlightComp.Get())
	{
		Flight->OnFlightStateChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleFlightStateChanged);
	}

	BoundAttributeComp.Reset();
	BoundCombatComp.Reset();
	BoundFlightComp.Reset();
}

void UIU_PlayerHUDWidget::HandleHPChanged(float CurrentHP, float MaxHP, float Delta)
{
	if (HealthProgressBar)
	{
		HealthProgressBar->SetPercent(MaxHP > 0.0f ? (CurrentHP / MaxHP) : 0.0f);
	}
	if (HealthValueText)
	{
		HealthValueText->SetText(FText::FromString(FString::Printf(TEXT("HP: %.0f / %.0f"), CurrentHP, MaxHP)));
	}
}

void UIU_PlayerHUDWidget::HandleKiChanged(float CurrentKi, float MaxKi, float Delta)
{
	if (KiProgressBar)
	{
		KiProgressBar->SetPercent(MaxKi > 0.0f ? (CurrentKi / MaxKi) : 0.0f);
	}
	if (KiValueText)
	{
		KiValueText->SetText(FText::FromString(FString::Printf(TEXT("KI: %.0f / %.0f"), CurrentKi, MaxKi)));
	}
}

void UIU_PlayerHUDWidget::HandleCombatStateChanged(EIU_CombatState OldState, EIU_CombatState NewState)
{
	DisplayedCombatState = NewState;
	if (NewState == EIU_CombatState::Idle)
	{
		DisplayedComboStep = 0;
	}
	RefreshStatusText();
}

void UIU_PlayerHUDWidget::HandleComboStepExecuted(int32 ComboStep)
{
	DisplayedComboStep = ComboStep;
	RefreshStatusText();
}

void UIU_PlayerHUDWidget::HandleFlightStateChanged(bool bIsFlying)
{
	bDisplayedFlying = bIsFlying;
	RefreshStatusText();
}

void UIU_PlayerHUDWidget::HandleMobilityStyleChanged(EIU_MobilityStepStyle NewStyle)
{
	DisplayedMobilityStyle = NewStyle;
	RefreshStatusText();
}

void UIU_PlayerHUDWidget::RefreshStatusText()
{
	if (!StatusReadoutText)
	{
		return;
	}

	const TCHAR* MoveLabel = bDisplayedFlying ? TEXT("FLIGHT 3D") : TEXT("GROUND");
	const TCHAR* StyleLabel = (DisplayedMobilityStyle == EIU_MobilityStepStyle::InstantBlink)
		? TEXT("ALIEN BLINK")
		: TEXT("EARTH DASH");

	FString CombatLabel = TEXT("READY");
	if (DisplayedCombatState == EIU_CombatState::Attacking && DisplayedComboStep > 0)
	{
		CombatLabel = FString::Printf(TEXT("COMBO HIT %d/3"), DisplayedComboStep);
	}
	else if (DisplayedCombatState == EIU_CombatState::Dodging)
	{
		const bool bCombatEvade = (BoundCombatComp.IsValid() && BoundCombatComp->IsLastDodgeCombatEvade());
		if (bCombatEvade)
		{
			CombatLabel = TEXT("MIRAGE EVADE (AFTERIMAGE)");
		}
		else
		{
			CombatLabel = (DisplayedMobilityStyle == EIU_MobilityStepStyle::InstantBlink)
				? TEXT("INSTANT BLINK")
				: TEXT("QUICK DASH");
		}
	}
	else if (DisplayedCombatState == EIU_CombatState::HitReaction)
	{
		CombatLabel = TEXT("STAGGERED");
	}
	else if (DisplayedCombatState == EIU_CombatState::Defeated)
	{
		CombatLabel = TEXT("DEFEATED");
	}

	StatusReadoutText->SetText(FText::FromString(FString::Printf(TEXT("[%s | %s]  %s"), MoveLabel, StyleLabel, *CombatLabel)));
}
