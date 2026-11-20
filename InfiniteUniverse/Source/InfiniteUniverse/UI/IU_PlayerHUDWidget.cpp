// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "UI/IU_PlayerHUDWidget.h"
#include "Core/IU_PlayerState.h"
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
#include "Fonts/SlateFontInfo.h"

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
			BoxSlot->SetPosition(FVector2D(36.0f, 18.0f));
			BoxSlot->SetSize(FVector2D(475.0f, 202.0f));
		}

		auto StyleHUDText = [](UTextBlock* TB, const FLinearColor& Col, int32 FontPt = 13)
		{
			if (!TB)
			{
				return;
			}
			FSlateFontInfo F = TB->GetFont();
			F.Size = FontPt;
			TB->SetFont(F);
			TB->SetColorAndOpacity(FSlateColor(Col));
			TB->SetShadowOffset(FVector2D(1.5f, 1.5f));
			TB->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.92f));
		};

		LevelEXPValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LevelEXPValueText"));
		StyleHUDText(LevelEXPValueText, FLinearColor(1.0f, 0.88f, 0.32f), 13);
		LevelEXPValueText->SetText(FText::FromString(TEXT("LV. 1  |  SUC MANH: 0 / 100")));
		VitalsBox->AddChildToVerticalBox(LevelEXPValueText);

		EXPProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("EXPProgressBar"));
		EXPProgressBar->SetFillColorAndOpacity(FLinearColor(0.95f, 0.75f, 0.15f, 1.0f));
		if (UVerticalBoxSlot* EXPSlot = VitalsBox->AddChildToVerticalBox(EXPProgressBar))
		{
			EXPSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 5.0f));
		}

		HealthValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HealthValueText"));
		StyleHUDText(HealthValueText, FLinearColor(0.95f, 1.0f, 0.95f), 13);
		VitalsBox->AddChildToVerticalBox(HealthValueText);

		HealthProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HealthProgressBar"));
		HealthProgressBar->SetFillColorAndOpacity(FLinearColor(0.18f, 0.85f, 0.32f, 1.0f));
		if (UVerticalBoxSlot* HPSlot = VitalsBox->AddChildToVerticalBox(HealthProgressBar))
		{
			HPSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 5.0f));
		}

		KiValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("KiValueText"));
		StyleHUDText(KiValueText, FLinearColor(0.75f, 0.95f, 1.0f), 13);
		VitalsBox->AddChildToVerticalBox(KiValueText);

		KiProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("KiProgressBar"));
		KiProgressBar->SetFillColorAndOpacity(FLinearColor(0.10f, 0.68f, 1.0f, 1.0f));
		if (UVerticalBoxSlot* KiSlot = VitalsBox->AddChildToVerticalBox(KiProgressBar))
		{
			KiSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 5.0f));
		}

		PotentialValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PotentialValueText"));
		StyleHUDText(PotentialValueText, FLinearColor(0.35f, 1.0f, 0.72f), 12);
		PotentialValueText->SetText(FText::FromString(TEXT("TN: 0 | ATK: 100 | GIAP: 20 | VANG: 0 | DAU[4]: 0")));
		if (UVerticalBoxSlot* PotSlot = VitalsBox->AddChildToVerticalBox(PotentialValueText))
		{
			PotSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 3.0f));
		}

		StatusReadoutText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusReadoutText"));
		StyleHUDText(StatusReadoutText, FLinearColor(1.0f, 0.88f, 0.35f), 12);
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
		Attr->OnAttackPowerChanged.AddDynamic(this, &UIU_PlayerHUDWidget::HandleAttackPowerChanged);
		Attr->OnDefenseChanged.AddDynamic(this, &UIU_PlayerHUDWidget::HandleDefenseChanged);

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

	EnsurePlayerStateBound(NewPawn);
	RefreshPotentialReadout();
	RefreshStatusText();
}

void UIU_PlayerHUDWidget::EnsurePlayerStateBound(APawn* CurrentPawn)
{
	if (!CurrentPawn)
	{
		return;
	}
	AIU_PlayerState* IUPS = CurrentPawn->GetPlayerState<AIU_PlayerState>();
	if (!IUPS || BoundPlayerState.Get() == IUPS)
	{
		return;
	}

	if (AIU_PlayerState* OldPS = BoundPlayerState.Get())
	{
		OldPS->OnCharacterEXPChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterEXPChanged);
		OldPS->OnCharacterPotentialChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterPotentialChanged);
		OldPS->OnCharacterEconomyChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterEconomyChanged);
	}

	BoundPlayerState = IUPS;
	IUPS->OnCharacterEXPChanged.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterEXPChanged);
	IUPS->OnCharacterPotentialChanged.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterPotentialChanged);
	IUPS->OnCharacterEconomyChanged.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterEconomyChanged);
	HandleCharacterEXPChanged(IUPS->GetCurrentEXP(), IUPS->GetNextLevelEXP(), IUPS->GetCharacterLevel());
	RefreshPotentialReadout();
}

void UIU_PlayerHUDWidget::UnbindCurrentComponents()
{
	if (UIU_AttributeComponent* Attr = BoundAttributeComp.Get())
	{
		Attr->OnHPChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleHPChanged);
		Attr->OnKiChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleKiChanged);
		Attr->OnAttackPowerChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleAttackPowerChanged);
		Attr->OnDefenseChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleDefenseChanged);
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
	if (AIU_PlayerState* PS = BoundPlayerState.Get())
	{
		PS->OnCharacterEXPChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterEXPChanged);
		PS->OnCharacterPotentialChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterPotentialChanged);
		PS->OnCharacterEconomyChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterEconomyChanged);
	}

	BoundAttributeComp.Reset();
	BoundCombatComp.Reset();
	BoundFlightComp.Reset();
	BoundPlayerState.Reset();
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

void UIU_PlayerHUDWidget::HandleCharacterEXPChanged(int64 CurrentEXP, int64 NextLevelEXP, int32 CurrentLevel)
{
	if (EXPProgressBar)
	{
		const float Pct = NextLevelEXP > 0 ? FMath::Clamp(static_cast<float>(CurrentEXP) / static_cast<float>(NextLevelEXP), 0.0f, 1.0f) : 1.0f;
		EXPProgressBar->SetPercent(Pct);
	}
	if (LevelEXPValueText)
	{
		FString NamePrefix;
		if (const AIU_PlayerState* PS = BoundPlayerState.Get())
		{
			if (!PS->GetPersistedCharacterName().IsEmpty())
			{
				NamePrefix = FString::Printf(TEXT("[%s]  "), *PS->GetPersistedCharacterName());
			}
		}
		LevelEXPValueText->SetText(FText::FromString(
			FString::Printf(TEXT("%sLV. %d  |  SUC MANH: %lld / %lld"), *NamePrefix, CurrentLevel, CurrentEXP, NextLevelEXP)));
	}
}

void UIU_PlayerHUDWidget::HandleAttackPowerChanged(float NewAttackPower)
{
	RefreshPotentialReadout();
}

void UIU_PlayerHUDWidget::HandleDefenseChanged(float NewDefense)
{
	RefreshPotentialReadout();
}

void UIU_PlayerHUDWidget::HandleCharacterPotentialChanged(int64 PotentialPoints, int64 NextHPCost, int64 NextKiCost, int64 NextAtkCost)
{
	RefreshPotentialReadout();
}

void UIU_PlayerHUDWidget::HandleCharacterEconomyChanged(int64 StandardCoin, int32 ElixirCount, int32 CrimsonOreCount, int32 EquippedCount)
{
	RefreshPotentialReadout();
}

void UIU_PlayerHUDWidget::RefreshPotentialReadout()
{
	if (!PotentialValueText)
	{
		return;
	}
	const int64 PoolTN = BoundPlayerState.IsValid() ? BoundPlayerState->GetPotentialPoints() : 0;
	const int64 Coin = BoundPlayerState.IsValid() ? BoundPlayerState->GetStandardCoin() : 0;
	const int32 Elixirs = BoundPlayerState.IsValid() ? BoundPlayerState->GetElixirCount() : 0;
	const float Atk = BoundAttributeComp.IsValid() ? BoundAttributeComp->GetAttackPower() : 100.0f;
	const float Def = BoundAttributeComp.IsValid() ? BoundAttributeComp->GetDefense() : 20.0f;
	PotentialValueText->SetText(FText::FromString(
		FString::Printf(TEXT("TN: %lld | ATK: %.0f | GIAP: %.0f | VANG: %lld | DAU[4]: %d"), PoolTN, Atk, Def, Coin, Elixirs)));
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
