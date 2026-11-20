// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "UI/IU_PotentialUpgradeWidget.h"
#include "Core/IU_PlayerController.h"
#include "Core/IU_PlayerState.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/Pawn.h"

TSharedRef<SWidget> UIU_PotentialUpgradeWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("PotentialRootCanvas"));
		WidgetTree->RootWidget = RootCanvas;

		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PotentialModalCard"));
		Card->SetBrushColor(FLinearColor(0.02f, 0.04f, 0.10f, 0.95f));
		Card->SetPadding(FMargin(22.0f, 18.0f));
		if (UCanvasPanelSlot* CardSlot = RootCanvas->AddChildToCanvas(Card))
		{
			CardSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CardSlot->SetPosition(FVector2D(0.0f, 14.0f));
			CardSlot->SetAutoSize(true);
		}

		auto ApplyFontSize = [](UTextBlock* TB, int32 SizePt)
		{
			if (!TB)
			{
				return;
			}
			FSlateFontInfo F = TB->GetFont();
			F.Size = SizePt;
			TB->SetFont(F);
			TB->SetShadowOffset(FVector2D(1.2f, 1.2f));
			TB->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.90f));
		};

		UVerticalBox* VBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PotentialVBox"));
		Card->SetContent(VBox);

		UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PotentialTitle"));
		Title->SetText(FText::FromString(TEXT("BANG CHI SO & TIEM NANG (NRO PROGRESSION - PHIM P)")));
		Title->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.88f, 0.28f)));
		ApplyFontSize(Title, 14);
		if (UVerticalBoxSlot* S = VBox->AddChildToVerticalBox(Title))
		{
			S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		}

		UTextBlock* SubRule = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PotentialRule"));
		SubRule->SetText(FText::FromString(TEXT("Moi don danh nhan 1.5x Dame -> Suc Manh (chi tang Lv) & Tiem Nang (cong chi so):")));
		SubRule->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.88f, 0.98f)));
		ApplyFontSize(SubRule, 11);
		if (UVerticalBoxSlot* S = VBox->AddChildToVerticalBox(SubRule))
		{
			S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		}

		LevelSummaryText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LevelSummaryText"));
		LevelSummaryText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 1.0f)));
		ApplyFontSize(LevelSummaryText, 12);
		if (UVerticalBoxSlot* S = VBox->AddChildToVerticalBox(LevelSummaryText))
		{
			S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		}

		PotentialPoolText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PotentialPoolText"));
		PotentialPoolText->SetColorAndOpacity(FSlateColor(FLinearColor(0.32f, 1.0f, 0.68f)));
		ApplyFontSize(PotentialPoolText, 13);
		if (UVerticalBoxSlot* S = VBox->AddChildToVerticalBox(PotentialPoolText))
		{
			S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
		}

		auto AddStatUpgradeRow = [&](const TCHAR* RowName, TObjectPtr<UTextBlock>& OutStatLabel, const FLinearColor& StatCol, TObjectPtr<UButton>& OutBtn, TObjectPtr<UTextBlock>& OutBtnLbl)
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), RowName);
			if (UVerticalBoxSlot* RS = VBox->AddChildToVerticalBox(Row))
			{
				RS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
			}

			USizeBox* LeftBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			LeftBox->SetMinDesiredWidth(340.0f);
			OutStatLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			OutStatLabel->SetColorAndOpacity(FSlateColor(StatCol));
			ApplyFontSize(OutStatLabel, 12);
			LeftBox->SetContent(OutStatLabel);
			if (UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(LeftBox))
			{
				LS->SetVerticalAlignment(VAlign_Center);
				LS->SetPadding(FMargin(0.0f, 0.0f, 18.0f, 0.0f));
			}

			USizeBox* BtnBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			BtnBox->SetMinDesiredWidth(235.0f);
			OutBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
			OutBtn->SetBackgroundColor(FLinearColor(0.12f, 0.28f, 0.48f, 1.0f));
			OutBtnLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			OutBtnLbl->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.96f, 0.78f)));
			ApplyFontSize(OutBtnLbl, 12);
			OutBtn->SetContent(OutBtnLbl);
			BtnBox->SetContent(OutBtn);
			if (UHorizontalBoxSlot* BS = Row->AddChildToHorizontalBox(BtnBox))
			{
				BS->SetVerticalAlignment(VAlign_Center);
			}
		};

		AddStatUpgradeRow(TEXT("HPRow"), HPStatText, FLinearColor(0.42f, 1.0f, 0.52f), UpgradeHPButton, UpgradeHPBtnLabel);
		AddStatUpgradeRow(TEXT("KiRow"), KiStatText, FLinearColor(0.45f, 0.88f, 1.0f), UpgradeKiButton, UpgradeKiBtnLabel);
		AddStatUpgradeRow(TEXT("AtkRow"), AtkStatText, FLinearColor(1.0f, 0.78f, 0.32f), UpgradeAtkButton, UpgradeAtkBtnLabel);

		UHorizontalBox* FooterRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FooterRow"));
		if (UVerticalBoxSlot* FS = VBox->AddChildToVerticalBox(FooterRow))
		{
			FS->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));
		}

		USizeBox* FooterLeftBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		FooterLeftBox->SetMinDesiredWidth(430.0f);
		FeedbackStatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FeedbackStatusText"));
		FeedbackStatusText->SetText(FText::FromString(TEXT("Chi nang chi so bang click chuot trong Bang Ho So [Tab -> Ky Nang]!")));
		FeedbackStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.92f, 1.0f)));
		ApplyFontSize(FeedbackStatusText, 11);
		FooterLeftBox->SetContent(FeedbackStatusText);
		if (UHorizontalBoxSlot* FTextSlot = FooterRow->AddChildToHorizontalBox(FooterLeftBox))
		{
			FTextSlot->SetVerticalAlignment(VAlign_Center);
			FTextSlot->SetPadding(FMargin(0.0f, 0.0f, 16.0f, 0.0f));
		}

		ClosePanelButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ClosePotentialBtn"));
		ClosePanelButton->SetBackgroundColor(FLinearColor(0.28f, 0.14f, 0.16f, 1.0f));
		UTextBlock* CloseLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ClosePotentialLbl"));
		CloseLbl->SetText(FText::FromString(TEXT(" DONG (P) ")));
		CloseLbl->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.92f, 0.92f)));
		ApplyFontSize(CloseLbl, 11);
		ClosePanelButton->SetContent(CloseLbl);
		FooterRow->AddChildToHorizontalBox(ClosePanelButton);

		UpdateAllLabels();
	}

	return Super::RebuildWidget();
}

void UIU_PotentialUpgradeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UpgradeHPButton)
	{
		UpgradeHPButton->OnClicked.AddUniqueDynamic(this, &UIU_PotentialUpgradeWidget::HandleUpgradeHPClicked);
	}
	if (UpgradeKiButton)
	{
		UpgradeKiButton->OnClicked.AddUniqueDynamic(this, &UIU_PotentialUpgradeWidget::HandleUpgradeKiClicked);
	}
	if (UpgradeAtkButton)
	{
		UpgradeAtkButton->OnClicked.AddUniqueDynamic(this, &UIU_PotentialUpgradeWidget::HandleUpgradeAtkClicked);
	}
	if (ClosePanelButton)
	{
		ClosePanelButton->OnClicked.AddUniqueDynamic(this, &UIU_PotentialUpgradeWidget::HandleCloseClicked);
	}

	RefreshBindings();
}

void UIU_PotentialUpgradeWidget::NativeDestruct()
{
	UnbindAll();
	Super::NativeDestruct();
}

void UIU_PotentialUpgradeWidget::RefreshBindings()
{
	UnbindAll();

	APawn* Pawn = GetOwningPlayerPawn();
	if (Pawn)
	{
		if (UIU_AttributeComponent* Attr = Pawn->FindComponentByClass<UIU_AttributeComponent>())
		{
			BoundAttr = Attr;
			Attr->OnHPChanged.AddUniqueDynamic(this, &UIU_PotentialUpgradeWidget::HandleHPChanged);
			Attr->OnKiChanged.AddUniqueDynamic(this, &UIU_PotentialUpgradeWidget::HandleKiChanged);
			Attr->OnAttackPowerChanged.AddUniqueDynamic(this, &UIU_PotentialUpgradeWidget::HandleAttackPowerChanged);
		}
	}

	AIU_PlayerState* PS = Pawn ? Pawn->GetPlayerState<AIU_PlayerState>() : nullptr;
	if (!PS && GetOwningPlayer())
	{
		PS = GetOwningPlayer()->GetPlayerState<AIU_PlayerState>();
	}
	if (PS)
	{
		BoundPS = PS;
		PS->OnCharacterEXPChanged.AddUniqueDynamic(this, &UIU_PotentialUpgradeWidget::HandleCharacterEXPChanged);
		PS->OnCharacterPotentialChanged.AddUniqueDynamic(this, &UIU_PotentialUpgradeWidget::HandleCharacterPotentialChanged);
	}

	UpdateAllLabels();
}

void UIU_PotentialUpgradeWidget::UnbindAll()
{
	if (UIU_AttributeComponent* Attr = BoundAttr.Get())
	{
		Attr->OnHPChanged.RemoveDynamic(this, &UIU_PotentialUpgradeWidget::HandleHPChanged);
		Attr->OnKiChanged.RemoveDynamic(this, &UIU_PotentialUpgradeWidget::HandleKiChanged);
		Attr->OnAttackPowerChanged.RemoveDynamic(this, &UIU_PotentialUpgradeWidget::HandleAttackPowerChanged);
	}
	if (AIU_PlayerState* PS = BoundPS.Get())
	{
		PS->OnCharacterEXPChanged.RemoveDynamic(this, &UIU_PotentialUpgradeWidget::HandleCharacterEXPChanged);
		PS->OnCharacterPotentialChanged.RemoveDynamic(this, &UIU_PotentialUpgradeWidget::HandleCharacterPotentialChanged);
	}
	BoundAttr.Reset();
	BoundPS.Reset();
}

void UIU_PotentialUpgradeWidget::RequestStatUpgrade(EIU_PotentialStatType StatType)
{
	AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer());
	if (!IUPC)
	{
		return;
	}

	const AIU_PlayerState* PS = BoundPS.Get();
	if (PS)
	{
		const int64 Needed = (StatType == EIU_PotentialStatType::MaxHP) ? PS->GetNextHPCost()
			: (StatType == EIU_PotentialStatType::MaxKi) ? PS->GetNextKiCost()
			: PS->GetNextAtkCost();
		if (PS->GetPotentialPoints() < Needed)
		{
			if (FeedbackStatusText)
			{
				FeedbackStatusText->SetText(FText::FromString(
					FString::Printf(TEXT("Khong du Tiem Nang! Can %lld TN (Hien co: %lld TN)"), Needed, PS->GetPotentialPoints())));
				FeedbackStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.35f, 0.28f)));
			}
			return;
		}
	}

	if (FeedbackStatusText)
	{
		FeedbackStatusText->SetText(FText::FromString(TEXT("Dang gui yeu cau cong chi so len Dedicated Server...")));
		FeedbackStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.90f, 0.35f)));
	}
	IUPC->Server_RequestAllocatePotential(StatType);
}

void UIU_PotentialUpgradeWidget::HandleUpgradeHPClicked()
{
	RequestStatUpgrade(EIU_PotentialStatType::MaxHP);
}

void UIU_PotentialUpgradeWidget::HandleUpgradeKiClicked()
{
	RequestStatUpgrade(EIU_PotentialStatType::MaxKi);
}

void UIU_PotentialUpgradeWidget::HandleUpgradeAtkClicked()
{
	RequestStatUpgrade(EIU_PotentialStatType::AttackPower);
}

void UIU_PotentialUpgradeWidget::HandleCloseClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		IUPC->SetPotentialUpgradePanelVisible(false, true);
	}
	else
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UIU_PotentialUpgradeWidget::HandleHPChanged(float, float, float)
{
	UpdateAllLabels();
}

void UIU_PotentialUpgradeWidget::HandleKiChanged(float, float, float)
{
	UpdateAllLabels();
}

void UIU_PotentialUpgradeWidget::HandleAttackPowerChanged(float)
{
	UpdateAllLabels();
}

void UIU_PotentialUpgradeWidget::HandleCharacterEXPChanged(int64, int64, int32)
{
	UpdateAllLabels();
}

void UIU_PotentialUpgradeWidget::HandleCharacterPotentialChanged(int64, int64, int64, int64)
{
	if (FeedbackStatusText)
	{
		FeedbackStatusText->SetText(FText::FromString(TEXT("Cap nhat Tiem Nang & Chi So thanh cong!")));
		FeedbackStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.35f, 1.0f, 0.62f)));
	}
	UpdateAllLabels();
}

void UIU_PotentialUpgradeWidget::UpdateAllLabels()
{
	const UIU_AttributeComponent* Attr = BoundAttr.Get();
	const AIU_PlayerState* PS = BoundPS.Get();

	const int32 Lv = PS ? PS->GetCharacterLevel() : 1;
	const int64 CurEXP = PS ? PS->GetCurrentEXP() : 0;
	const int64 NextEXP = PS ? PS->GetNextLevelEXP() : 100;
	const int64 PoolTN = PS ? PS->GetPotentialPoints() : 0;
	const int32 HPUpg = PS ? PS->GetHPUpgrades() : 0;
	const int32 KiUpg = PS ? PS->GetKiUpgrades() : 0;
	const int32 AtkUpg = PS ? PS->GetAtkUpgrades() : 0;
	const int64 HPCost = PS ? PS->GetNextHPCost() : 100;
	const int64 KiCost = PS ? PS->GetNextKiCost() : 100;
	const int64 AtkCost = PS ? PS->GetNextAtkCost() : 150;

	const float CurHP = Attr ? Attr->GetHP() : 200.0f;
	const float MaxHP = Attr ? Attr->GetMaxHP() : 200.0f;
	const float CurKi = Attr ? Attr->GetKi() : 200.0f;
	const float MaxKi = Attr ? Attr->GetMaxKi() : 200.0f;
	const float Atk = Attr ? Attr->GetAttackPower() : 100.0f;
	const float Mul = Attr ? Attr->GetAttackPowerMultiplier() : 1.0f;

	if (LevelSummaryText)
	{
		LevelSummaryText->SetText(FText::FromString(
			FString::Printf(TEXT("Cap Do: Lv. %d  |  Diem Suc Manh: %lld / %lld  (Khong tang chi so)"), Lv, CurEXP, NextEXP)));
	}
	if (PotentialPoolText)
	{
		PotentialPoolText->SetText(FText::FromString(
			FString::Printf(TEXT("Diem Tiem Nang hien co: %lld TN"), PoolTN)));
	}
	if (HPStatText)
	{
		HPStatText->SetText(FText::FromString(
			FString::Printf(TEXT("HP: %.0f / %.0f  (Goc 200, da cong %d lan)"), CurHP, MaxHP, HPUpg)));
	}
	if (KiStatText)
	{
		KiStatText->SetText(FText::FromString(
			FString::Printf(TEXT("KI: %.0f / %.0f  (Goc 200, da cong %d lan)"), CurKi, MaxKi, KiUpg)));
	}
	if (AtkStatText)
	{
		AtkStatText->SetText(FText::FromString(
			FString::Printf(TEXT("Suc Danh: %.0f (x%.2f)  (Goc 100, da cong %d lan)"), Atk, Mul, AtkUpg)));
	}

	if (UpgradeHPBtnLabel)
	{
		UpgradeHPBtnLabel->SetText(FText::FromString(FString::Printf(TEXT(" +100 HP (%lld TN) "), HPCost)));
	}
	if (UpgradeKiBtnLabel)
	{
		UpgradeKiBtnLabel->SetText(FText::FromString(FString::Printf(TEXT(" +100 KI (%lld TN) "), KiCost)));
	}
	if (UpgradeAtkBtnLabel)
	{
		UpgradeAtkBtnLabel->SetText(FText::FromString(FString::Printf(TEXT(" +10 SUC DANH (%lld TN) "), AtkCost)));
	}
}
