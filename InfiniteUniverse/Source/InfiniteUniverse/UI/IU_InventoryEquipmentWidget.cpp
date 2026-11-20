// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "UI/IU_InventoryEquipmentWidget.h"
#include "Core/IU_PlayerController.h"
#include "Core/IU_PlayerState.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Attributes/IU_AttributeComponent.h"
#include "UI/IU_HUDManager.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/Pawn.h"

void UIU_InventoryActionProxy::HandleButtonClicked()
{
	if (UIU_InventoryEquipmentWidget* W = OwnerWidget.Get())
	{
		W->ExecuteRowAction(Action, ItemInstanceID, SlotID, BagSlotIndex, bTargetLockState);
	}
}

namespace
{
	struct FIU_PaperdollSlotSpec
	{
		const TCHAR* SlotID;
		const TCHAR* EmptyGlyph;
		const TCHAR* DisplayLabel;
		bool bLeftColumn;
	};

	// Left 4 slots (Armor) + Right 4 slots (Head / Accessories / Special) matching Image 1 Paperdoll!
	static const FIU_PaperdollSlotSpec GPaperdollSlots[8] = {
		{ TEXT("slot.body"),    TEXT("AO"),    TEXT("Giap Than"),    true  },
		{ TEXT("slot.legs"),    TEXT("QUAN"),  TEXT("Quan Giap"),    true  },
		{ TEXT("slot.hands"),   TEXT("GANG"),  TEXT("Gang Tay"),     true  },
		{ TEXT("slot.feet"),    TEXT("GIAY"),  TEXT("Giay Chien"),   true  },
		{ TEXT("slot.head"),    TEXT("MU"),    TEXT("Mu / Dau"),     false },
		{ TEXT("slot.acc_1"),   TEXT("NHAN"),  TEXT("Phu Kien 1"),   false },
		{ TEXT("slot.acc_2"),   TEXT("RADA"),  TEXT("Rada / PK2"),   false },
		{ TEXT("slot.special"), TEXT("LOI"),   TEXT("Vu Khi / Loi"), false }
	};

	static void StyleNROText(UTextBlock* TB, int32 SizePt, const FLinearColor& Color, bool bShadow = true)
	{
		if (!TB)
		{
			return;
		}
		FSlateFontInfo F = TB->GetFont();
		F.Size = SizePt;
		TB->SetFont(F);
		TB->SetColorAndOpacity(FSlateColor(Color));
		if (bShadow)
		{
			TB->SetShadowOffset(FVector2D(1.1f, 1.1f));
			TB->SetShadowColorAndOpacity(FLinearColor(0.05f, 0.03f, 0.01f, 0.92f));
		}
		else
		{
			TB->SetShadowOffset(FVector2D::ZeroVector);
		}
	}

	static FLinearColor GetRarityTintColor(const FString& Rarity)
	{
		if (Rarity.Contains(TEXT("MYTHIC"), ESearchCase::IgnoreCase)) { return FLinearColor(1.0f, 0.30f, 0.25f, 1.0f); }
		if (Rarity.Contains(TEXT("LEGENDARY"), ESearchCase::IgnoreCase)) { return FLinearColor(1.0f, 0.78f, 0.16f, 1.0f); }
		if (Rarity.Contains(TEXT("EPIC"), ESearchCase::IgnoreCase)) { return FLinearColor(0.76f, 0.36f, 1.0f, 1.0f); }
		if (Rarity.Contains(TEXT("RARE"), ESearchCase::IgnoreCase)) { return FLinearColor(0.22f, 0.72f, 1.0f, 1.0f); }
		if (Rarity.Contains(TEXT("UNCOMMON"), ESearchCase::IgnoreCase)) { return FLinearColor(0.24f, 0.86f, 0.42f, 1.0f); }
		return FLinearColor(0.95f, 0.92f, 0.85f, 1.0f);
	}

	static FString GetShortItemIconGlyph(const FIU_InventoryItemView& Item)
	{
		if (Item.ItemDefID.Contains(TEXT("elixir"))) { return TEXT("DAU"); }
		if (Item.ItemDefID.Contains(TEXT("ore"))) { return TEXT("DA"); }
		if (Item.ItemDefID.Contains(TEXT("token"))) { return TEXT("HUY"); }
		if (Item.EquipSlot == TEXT("slot.body")) { return TEXT("AO"); }
		if (Item.EquipSlot == TEXT("slot.legs")) { return TEXT("QUAN"); }
		if (Item.EquipSlot == TEXT("slot.hands")) { return TEXT("GANG"); }
		if (Item.EquipSlot == TEXT("slot.feet")) { return TEXT("GIAY"); }
		if (Item.EquipSlot == TEXT("slot.head")) { return TEXT("MU"); }
		if (Item.EquipSlot == TEXT("slot.acc_1")) { return TEXT("NHAN"); }
		if (Item.EquipSlot == TEXT("slot.acc_2")) { return TEXT("RADA"); }
		if (Item.EquipSlot == TEXT("slot.special")) { return TEXT("LOI"); }
		return Item.DisplayName.Left(4).ToUpper();
	}

	static FLinearColor GetShortItemBgColor(const FIU_InventoryItemView& Item)
	{
		if (Item.ItemDefID.Contains(TEXT("elixir"))) { return FLinearColor(0.18f, 0.62f, 0.22f, 0.95f); }
		if (Item.ItemDefID.Contains(TEXT("ore"))) { return FLinearColor(0.68f, 0.18f, 0.16f, 0.95f); }
		if (Item.Category == TEXT("EQUIPMENT"))
		{
			if (Item.Rarity.Contains(TEXT("RARE"), ESearchCase::IgnoreCase)) { return FLinearColor(0.14f, 0.44f, 0.72f, 0.95f); }
			if (Item.Rarity.Contains(TEXT("UNCOMMON"), ESearchCase::IgnoreCase)) { return FLinearColor(0.16f, 0.52f, 0.28f, 0.95f); }
			return FLinearColor(0.62f, 0.34f, 0.12f, 0.95f);
		}
		return FLinearColor(0.45f, 0.36f, 0.24f, 0.95f);
	}
}

TSharedRef<SWidget> UIU_InventoryEquipmentWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// Classic NRO Warm Parchment / Bronze / Emerald Palette matching Image 1!
		CardOuterBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.84f, 0.75f, 0.61f, 0.98f), // Warm NRO tan parchment (#D6BFA0)
			8.0f,
			FLinearColor(0.32f, 0.20f, 0.08f, 1.0f),
			2.5f);
		HeaderBannerBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.54f, 0.41f, 0.24f, 1.0f), // Rich NRO olive-brown header (#8A693D)
			6.0f,
			FLinearColor(0.34f, 0.23f, 0.10f, 1.0f),
			1.5f);
		CloseRedBtnBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.88f, 0.22f, 0.10f, 1.0f), // Glossy NRO red [X] button
			5.0f,
			FLinearColor(0.35f, 0.06f, 0.02f, 1.0f),
			1.5f);
		ActiveTabGreenBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.56f, 0.88f, 0.56f, 1.0f), // Active Jade-Green tab (exact Image 1 "Hanh Trang" tab!)
			6.0f,
			FLinearColor(0.12f, 0.52f, 0.18f, 1.0f),
			2.0f);
		InactiveTabCreamBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.96f, 0.92f, 0.84f, 1.0f), // Cream inactive tabs with orange-brown border
			6.0f,
			FLinearColor(0.78f, 0.36f, 0.12f, 1.0f),
			1.8f);
		EquipOccupiedSlotBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.88f, 0.32f, 0.08f, 1.0f), // Fiery orange-red NRO equipped frame
			5.0f,
			FLinearColor(1.0f, 0.82f, 0.25f, 1.0f),
			1.8f);
		EquipEmptySlotBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.68f, 0.58f, 0.45f, 0.95f),
			5.0f,
			FLinearColor(0.92f, 0.86f, 0.78f, 0.95f),
			1.5f);
		BagCellSelectedBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.72f, 0.62f, 0.48f, 1.0f),
			4.0f,
			FLinearColor(0.16f, 0.92f, 0.38f, 1.0f),
			2.2f);
		BagCellNormalBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.73f, 0.64f, 0.52f, 0.96f), // Exact Image 1 warm taupe square tile (#BBA385)
			4.0f,
			FLinearColor(0.95f, 0.91f, 0.85f, 0.95f),
			1.2f);

		UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ProfileRootCanvas"));
		WidgetTree->RootWidget = RootCanvas;

		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ProfileMainCard"));
		Card->SetBrush(CardOuterBrush);
		Card->SetPadding(FMargin(8.0f, 7.0f));
		if (UCanvasPanelSlot* CardSlot = RootCanvas->AddChildToCanvas(Card))
		{
			CardSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CardSlot->SetPosition(FVector2D(0.0f, 0.0f));
			CardSlot->SetAutoSize(true);
		}

		USizeBox* CardWidthBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardWidthBox"));
		CardWidthBox->SetWidthOverride(436.0f);
		Card->SetContent(CardWidthBox);

		UVerticalBox* MainVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MainVBox"));
		CardWidthBox->SetContent(MainVBox);

		// ====================================================================
		// 1. TOP HEADER BANNER: Avatar + HP/KI/SucDanh/Giap&Crit + Red [X] Button (Image 1!)
		// ====================================================================
		UBorder* HeaderBanner = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HeaderBanner"));
		HeaderBanner->SetBrush(HeaderBannerBrush);
		HeaderBanner->SetPadding(FMargin(8.0f, 5.0f));
		if (UVerticalBoxSlot* HS = MainVBox->AddChildToVerticalBox(HeaderBanner))
		{
			HS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
		}

		UHorizontalBox* HeaderHBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HeaderHBox"));
		HeaderBanner->SetContent(HeaderHBox);

		// Left Avatar Box
		USizeBox* AvatarSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("AvatarSize"));
		AvatarSize->SetWidthOverride(52.0f);
		AvatarSize->SetHeightOverride(52.0f);
		UBorder* AvatarFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("AvatarFrame"));
		AvatarFrame->SetBrushColor(FLinearColor(0.16f, 0.24f, 0.32f, 0.98f));
		AvatarFrame->SetPadding(FMargin(3.0f));
		AvatarBadgeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AvatarBadgeText"));
		AvatarBadgeText->SetText(FText::FromString(TEXT("HERO\nLV.1")));
		AvatarBadgeText->SetJustification(ETextJustify::Center);
		StyleNROText(AvatarBadgeText, 9, FLinearColor(0.95f, 0.98f, 1.0f));
		AvatarFrame->SetContent(AvatarBadgeText);
		AvatarSize->SetContent(AvatarFrame);
		if (UHorizontalBoxSlot* AvS = HeaderHBox->AddChildToHorizontalBox(AvatarSize))
		{
			AvS->SetVerticalAlignment(VAlign_Center);
			AvS->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
		}

		// Middle 4-Line Stats Readout (Exact Image 1 yellow lines!)
		USizeBox* VitalsColWidth = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("VitalsColWidth"));
		VitalsColWidth->SetWidthOverride(312.0f);
		UVerticalBox* VitalsCol = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("HeaderVitalsCol"));
		VitalsColWidth->SetContent(VitalsCol);

		HeaderHPText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HeaderHPText"));
		HeaderKiText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HeaderKiText"));
		HeaderAtkText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HeaderAtkText"));
		HeaderDefCritText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HeaderDefCritText"));

		const FLinearColor BrightGold(1.0f, 0.92f, 0.22f, 1.0f);
		StyleNROText(HeaderHPText, 10, BrightGold);
		StyleNROText(HeaderKiText, 10, BrightGold);
		StyleNROText(HeaderAtkText, 10, BrightGold);
		StyleNROText(HeaderDefCritText, 10, BrightGold);

		VitalsCol->AddChildToVerticalBox(HeaderHPText);
		VitalsCol->AddChildToVerticalBox(HeaderKiText);
		VitalsCol->AddChildToVerticalBox(HeaderAtkText);
		VitalsCol->AddChildToVerticalBox(HeaderDefCritText);

		if (UHorizontalBoxSlot* VCS = HeaderHBox->AddChildToHorizontalBox(VitalsColWidth))
		{
			VCS->SetVerticalAlignment(VAlign_Center);
		}

		// Top-Right Red [X] Close Button
		USizeBox* CloseBtnSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CloseBtnSize"));
		CloseBtnSize->SetWidthOverride(32.0f);
		CloseBtnSize->SetHeightOverride(30.0f);
		UBorder* RedXBg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RedXBg"));
		RedXBg->SetBrush(CloseRedBtnBrush);
		RedXBg->SetPadding(FMargin(0.0f));
		CloseRedXButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseRedXButton"));
		CloseRedXButton->SetBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
		UTextBlock* XLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CloseXLbl"));
		XLbl->SetText(FText::FromString(TEXT("X")));
		XLbl->SetJustification(ETextJustify::Center);
		StyleNROText(XLbl, 12, FLinearColor::White);
		CloseRedXButton->SetContent(XLbl);
		RedXBg->SetContent(CloseRedXButton);
		CloseBtnSize->SetContent(RedXBg);
		if (UHorizontalBoxSlot* XS = HeaderHBox->AddChildToHorizontalBox(CloseBtnSize))
		{
			XS->SetVerticalAlignment(VAlign_Top);
		}

		// ====================================================================
		// 2. FIVE NRO TOP MENU TABS: Nhiem Vu | Hanh Trang | Ky Nang | Bang Hoi | Chuc Nang
		// ====================================================================
		UHorizontalBox* TabsRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ProfileTabsRow"));
		if (UVerticalBoxSlot* TRS = MainVBox->AddChildToVerticalBox(TabsRow))
		{
			TRS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
		}

		static const TCHAR* GTabTitles[5] = {
			TEXT("Nhiem\nVu"),
			TEXT("Hanh\nTrang"),
			TEXT("Ky\nNang"),
			TEXT("Bang\nHoi"),
			TEXT("Chuc\nNang")
		};

		TabOuterBorders.SetNum(5);
		TabButtons.SetNum(5);
		for (int32 t = 0; t < 5; ++t)
		{
			USizeBox* TabSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			TabSize->SetWidthOverride(81.0f);
			TabSize->SetHeightOverride(36.0f);

			UBorder* TabBdr = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			TabBdr->SetBrush((t == static_cast<int32>(CurrentProfileTab)) ? ActiveTabGreenBrush : InactiveTabCreamBrush);
			TabBdr->SetPadding(FMargin(1.0f));
			TabOuterBorders[t] = TabBdr;
			TabSize->SetContent(TabBdr);

			UButton* TBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
			TBtn->SetBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
			UTextBlock* TLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			TLbl->SetText(FText::FromString(GTabTitles[t]));
			TLbl->SetJustification(ETextJustify::Center);
			StyleNROText(TLbl, 9, FLinearColor(0.16f, 0.10f, 0.04f, 1.0f), /*bShadow=*/false);
			TBtn->SetContent(TLbl);
			TabBdr->SetContent(TBtn);
			TabButtons[t] = TBtn;

			if (UHorizontalBoxSlot* THS = TabsRow->AddChildToHorizontalBox(TabSize))
			{
				THS->SetPadding(FMargin(0.0f, 0.0f, (t < 4) ? 3.5f : 0.0f, 0.0f));
			}
		}

		// ====================================================================
		// 3. CONTENT SWITCHER FOR THE 5 MENU TABS
		// ====================================================================
		TabContentSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("TabContentSwitcher"));
		if (UVerticalBoxSlot* SWS = MainVBox->AddChildToVerticalBox(TabContentSwitcher))
		{
			SWS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
		}

		// ---- PANE 0: NHIEM VU (STEP 6 POSTGRESQL character_quests + TURN-IN!) ----
		{
			UBorder* QuestPane = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("QuestPane"));
			QuestPane->SetBrushColor(FLinearColor(0.46f, 0.35f, 0.21f, 0.96f));
			QuestPane->SetPadding(FMargin(8.0f, 7.0f));
			USizeBox* QHeight = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			QHeight->SetHeightOverride(318.0f);
			QuestPane->SetContent(QHeight);

			UVerticalBox* QVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("QuestPaneVBox"));
			QHeight->SetContent(QVBox);

			UHorizontalBox* QHeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			if (UVerticalBoxSlot* S = QVBox->AddChildToVerticalBox(QHeaderRow))
			{
				S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
			}
			USizeBox* QTitleW = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			QTitleW->SetWidthOverride(322.0f);
			UTextBlock* QTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			QTitle->SetText(FText::FromString(TEXT("NHIEM VU HANH TINH & SAN BOSS (POSTGRESQL 18):")));
			StyleNROText(QTitle, 9, BrightGold);
			QTitleW->SetContent(QTitle);
			if (UHorizontalBoxSlot* S = QHeaderRow->AddChildToHorizontalBox(QTitleW))
			{
				S->SetVerticalAlignment(VAlign_Center);
			}

			QuestRefreshButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("QuestRefreshButton"));
			QuestRefreshButton->SetBackgroundColor(FLinearColor(0.28f, 0.44f, 0.58f, 1.0f));
			UTextBlock* QRefLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			QRefLbl->SetText(FText::FromString(TEXT(" TAI LAI ")));
			StyleNROText(QRefLbl, 8, FLinearColor::White);
			QuestRefreshButton->SetContent(QRefLbl);
			QHeaderRow->AddChildToHorizontalBox(QuestRefreshButton);

			USizeBox* QScrollH = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			QScrollH->SetHeightOverride(278.0f);
			UScrollBox* QScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("QuestScrollBox"));
			QuestListVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("QuestListVBox"));
			QuestChecklistText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("QuestChecklistText"));
			StyleNROText(QuestChecklistText, 9, FLinearColor(0.98f, 0.95f, 0.85f));
			QuestListVBox->AddChildToVerticalBox(QuestChecklistText);
			QScroll->AddChild(QuestListVBox);
			QScrollH->SetContent(QScroll);
			QVBox->AddChildToVerticalBox(QScrollH);

			TabContentSwitcher->AddChild(QuestPane);
		}

		// ---- PANE 1: HANH TRANG (EXACT IMAGE 1 PAPERDOLL + INSPECTOR + 48-CELL GRID!) ----
		{
			UVerticalBox* InvPaneVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("InvPaneVBox"));
			TabContentSwitcher->AddChild(InvPaneVBox);

			// 1A. Upper 3-Column Paperdoll Stage
			UBorder* PaperdollBg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PaperdollBg"));
			PaperdollBg->SetBrushColor(FLinearColor(0.76f, 0.66f, 0.52f, 0.95f));
			PaperdollBg->SetPadding(FMargin(4.0f, 4.0f));
			if (UVerticalBoxSlot* PDS = InvPaneVBox->AddChildToVerticalBox(PaperdollBg))
			{
				PDS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
			}

			UHorizontalBox* PaperdollRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PaperdollRow"));
			PaperdollBg->SetContent(PaperdollRow);

			LeftEquipColumnBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LeftEquipColumnBox"));
			if (UHorizontalBoxSlot* LCS = PaperdollRow->AddChildToHorizontalBox(LeftEquipColumnBox))
			{
				LCS->SetVerticalAlignment(VAlign_Top);
				LCS->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
			}

			// Center Character Stage + Olive Sub-Stats (matching Image 1!)
			USizeBox* CenterStageWidth = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CenterStageWidth"));
			CenterStageWidth->SetWidthOverride(316.0f);
			UVerticalBox* CenterStageVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CenterStageVBox"));
			CenterStageWidth->SetContent(CenterStageVBox);

			// Dark-Teal Character Showcase Stage
			UBorder* PortraitStage = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PortraitStage"));
			PortraitStage->SetBrushColor(FLinearColor(0.20f, 0.33f, 0.38f, 0.98f));
			PortraitStage->SetPadding(FMargin(6.0f, 5.0f));
			UVerticalBox* StageInner = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
			PortraitStage->SetContent(StageInner);

			UBorder* CyanNameplate = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CyanNameplate"));
			CyanNameplate->SetBrushColor(FLinearColor(0.08f, 0.14f, 0.18f, 0.92f));
			CyanNameplate->SetPadding(FMargin(6.0f, 2.0f));
			PaperdollNameplateText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PaperdollNameplateText"));
			PaperdollNameplateText->SetJustification(ETextJustify::Center);
			StyleNROText(PaperdollNameplateText, 10, FLinearColor(0.45f, 0.96f, 1.0f));
			CyanNameplate->SetContent(PaperdollNameplateText);
			if (UVerticalBoxSlot* NPS = StageInner->AddChildToVerticalBox(CyanNameplate))
			{
				NPS->SetHorizontalAlignment(HAlign_Center);
				NPS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
			}

			PaperdollAvatarGlyphText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PaperdollAvatarGlyphText"));
			PaperdollAvatarGlyphText->SetJustification(ETextJustify::Center);
			StyleNROText(PaperdollAvatarGlyphText, 10, FLinearColor(1.0f, 0.92f, 0.62f));
			if (UVerticalBoxSlot* AGS = StageInner->AddChildToVerticalBox(PaperdollAvatarGlyphText))
			{
				AGS->SetHorizontalAlignment(HAlign_Center);
				AGS->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 4.0f));
			}

			CenterStageVBox->AddChildToVerticalBox(PortraitStage);

			// Olive-Gold Bonus Stats Box right under Character Stage (exact Image 1!)
			UBorder* SubStatsBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SubStatsBorder"));
			SubStatsBorder->SetBrushColor(FLinearColor(0.54f, 0.42f, 0.23f, 0.98f));
			SubStatsBorder->SetPadding(FMargin(6.0f, 4.0f));
			PaperdollSubStatsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PaperdollSubStatsText"));
			StyleNROText(PaperdollSubStatsText, 9, BrightGold);
			SubStatsBorder->SetContent(PaperdollSubStatsText);
			CenterStageVBox->AddChildToVerticalBox(SubStatsBorder);

			if (UHorizontalBoxSlot* CCS = PaperdollRow->AddChildToHorizontalBox(CenterStageWidth))
			{
				CCS->SetVerticalAlignment(VAlign_Top);
			}

			RightEquipColumnBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RightEquipColumnBox"));
			if (UHorizontalBoxSlot* RCS = PaperdollRow->AddChildToHorizontalBox(RightEquipColumnBox))
			{
				RCS->SetVerticalAlignment(VAlign_Top);
				RCS->SetPadding(FMargin(6.0f, 0.0f, 0.0f, 0.0f));
			}

			// 1B. Selected Item Inspector & One-Click Action Bar
			UBorder* InspectorBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InspectorBorder"));
			InspectorBorder->SetBrushColor(FLinearColor(0.36f, 0.26f, 0.15f, 0.98f));
			InspectorBorder->SetPadding(FMargin(6.0f, 4.0f));
			if (UVerticalBoxSlot* INS = InvPaneVBox->AddChildToVerticalBox(InspectorBorder))
			{
				INS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
			}

			UVerticalBox* InspectorVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
			InspectorBorder->SetContent(InspectorVBox);

			SelectedItemTitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SelectedItemTitleText"));
			StyleNROText(SelectedItemTitleText, 9, FLinearColor(1.0f, 0.95f, 0.78f));
			InspectorVBox->AddChildToVerticalBox(SelectedItemTitleText);

			SelectedItemActionsBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SelectedItemActionsBox"));
			if (UVerticalBoxSlot* ActS = InspectorVBox->AddChildToVerticalBox(SelectedItemActionsBox))
			{
				ActS->SetPadding(FMargin(0.0f, 3.0f, 0.0f, 0.0f));
			}

			// 1C. 48-Slot Square NRO Bag Grid (6 cols x 8 rows inside ScrollBox — exact Image 1!)
			USizeBox* GridScrollCap = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("GridScrollCap"));
			GridScrollCap->SetHeightOverride(112.0f);
			UScrollBox* BagScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("BagGridScrollBox"));
			GridScrollCap->SetContent(BagScroll);

			BagUniformGrid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), TEXT("BagUniformGrid"));
			BagUniformGrid->SetSlotPadding(FMargin(1.5f));
			BagScroll->AddChild(BagUniformGrid);
			InvPaneVBox->AddChildToVerticalBox(GridScrollCap);
		}

		// ---- PANE 2: KY NANG & NANG CHI SO TIEM NANG (ONLY PLACE TO ALLOCATE POTENTIAL!) ----
		{
			UBorder* SkillPotPane = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SkillPotPane"));
			SkillPotPane->SetBrushColor(FLinearColor(0.45f, 0.34f, 0.20f, 0.98f));
			SkillPotPane->SetPadding(FMargin(10.0f, 8.0f));

			USizeBox* SPHeight = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			SPHeight->SetHeightOverride(318.0f);
			SkillPotPane->SetContent(SPHeight);

			UVerticalBox* SPVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
			SPHeight->SetContent(SPVBox);

			UTextBlock* PotHeader = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			PotHeader->SetText(FText::FromString(TEXT("CONG DIEM TIEM NANG NANG CHI SO GOC (CHUAN NRO):")));
			StyleNROText(PotHeader, 11, BrightGold);
			if (UVerticalBoxSlot* PHS = SPVBox->AddChildToVerticalBox(PotHeader))
			{
				PHS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 3.0f));
			}

			PotentialPoolBannerText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PotentialPoolBannerText"));
			StyleNROText(PotentialPoolBannerText, 10, FLinearColor(0.42f, 1.0f, 0.65f));
			if (UVerticalBoxSlot* PPS = SPVBox->AddChildToVerticalBox(PotentialPoolBannerText))
			{
				PPS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
			}

			auto AddPotentialRow = [&](const TCHAR* Name, TObjectPtr<UTextBlock>& OutRowTxt, const FLinearColor& TxtCol, TObjectPtr<UButton>& OutBtn, TObjectPtr<UTextBlock>& OutBtnLbl)
			{
				UBorder* RowCard = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
				RowCard->SetBrushColor(FLinearColor(0.32f, 0.23f, 0.13f, 0.96f));
				RowCard->SetPadding(FMargin(8.0f, 5.0f));
				if (UVerticalBoxSlot* RCS = SPVBox->AddChildToVerticalBox(RowCard))
				{
					RCS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
				}

				UHorizontalBox* HRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
				RowCard->SetContent(HRow);

				USizeBox* LeftW = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				LeftW->SetWidthOverride(238.0f);
				OutRowTxt = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
				StyleNROText(OutRowTxt, 10, TxtCol);
				LeftW->SetContent(OutRowTxt);
				if (UHorizontalBoxSlot* LS = HRow->AddChildToHorizontalBox(LeftW))
				{
					LS->SetVerticalAlignment(VAlign_Center);
					LS->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
				}

				USizeBox* BtnW = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				BtnW->SetWidthOverride(150.0f);
				OutBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
				OutBtn->SetBackgroundColor(FLinearColor(0.18f, 0.56f, 0.24f, 1.0f));
				OutBtnLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
				StyleNROText(OutBtnLbl, 9, FLinearColor(1.0f, 0.98f, 0.85f));
				OutBtn->SetContent(OutBtnLbl);
				BtnW->SetContent(OutBtn);
				if (UHorizontalBoxSlot* BS = HRow->AddChildToHorizontalBox(BtnW))
				{
					BS->SetVerticalAlignment(VAlign_Center);
				}
			};

			AddPotentialRow(TEXT("PotHPRow"), PotHPRowText, FLinearColor(0.45f, 1.0f, 0.55f), UpgradeHPButton, UpgradeHPBtnLabel);
			AddPotentialRow(TEXT("PotKiRow"), PotKiRowText, FLinearColor(0.48f, 0.90f, 1.0f), UpgradeKiButton, UpgradeKiBtnLabel);
			AddPotentialRow(TEXT("PotAtkRow"), PotAtkRowText, FLinearColor(1.0f, 0.82f, 0.35f), UpgradeAtkButton, UpgradeAtkBtnLabel);

			UTextBlock* SkillListTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			SkillListTitle->SetText(FText::FromString(TEXT("HUB O SKILL NGOAI MAN HINH (PHIM 1 -> 6):")));
			StyleNROText(SkillListTitle, 10, BrightGold);
			if (UVerticalBoxSlot* STS = SPVBox->AddChildToVerticalBox(SkillListTitle))
			{
				STS->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 3.0f));
			}

			UTextBlock* SkillListBody = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			SkillListBody->SetText(FText::FromString(
				TEXT("[1] Dam Lien Hoan (0 Ki)   |  [2] Chuong Ki (15 Ki)   |  [3] Tan Anh Bo (Ne)\n")
				TEXT("[4] Doi Than Phap          |  [5] Rada Khoa Muc Tieu  |  [6] Bay Vu Tru 3D\n")
				TEXT("* Hub Dau Than goc phai duoi: Click chuot hoac bam phim [G] (+150 HP/Ki)")));
			StyleNROText(SkillListBody, 9, FLinearColor(0.94f, 0.92f, 0.86f));
			SPVBox->AddChildToVerticalBox(SkillListBody);

			TabContentSwitcher->AddChild(SkillPotPane);
		}

		// ---- PANE 3: BANG HOI (STEP 5 COMPLETE NRO GUILD MANAGEMENT!) ----
		{
			UBorder* GuildPane = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("GuildPane"));
			GuildPane->SetBrushColor(FLinearColor(0.46f, 0.35f, 0.21f, 0.98f));
			GuildPane->SetPadding(FMargin(8.0f, 7.0f));
			USizeBox* GHeight = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			GHeight->SetHeightOverride(318.0f);
			GuildPane->SetContent(GHeight);

			GuildModeSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("GuildModeSwitcher"));
			GHeight->SetContent(GuildModeSwitcher);

			auto MakeGuildBtn = [&](const TCHAR* Lbl, const FLinearColor& BgCol) -> UButton*
			{
				UButton* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
				B->SetBackgroundColor(BgCol);
				UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
				T->SetText(FText::FromString(Lbl));
				StyleNROText(T, 8, FLinearColor(1.0f, 0.98f, 0.88f));
				B->SetContent(T);
				return B;
			};

			// Sub-Pane 0: Not In Guild (Create Guild + Browse Open Guilds)
			{
				UVerticalBox* NoGuildV = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NoGuildVBox"));
				GuildModeSwitcher->AddChild(NoGuildV);

				UTextBlock* CreateTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
				CreateTitle->SetText(FText::FromString(TEXT("THANH LAP BANG HOI MOI (PHI: 200 VANG | YEU CAU LV.2+):")));
				StyleNROText(CreateTitle, 9, BrightGold);
				if (UVerticalBoxSlot* S = NoGuildV->AddChildToVerticalBox(CreateTitle))
				{
					S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
				}

				UHorizontalBox* InputRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
				if (UVerticalBoxSlot* S = NoGuildV->AddChildToVerticalBox(InputRow))
				{
					S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
				}

				USizeBox* NameW = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				NameW->SetWidthOverride(252.0f);
				GuildCreateNameInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("GuildCreateNameInput"));
				GuildCreateNameInput->SetText(FText::FromString(TEXT("Zenith Vanguards")));
				GuildCreateNameInput->SetHintText(FText::FromString(TEXT("Ten Bang (3-24 ky tu)")));
				NameW->SetContent(GuildCreateNameInput);
				if (UHorizontalBoxSlot* S = InputRow->AddChildToHorizontalBox(NameW))
				{
					S->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
				}

				USizeBox* TagW = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				TagW->SetWidthOverride(132.0f);
				GuildCreateTagInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("GuildCreateTagInput"));
				GuildCreateTagInput->SetText(FText::FromString(TEXT("ZNT")));
				GuildCreateTagInput->SetHintText(FText::FromString(TEXT("TAG (2-5)")));
				TagW->SetContent(GuildCreateTagInput);
				InputRow->AddChildToHorizontalBox(TagW);

				UHorizontalBox* CreateBtnRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
				if (UVerticalBoxSlot* S = NoGuildV->AddChildToVerticalBox(CreateBtnRow))
				{
					S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
				}
				GuildCreateButton = MakeGuildBtn(TEXT(" LAP BANG HOI (-200 VANG) "), FLinearColor(0.16f, 0.58f, 0.24f, 1.0f));
				GuildRefreshDirButton = MakeGuildBtn(TEXT(" TAI LAI DS BANG "), FLinearColor(0.32f, 0.42f, 0.56f, 1.0f));
				if (UHorizontalBoxSlot* S = CreateBtnRow->AddChildToHorizontalBox(GuildCreateButton))
				{
					S->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
				}
				CreateBtnRow->AddChildToHorizontalBox(GuildRefreshDirButton);

				UTextBlock* DirTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
				DirTitle->SetText(FText::FromString(TEXT("DANH SACH BANG HOI DANG MO TUYEN THANH VIEN:")));
				StyleNROText(DirTitle, 9, BrightGold);
				if (UVerticalBoxSlot* S = NoGuildV->AddChildToVerticalBox(DirTitle))
				{
					S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
				}

				USizeBox* DirScrollH = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				DirScrollH->SetHeightOverride(182.0f);
				UScrollBox* DirScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
				GuildDirectoryListBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GuildDirectoryListBox"));
				DirScroll->AddChild(GuildDirectoryListBox);
				DirScrollH->SetContent(DirScroll);
				NoGuildV->AddChildToVerticalBox(DirScrollH);
			}

			// Sub-Pane 1: In Guild (Header + MOTD + Donate Vang + Roster)
			{
				UVerticalBox* InGuildV = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("InGuildVBox"));
				GuildModeSwitcher->AddChild(InGuildV);

				UBorder* SummaryCard = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
				SummaryCard->SetBrushColor(FLinearColor(0.30f, 0.21f, 0.12f, 0.98f));
				SummaryCard->SetPadding(FMargin(7.0f, 5.0f));
				GuildHeaderSummaryText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GuildHeaderSummaryText"));
				StyleNROText(GuildHeaderSummaryText, 9, BrightGold);
				SummaryCard->SetContent(GuildHeaderSummaryText);
				if (UVerticalBoxSlot* S = InGuildV->AddChildToVerticalBox(SummaryCard))
				{
					S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
				}

				UHorizontalBox* MOTDRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
				if (UVerticalBoxSlot* S = InGuildV->AddChildToVerticalBox(MOTDRow))
				{
					S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
				}
				USizeBox* MOTDW = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				MOTDW->SetWidthOverride(305.0f);
				GuildMOTDInputBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("GuildMOTDInputBox"));
				GuildMOTDInputBox->SetHintText(FText::FromString(TEXT("Thong bao Bang hoi...")));
				MOTDW->SetContent(GuildMOTDInputBox);
				if (UHorizontalBoxSlot* S = MOTDRow->AddChildToHorizontalBox(MOTDW))
				{
					S->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
				}
				GuildSaveMOTDButton = MakeGuildBtn(TEXT(" LUU TB "), FLinearColor(0.28f, 0.48f, 0.62f, 1.0f));
				MOTDRow->AddChildToHorizontalBox(GuildSaveMOTDButton);

				UHorizontalBox* DonateRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
				if (UVerticalBoxSlot* S = InGuildV->AddChildToVerticalBox(DonateRow))
				{
					S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
				}
				GuildDonate50Button = MakeGuildBtn(TEXT(" GOP 50 VANG "), FLinearColor(0.78f, 0.50f, 0.08f, 1.0f));
				GuildDonate150Button = MakeGuildBtn(TEXT(" GOP 150 VANG "), FLinearColor(0.86f, 0.56f, 0.08f, 1.0f));
				GuildRefreshRosterButton = MakeGuildBtn(TEXT(" LAM MOI "), FLinearColor(0.26f, 0.42f, 0.56f, 1.0f));
				GuildLeaveButton = MakeGuildBtn(TEXT(" ROI BANG "), FLinearColor(0.66f, 0.18f, 0.12f, 1.0f));

				for (UButton* B : { GuildDonate50Button.Get(), GuildDonate150Button.Get(), GuildRefreshRosterButton.Get(), GuildLeaveButton.Get() })
				{
					if (B)
					{
						if (UHorizontalBoxSlot* S = DonateRow->AddChildToHorizontalBox(B))
						{
							S->SetPadding(FMargin(0.0f, 0.0f, 5.0f, 0.0f));
						}
					}
				}

				USizeBox* MemScrollH = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				MemScrollH->SetHeightOverride(158.0f);
				UScrollBox* MemScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
				GuildMembersListBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GuildMembersListBox"));
				MemScroll->AddChild(GuildMembersListBox);
				MemScrollH->SetContent(MemScroll);
				InGuildV->AddChildToVerticalBox(MemScrollH);
			}

			TabContentSwitcher->AddChild(GuildPane);
		}

		// ---- PANE 4: CHUC NANG (STEP 6 DOI KHU / VUNG + SHOP BAO HIEM BOSS + CAI DAT!) ----
		{
			UBorder* FuncPane = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FuncPane"));
			FuncPane->SetBrushColor(FLinearColor(0.46f, 0.35f, 0.21f, 0.96f));
			FuncPane->SetPadding(FMargin(8.0f, 7.0f));
			USizeBox* FHeight = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			FHeight->SetHeightOverride(318.0f);
			UVerticalBox* FuncOuterVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FuncOuterVBox"));
			FHeight->SetContent(FuncOuterVBox);
			FuncPane->SetContent(FHeight);

			// Top Sub-Switcher Row: [DOI KHU / VUNG] | [SHOP BAO HIEM BOSS] | [CAI DAT]
			UHorizontalBox* SubTabsRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FuncSubTabsRow"));
			if (UVerticalBoxSlot* STS = FuncOuterVBox->AddChildToVerticalBox(SubTabsRow))
			{
				STS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
			}

			auto MakeSubTabBtn = [&](const TCHAR* Name, const TCHAR* Label, const FLinearColor& Bg, TObjectPtr<UButton>& OutBtn)
			{
				OutBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
				OutBtn->SetBackgroundColor(Bg);
				UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
				T->SetText(FText::FromString(Label));
				StyleNROText(T, 8, FLinearColor(1.0f, 0.98f, 0.88f));
				OutBtn->SetContent(T);
				if (UHorizontalBoxSlot* S = SubTabsRow->AddChildToHorizontalBox(OutBtn))
				{
					S->SetPadding(FMargin(0.0f, 0.0f, 5.0f, 0.0f));
				}
			};

			MakeSubTabBtn(TEXT("FuncTabChannelsBtn"), TEXT(" DOI KHU / VUNG "), FLinearColor(0.16f, 0.56f, 0.34f, 1.0f), FuncTabChannelsBtn);
			MakeSubTabBtn(TEXT("FuncTabPityShopBtn"), TEXT(" SHOP BAO HIEM BOSS "), FLinearColor(0.76f, 0.44f, 0.10f, 1.0f), FuncTabPityShopBtn);
			MakeSubTabBtn(TEXT("FuncTabSettingsBtn"), TEXT(" CAI DAT HE THONG "), FLinearColor(0.32f, 0.24f, 0.16f, 1.0f), FuncTabSettingsBtn);

			FuncModeSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("FuncModeSwitcher"));
			FuncOuterVBox->AddChildToVerticalBox(FuncModeSwitcher);

			// Sub-Mode 0: NRO Multi-Zone & Channel Switcher (Khu 1..N + World Boss Indicator)
			{
				UVerticalBox* ChanVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ChanVBox"));
				FuncModeSwitcher->AddChild(ChanVBox);

				UHorizontalBox* ChanHdr = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
				if (UVerticalBoxSlot* S = ChanVBox->AddChildToVerticalBox(ChanHdr))
				{
					S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
				}
				USizeBox* CHW = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				CHW->SetWidthOverride(318.0f);
				ChannelHeaderInfoText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ChannelHeaderInfoText"));
				StyleNROText(ChannelHeaderInfoText, 9, BrightGold);
				CHW->SetContent(ChannelHeaderInfoText);
				if (UHorizontalBoxSlot* S = ChanHdr->AddChildToHorizontalBox(CHW))
				{
					S->SetVerticalAlignment(VAlign_Center);
				}

				ChannelRefreshButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ChannelRefreshButton"));
				ChannelRefreshButton->SetBackgroundColor(FLinearColor(0.26f, 0.44f, 0.58f, 1.0f));
				UTextBlock* CRLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
				CRLbl->SetText(FText::FromString(TEXT(" LAM MOI ")));
				StyleNROText(CRLbl, 8, FLinearColor::White);
				ChannelRefreshButton->SetContent(CRLbl);
				ChanHdr->AddChildToHorizontalBox(ChannelRefreshButton);

				USizeBox* ChanScrollH = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				ChanScrollH->SetHeightOverride(242.0f);
				UScrollBox* ChanScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
				ChannelListVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ChannelListVBox"));
				ChanScroll->AddChild(ChannelListVBox);
				ChanScrollH->SetContent(ChanScroll);
				ChanVBox->AddChildToVerticalBox(ChanScrollH);
			}

			// Sub-Mode 1: World Boss Pity Token Redemption Shop (7 entries from pity_shop.json)
			{
				UVerticalBox* PityVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PityVBox"));
				FuncModeSwitcher->AddChild(PityVBox);

				PityHeaderInfoText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PityHeaderInfoText"));
				StyleNROText(PityHeaderInfoText, 9, BrightGold);
				if (UVerticalBoxSlot* S = PityVBox->AddChildToVerticalBox(PityHeaderInfoText))
				{
					S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
				}

				USizeBox* PityScrollH = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				PityScrollH->SetHeightOverride(236.0f);
				UScrollBox* PityScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
				PityShopListVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PityShopListVBox"));
				PityScroll->AddChild(PityShopListVBox);
				PityScrollH->SetContent(PityScroll);
				PityVBox->AddChildToVerticalBox(PityScrollH);
			}

			// Sub-Mode 2: Classic System & Camera Settings
			{
				UVerticalBox* FVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SettingsVBox"));
				FuncModeSwitcher->AddChild(FVBox);

				auto AddFuncBtn = [&](const TCHAR* Name, const TCHAR* Label, TObjectPtr<UButton>& OutBtn)
				{
					OutBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
					OutBtn->SetBackgroundColor(FLinearColor(0.32f, 0.22f, 0.12f, 1.0f));
					UTextBlock* Lbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
					Lbl->SetText(FText::FromString(Label));
					StyleNROText(Lbl, 9, FLinearColor(1.0f, 0.95f, 0.82f));
					OutBtn->SetContent(Lbl);
					if (UVerticalBoxSlot* BS = FVBox->AddChildToVerticalBox(OutBtn))
					{
						BS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
					}
				};

				AddFuncBtn(TEXT("FuncRefreshBtn"), TEXT(" Dong Bo Lai Ho So & Hanh Trang Server "), FuncRefreshButton);
				AddFuncBtn(TEXT("FuncRotModeBtn"), TEXT(" Doi Che Do Xoay Camera (Phim R) "), FuncRotModeButton);
				AddFuncBtn(TEXT("FuncDebugBtn"),   TEXT(" Bat / Tat Bang Telemetry Debug (F3) "), FuncDebugButton);
				AddFuncBtn(TEXT("FuncPvPBtn"),     TEXT(" Bat / Tat Co Hieu Luyen Tap PvP (F4) "), FuncPvPButton);
				AddFuncBtn(TEXT("FuncAuthBtn"),    TEXT(" Mo Bang Dang Nhap / Doi Nhan Vat (F5) "), FuncAuthButton);
			}

			TabContentSwitcher->AddChild(FuncPane);
		}

		// ====================================================================
		// 4. BOTTOM CURRENCY FOOTER BAR (Exact Image 1 Gold/Green/Ruby + Titan Token Bar!)
		// ====================================================================
		UBorder* FooterBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FooterCurrencyBar"));
		FooterBar->SetBrushColor(FLinearColor(0.30f, 0.21f, 0.11f, 0.98f));
		FooterBar->SetPadding(FMargin(8.0f, 4.0f));
		MainVBox->AddChildToVerticalBox(FooterBar);

		UVerticalBox* FooterVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		FooterBar->SetContent(FooterVBox);

		UHorizontalBox* CurrenciesRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CurrenciesRow"));
		FooterVBox->AddChildToVerticalBox(CurrenciesRow);

		FooterGoldText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FooterGoldText"));
		FooterPotentialText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FooterPotentialText"));
		FooterOreText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FooterOreText"));
		FooterTokenText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FooterTokenText"));

		StyleNROText(FooterGoldText, 9, FLinearColor(1.0f, 0.88f, 0.22f));
		StyleNROText(FooterPotentialText, 9, FLinearColor(0.42f, 1.0f, 0.58f));
		StyleNROText(FooterOreText, 9, FLinearColor(1.0f, 0.52f, 0.62f));
		StyleNROText(FooterTokenText, 9, FLinearColor(0.45f, 0.96f, 1.0f));

		if (UHorizontalBoxSlot* S1 = CurrenciesRow->AddChildToHorizontalBox(FooterGoldText))
		{
			S1->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
		}
		if (UHorizontalBoxSlot* S2 = CurrenciesRow->AddChildToHorizontalBox(FooterPotentialText))
		{
			S2->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
		}
		if (UHorizontalBoxSlot* S3 = CurrenciesRow->AddChildToHorizontalBox(FooterOreText))
		{
			S3->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
		}
		CurrenciesRow->AddChildToHorizontalBox(FooterTokenText);

		FeedbackStatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FeedbackStatusText"));
		FeedbackStatusText->SetText(FText::FromString(TEXT("Chon o Trang bi / Tui do de thao tac, hoac sang tab [Ky Nang] de cong Tiem Nang!")));
		StyleNROText(FeedbackStatusText, 9, FLinearColor(0.95f, 0.96f, 1.0f));
		if (UVerticalBoxSlot* FBS = FooterVBox->AddChildToVerticalBox(FeedbackStatusText))
		{
			FBS->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
		}

		if (TabContentSwitcher)
		{
			TabContentSwitcher->SetActiveWidgetIndex(static_cast<int32>(CurrentProfileTab));
		}
		UpdateTabHighlightVisuals();
		UpdateAllReadouts();
		RebuildPaperdollAndBagGrid();
	}

	return Super::RebuildWidget();
}

void UIU_InventoryEquipmentWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (TabContentSwitcher)
	{
		TabContentSwitcher->SetActiveWidgetIndex(static_cast<int32>(CurrentProfileTab));
	}

	if (CloseRedXButton)
	{
		CloseRedXButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleCloseClicked);
	}
	if (TabButtons.Num() >= 5)
	{
		if (TabButtons[0]) { TabButtons[0]->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleTabNhiemVuClicked); }
		if (TabButtons[1]) { TabButtons[1]->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleTabHanhTrangClicked); }
		if (TabButtons[2]) { TabButtons[2]->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleTabKyNangClicked); }
		if (TabButtons[3]) { TabButtons[3]->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleTabBangHoiClicked); }
		if (TabButtons[4]) { TabButtons[4]->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleTabChucNangClicked); }
	}
	if (UpgradeHPButton) { UpgradeHPButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleUpgradeHPClicked); }
	if (UpgradeKiButton) { UpgradeKiButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleUpgradeKiClicked); }
	if (UpgradeAtkButton) { UpgradeAtkButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleUpgradeAtkClicked); }

	if (GuildCreateButton) { GuildCreateButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleCreateGuildClicked); }
	if (GuildRefreshDirButton) { GuildRefreshDirButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleRefreshGuildClicked); }
	if (GuildSaveMOTDButton) { GuildSaveMOTDButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleSaveGuildMOTDClicked); }
	if (GuildDonate50Button) { GuildDonate50Button->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleDonate50GuildClicked); }
	if (GuildDonate150Button) { GuildDonate150Button->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleDonate150GuildClicked); }
	if (GuildRefreshRosterButton) { GuildRefreshRosterButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleRefreshGuildClicked); }
	if (GuildLeaveButton) { GuildLeaveButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleLeaveGuildClicked); }

	if (QuestRefreshButton) { QuestRefreshButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleRefreshQuestsClicked); }
	if (FuncTabChannelsBtn) { FuncTabChannelsBtn->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleFuncSubModeChannelsClicked); }
	if (FuncTabPityShopBtn) { FuncTabPityShopBtn->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleFuncSubModePityShopClicked); }
	if (FuncTabSettingsBtn) { FuncTabSettingsBtn->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleFuncSubModeSettingsClicked); }
	if (ChannelRefreshButton) { ChannelRefreshButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleRefreshChannelsClicked); }
	if (FuncRefreshButton) { FuncRefreshButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleRefreshClicked); }
	if (FuncDebugButton) { FuncDebugButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleFuncToggleDebugClicked); }
	if (FuncRotModeButton) { FuncRotModeButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleFuncToggleRotClicked); }
	if (FuncPvPButton) { FuncPvPButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleFuncTogglePvPClicked); }
	if (FuncAuthButton) { FuncAuthButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleFuncAuthPanelClicked); }

	RefreshBindingsAndRebuild();
}

void UIU_InventoryEquipmentWidget::NativeDestruct()
{
	UnbindAll();
	ActiveActionProxies.Empty();
	Super::NativeDestruct();
}

void UIU_InventoryEquipmentWidget::SetActiveProfileTab(EIU_CharacterProfileTab NewTab)
{
	CurrentProfileTab = NewTab;
	if (TabContentSwitcher)
	{
		TabContentSwitcher->SetActiveWidgetIndex(static_cast<int32>(CurrentProfileTab));
	}
	if (CurrentProfileTab == EIU_CharacterProfileTab::NhiemVu)
	{
		if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
		{
			if (IUPC->HasCachedQuestSnapshot())
			{
				DisplayedQuestSnapshot = IUPC->GetCachedQuestSnapshot();
				bHasQuestSnapshotData = true;
			}
			IUPC->Server_RequestRefreshQuests();
		}
		RebuildQuestPaneView();
	}
	else if (CurrentProfileTab == EIU_CharacterProfileTab::BangHoi)
	{
		if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
		{
			if (IUPC->HasCachedGuildSnapshot())
			{
				DisplayedGuildSnapshot = IUPC->GetCachedGuildSnapshot();
				bHasGuildSnapshotData = true;
			}
			IUPC->Server_RequestRefreshGuild();
		}
		RebuildGuildPaneView();
	}
	else if (CurrentProfileTab == EIU_CharacterProfileTab::ChucNang)
	{
		if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
		{
			if (IUPC->HasCachedWorldChannels())
			{
				DisplayedWorldChannels = IUPC->GetCachedWorldChannels();
				bHasWorldChannelData = true;
			}
			if (IUPC->HasCachedPityShopSnapshot())
			{
				DisplayedPityShopSnapshot = IUPC->GetCachedPityShopSnapshot();
				bHasPityShopData = true;
			}
			IUPC->Server_RequestRefreshChannels();
			IUPC->Server_RequestRefreshPityShop();
		}
		RebuildChannelListView();
		RebuildPityShopView();
	}
	UpdateTabHighlightVisuals();
	UpdateAllReadouts();
}

void UIU_InventoryEquipmentWidget::UpdateTabHighlightVisuals()
{
	const int32 ActiveIdx = static_cast<int32>(CurrentProfileTab);
	for (int32 i = 0; i < TabOuterBorders.Num(); ++i)
	{
		if (UBorder* B = TabOuterBorders[i])
		{
			B->SetBrush((i == ActiveIdx) ? ActiveTabGreenBrush : InactiveTabCreamBrush);
		}
	}
}

void UIU_InventoryEquipmentWidget::RefreshBindingsAndRebuild()
{
	UnbindAll();

	APawn* Pawn = GetOwningPlayerPawn();
	if (Pawn)
	{
		if (UIU_AttributeComponent* Attr = Pawn->FindComponentByClass<UIU_AttributeComponent>())
		{
			BoundAttr = Attr;
			Attr->OnHPChanged.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleAnyVitalOrEconomyChanged);
			Attr->OnKiChanged.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleAnyVitalOrEconomyChanged);
			Attr->OnAttackPowerChanged.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleAttackOrDefenseChanged);
			Attr->OnDefenseChanged.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleAttackOrDefenseChanged);
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
		PS->OnCharacterEXPChanged.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleCharacterEXPChanged);
		PS->OnCharacterPotentialChanged.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleCharacterPotentialChanged);
		PS->OnCharacterEconomyChanged.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleEconomySummaryChanged);
	}

	if (const AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		if (IUPC->HasCachedInventorySnapshot())
		{
			DisplayedSnapshot = IUPC->GetCachedInventorySnapshot();
			bHasSnapshotData = true;
		}
		if (IUPC->HasCachedGuildSnapshot())
		{
			DisplayedGuildSnapshot = IUPC->GetCachedGuildSnapshot();
			bHasGuildSnapshotData = true;
		}
		if (IUPC->HasCachedQuestSnapshot())
		{
			DisplayedQuestSnapshot = IUPC->GetCachedQuestSnapshot();
			bHasQuestSnapshotData = true;
		}
		if (IUPC->HasCachedWorldChannels())
		{
			DisplayedWorldChannels = IUPC->GetCachedWorldChannels();
			bHasWorldChannelData = true;
		}
		if (IUPC->HasCachedPityShopSnapshot())
		{
			DisplayedPityShopSnapshot = IUPC->GetCachedPityShopSnapshot();
			bHasPityShopData = true;
		}
	}

	UpdateAllReadouts();
	RebuildPaperdollAndBagGrid();
	RebuildGuildPaneView();
	RebuildQuestPaneView();
	RebuildChannelListView();
	RebuildPityShopView();
}

void UIU_InventoryEquipmentWidget::UnbindAll()
{
	if (UIU_AttributeComponent* Attr = BoundAttr.Get())
	{
		Attr->OnHPChanged.RemoveDynamic(this, &UIU_InventoryEquipmentWidget::HandleAnyVitalOrEconomyChanged);
		Attr->OnKiChanged.RemoveDynamic(this, &UIU_InventoryEquipmentWidget::HandleAnyVitalOrEconomyChanged);
		Attr->OnAttackPowerChanged.RemoveDynamic(this, &UIU_InventoryEquipmentWidget::HandleAttackOrDefenseChanged);
		Attr->OnDefenseChanged.RemoveDynamic(this, &UIU_InventoryEquipmentWidget::HandleAttackOrDefenseChanged);
	}
	if (AIU_PlayerState* PS = BoundPS.Get())
	{
		PS->OnCharacterEXPChanged.RemoveDynamic(this, &UIU_InventoryEquipmentWidget::HandleCharacterEXPChanged);
		PS->OnCharacterPotentialChanged.RemoveDynamic(this, &UIU_InventoryEquipmentWidget::HandleCharacterPotentialChanged);
		PS->OnCharacterEconomyChanged.RemoveDynamic(this, &UIU_InventoryEquipmentWidget::HandleEconomySummaryChanged);
	}
	BoundAttr.Reset();
	BoundPS.Reset();
}

void UIU_InventoryEquipmentWidget::ApplyInventorySnapshot(const FIU_InventorySnapshot& Snapshot)
{
	DisplayedSnapshot = Snapshot;
	bHasSnapshotData = true;

	if (!Snapshot.ActionResult.IsEmpty())
	{
		const bool bEnhanceFailed = Snapshot.bHasEnhanceOutcome && !Snapshot.bEnhanceSucceeded;
		SetFeedbackMessage(Snapshot.ActionResult, bEnhanceFailed);
	}

	UpdateAllReadouts();
	RebuildPaperdollAndBagGrid();
}

void UIU_InventoryEquipmentWidget::SetFeedbackMessage(const FString& Message, bool bIsError)
{
	if (FeedbackStatusText)
	{
		FeedbackStatusText->SetText(FText::FromString(Message));
		FeedbackStatusText->SetColorAndOpacity(FSlateColor(
			bIsError ? FLinearColor(1.0f, 0.48f, 0.38f) : FLinearColor(0.55f, 1.0f, 0.72f)));
	}
}

void UIU_InventoryEquipmentWidget::ExecuteRowAction(
	EIU_InvRowAction Action,
	const FString& ItemInstanceID,
	const FString& SlotID,
	int32 BagSlotIndex,
	bool bTargetLockState)
{
	if (Action == EIU_InvRowAction::SelectEquippedSlot)
	{
		SelectedEquippedSlotID = SlotID;
		SelectedItemInstanceID = ItemInstanceID;
		SelectedBagSlotIndex = -1;
		RebuildPaperdollAndBagGrid();
		return;
	}
	if (Action == EIU_InvRowAction::SelectBagCell)
	{
		SelectedBagSlotIndex = BagSlotIndex;
		SelectedItemInstanceID = ItemInstanceID;
		SelectedEquippedSlotID.Empty();
		RebuildPaperdollAndBagGrid();
		return;
	}

	AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer());
	if (!IUPC)
	{
		return;
	}

	switch (Action)
	{
	case EIU_InvRowAction::Equip:
		SetFeedbackMessage(TEXT("Dang mac trang bi vao Paperdoll..."));
		IUPC->Server_RequestEquipItem(ItemInstanceID);
		break;
	case EIU_InvRowAction::Unequip:
		SetFeedbackMessage(TEXT("Dang thao trang bi ve Hanh Trang..."));
		IUPC->Server_RequestUnequipSlot(SlotID);
		break;
	case EIU_InvRowAction::UseConsumable:
		SetFeedbackMessage(TEXT("Dang su dung Dau Than (+150 HP/Ki)..."));
		IUPC->Server_RequestUseConsumable(ItemInstanceID);
		break;
	case EIU_InvRowAction::Enhance:
		SetFeedbackMessage(TEXT("Dang cuong hoa trang bi (CSPRNG + Safe Floor)..."));
		IUPC->Server_RequestEnhanceItem(ItemInstanceID);
		break;
	case EIU_InvRowAction::ToggleLock:
		SetFeedbackMessage(bTargetLockState ? TEXT("Dang khoa bao ve vat pham...") : TEXT("Dang mo khoa vat pham..."));
		IUPC->Server_RequestToggleItemLock(ItemInstanceID, bTargetLockState);
		break;
	case EIU_InvRowAction::SellOne:
		SetFeedbackMessage(TEXT("Dang ban vat pham nhan Vang..."));
		IUPC->Server_RequestSellItem(ItemInstanceID, 1);
		break;
	case EIU_InvRowAction::JoinGuildByID:
		SetFeedbackMessage(TEXT("Dang gui yeu cau gia nhap Bang hoi..."));
		IUPC->Server_RequestJoinGuild(SlotID);
		break;
	case EIU_InvRowAction::PromoteGuildVice:
		SetFeedbackMessage(TEXT("Dang phong chuc Pho Bang..."));
		IUPC->Server_RequestSetGuildMemberRole(ItemInstanceID, TEXT("VICE_LEADER"));
		break;
	case EIU_InvRowAction::KickGuildMember:
		SetFeedbackMessage(TEXT("Dang truc xuat thanh vien khoi Bang..."));
		IUPC->Server_RequestKickGuildMember(ItemInstanceID);
		break;
	case EIU_InvRowAction::SwitchZoneChannel:
		SetFeedbackMessage(FString::Printf(TEXT("Dang khoi hanh Tau Vu Tru sang %s (Khu %d)..."), *SlotID, BagSlotIndex));
		IUPC->Server_RequestZoneTransfer(SlotID, BagSlotIndex);
		break;
	case EIU_InvRowAction::ClaimQuestReward:
		SetFeedbackMessage(TEXT("Dang nhan thuong Nhiem Vu (Vang + SM/TN + Huy Chuong)..."));
		IUPC->Server_RequestClaimQuest(SlotID);
		break;
	case EIU_InvRowAction::RepeatQuest:
		SetFeedbackMessage(TEXT("Dang nhan lai Nhiem Vu..."));
		IUPC->Server_RequestRepeatQuest(SlotID);
		break;
	case EIU_InvRowAction::RedeemPityShopItem:
		SetFeedbackMessage(TEXT("Dang doi vat pham Bao Hiem Boss..."));
		IUPC->Server_RequestRedeemPityEntry(SlotID);
		break;
	default:
		break;
	}
}

void UIU_InventoryEquipmentWidget::ApplyQuestSnapshot(const FIU_QuestSnapshot& Snapshot)
{
	DisplayedQuestSnapshot = Snapshot;
	bHasQuestSnapshotData = true;
	if (!Snapshot.ActionMessage.IsEmpty())
	{
		SetFeedbackMessage(Snapshot.ActionMessage, false);
	}
	RebuildQuestPaneView();
	UpdateAllReadouts();
}

void UIU_InventoryEquipmentWidget::ApplyWorldChannels(const TArray<FIU_ZoneChannelView>& Channels)
{
	DisplayedWorldChannels = Channels;
	bHasWorldChannelData = true;
	RebuildChannelListView();
}

void UIU_InventoryEquipmentWidget::ApplyPityShopSnapshot(const FIU_PityShopSnapshot& Snapshot)
{
	DisplayedPityShopSnapshot = Snapshot;
	bHasPityShopData = true;
	if (!Snapshot.ActionMessage.IsEmpty())
	{
		SetFeedbackMessage(Snapshot.ActionMessage, false);
	}
	RebuildPityShopView();
	UpdateAllReadouts();
}

void UIU_InventoryEquipmentWidget::SetActiveFuncSubMode(int32 SubModeIndex)
{
	CurrentFuncSubMode = FMath::Clamp(SubModeIndex, 0, 2);
	if (FuncModeSwitcher)
	{
		FuncModeSwitcher->SetActiveWidgetIndex(CurrentFuncSubMode);
	}
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		if (CurrentFuncSubMode == 0) { IUPC->Server_RequestRefreshChannels(); }
		else if (CurrentFuncSubMode == 1) { IUPC->Server_RequestRefreshPityShop(); }
	}
}

void UIU_InventoryEquipmentWidget::RebuildQuestPaneView()
{
	if (!WidgetTree || !QuestListVBox)
	{
		return;
	}
	if (!bHasQuestSnapshotData || DisplayedQuestSnapshot.Quests.Num() == 0)
	{
		return;
	}

	QuestListVBox->ClearChildren();
	for (const FIU_QuestEntryView& Q : DisplayedQuestSnapshot.Quests)
	{
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Card->SetBrushColor(FLinearColor(0.31f, 0.22f, 0.12f, 0.97f));
		Card->SetPadding(FMargin(7.0f, 5.0f));
		if (UVerticalBoxSlot* CS = QuestListVBox->AddChildToVerticalBox(Card))
		{
			CS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
		}

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Card->SetContent(Row);

		const bool bReady = (Q.QuestState == TEXT("READY_TO_TURN_IN"));
		const bool bDone = (Q.QuestState == TEXT("COMPLETED"));
		const FString StateTag = bReady
			? FString::Printf(TEXT("[SAN SANG %d/%d]"), Q.CurrentCount, Q.TargetCount)
			: bDone
			? FString::Printf(TEXT("[HOAN THANH x%d]"), FMath::Max(1, Q.RepeatCount))
			: FString::Printf(TEXT("[DANG LAM %d/%d]"), Q.CurrentCount, Q.TargetCount);

		USizeBox* InfoW = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		InfoW->SetWidthOverride((bReady || bDone) ? 286.0f : 386.0f);
		UTextBlock* InfoTxt = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		InfoTxt->SetText(FText::FromString(FString::Printf(
			TEXT("%s %s\n%s\nThuong: +%lld Vang | +%lld SM/TN | +%d Huy Chuong Titan"),
			*StateTag, *Q.Title, *Q.Description, Q.RewardCoin, Q.RewardExpAndPot, Q.RewardTitanToken)));
		StyleNROText(InfoTxt, 8, bReady ? FLinearColor(0.45f, 1.0f, 0.62f) : (bDone ? FLinearColor(1.0f, 0.90f, 0.42f) : FLinearColor(0.96f, 0.94f, 0.86f)));
		InfoW->SetContent(InfoTxt);
		if (UHorizontalBoxSlot* IS = Row->AddChildToHorizontalBox(InfoW))
		{
			IS->SetVerticalAlignment(VAlign_Center);
			IS->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
		}

		if (bReady || bDone)
		{
			UButton* ActBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
			ActBtn->SetBackgroundColor(bReady ? FLinearColor(0.16f, 0.62f, 0.26f, 1.0f) : FLinearColor(0.24f, 0.44f, 0.62f, 1.0f));
			UTextBlock* Lbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Lbl->SetText(FText::FromString(bReady ? TEXT(" NHAN THUONG ") : TEXT(" LAM LAI ")));
			StyleNROText(Lbl, 8, FLinearColor::White);
			ActBtn->SetContent(Lbl);

			UIU_InventoryActionProxy* Proxy = NewObject<UIU_InventoryActionProxy>(this);
			Proxy->OwnerWidget = this;
			Proxy->Action = bReady ? EIU_InvRowAction::ClaimQuestReward : EIU_InvRowAction::RepeatQuest;
			Proxy->SlotID = Q.QuestID;
			ActiveActionProxies.Add(Proxy);
			ActBtn->OnClicked.AddDynamic(Proxy, &UIU_InventoryActionProxy::HandleButtonClicked);
			if (UHorizontalBoxSlot* BS = Row->AddChildToHorizontalBox(ActBtn))
			{
				BS->SetVerticalAlignment(VAlign_Center);
			}
		}
	}
}

void UIU_InventoryEquipmentWidget::RebuildChannelListView()
{
	if (!WidgetTree || !ChannelListVBox)
	{
		return;
	}
	const AIU_PlayerState* PS = BoundPS.Get();
	const FString CurZone = PS ? PS->GetCurrentZoneID() : TEXT("zone.p01_west_hub");
	const int32 CurKhu = PS ? PS->GetCurrentChannelID() : 1;

	if (ChannelHeaderInfoText)
	{
		const FString ZName = (CurZone == TEXT("zone.p01_highland_01")) ? TEXT("Cao Nguyen Titan") : TEXT("Tram Tay Nam");
		ChannelHeaderInfoText->SetText(FText::FromString(FString::Printf(TEXT("Vi tri hien tai: %s - KHU %d"), *ZName, CurKhu)));
	}

	ChannelListVBox->ClearChildren();
	if (DisplayedWorldChannels.Num() == 0)
	{
		UTextBlock* EmptyT = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		EmptyT->SetText(FText::FromString(TEXT("Dang cap nhat danh sach Khu / Vung tu World Coordinator...")));
		StyleNROText(EmptyT, 8, FLinearColor(0.95f, 0.92f, 0.84f));
		ChannelListVBox->AddChildToVerticalBox(EmptyT);
		return;
	}

	for (const FIU_ZoneChannelView& Ch : DisplayedWorldChannels)
	{
		UBorder* RowBdr = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		const bool bIsCurrent = (Ch.ZoneID == CurZone && Ch.ChannelID == CurKhu);
		RowBdr->SetBrushColor(bIsCurrent ? FLinearColor(0.20f, 0.36f, 0.22f, 0.98f) : FLinearColor(0.31f, 0.22f, 0.12f, 0.96f));
		RowBdr->SetPadding(FMargin(6.0f, 4.5f));
		if (UVerticalBoxSlot* RS = ChannelListVBox->AddChildToVerticalBox(RowBdr))
		{
			RS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		}

		UHorizontalBox* RowH = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		RowBdr->SetContent(RowH);

		const FLinearColor LoadCol = (Ch.StatusColor == TEXT("RED"))
			? FLinearColor(1.0f, 0.38f, 0.32f)
			: (Ch.StatusColor == TEXT("YELLOW"))
			? FLinearColor(1.0f, 0.86f, 0.25f)
			: FLinearColor(0.45f, 1.0f, 0.58f);
		const FString BossBadge = Ch.bBossAlive
			? FString::Printf(TEXT("  [BOSS TITAN %.0f%%]"), Ch.BossHPRatio * 100.0f)
			: TEXT("");

		USizeBox* InfoW = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		InfoW->SetWidthOverride(292.0f);
		UTextBlock* InfoT = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		InfoT->SetText(FText::FromString(FString::Printf(
			TEXT("[KHU %d] %s (%d/%d - %s)%s\nMay chu: %s"),
			Ch.ChannelID,
			Ch.ZoneLabel.IsEmpty() ? *Ch.ZoneID : *Ch.ZoneLabel,
			Ch.OnlineCCU, Ch.SoftCapCCU, *Ch.StatusColor, *BossBadge, *Ch.ServerAddr)));
		StyleNROText(InfoT, 8, LoadCol);
		InfoW->SetContent(InfoT);
		if (UHorizontalBoxSlot* IS = RowH->AddChildToHorizontalBox(InfoW))
		{
			IS->SetVerticalAlignment(VAlign_Center);
			IS->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
		}

		if (bIsCurrent)
		{
			UTextBlock* HereT = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			HereT->SetText(FText::FromString(TEXT("[DANG O DAY]")));
			StyleNROText(HereT, 8, FLinearColor(0.65f, 1.0f, 0.72f));
			if (UHorizontalBoxSlot* HS = RowH->AddChildToHorizontalBox(HereT))
			{
				HS->SetVerticalAlignment(VAlign_Center);
			}
		}
		else
		{
			UButton* GoBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
			GoBtn->SetBackgroundColor(FLinearColor(0.16f, 0.58f, 0.28f, 1.0f));
			UTextBlock* GoLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			GoLbl->SetText(FText::FromString(TEXT(" CHUYEN KHU ")));
			StyleNROText(GoLbl, 8, FLinearColor::White);
			GoBtn->SetContent(GoLbl);

			UIU_InventoryActionProxy* Proxy = NewObject<UIU_InventoryActionProxy>(this);
			Proxy->OwnerWidget = this;
			Proxy->Action = EIU_InvRowAction::SwitchZoneChannel;
			Proxy->SlotID = Ch.ZoneID;
			Proxy->BagSlotIndex = Ch.ChannelID;
			ActiveActionProxies.Add(Proxy);
			GoBtn->OnClicked.AddDynamic(Proxy, &UIU_InventoryActionProxy::HandleButtonClicked);
			if (UHorizontalBoxSlot* BS = RowH->AddChildToHorizontalBox(GoBtn))
			{
				BS->SetVerticalAlignment(VAlign_Center);
			}
		}
	}
}

void UIU_InventoryEquipmentWidget::RebuildPityShopView()
{
	if (!WidgetTree || !PityShopListVBox)
	{
		return;
	}
	const AIU_PlayerState* PS = BoundPS.Get();
	const int32 Tokens = bHasPityShopData ? DisplayedPityShopSnapshot.TokenBalance : (PS ? PS->GetPityTitanTokens() : 0);

	if (PityHeaderInfoText)
	{
		PityHeaderInfoText->SetText(FText::FromString(FString::Printf(
			TEXT("Huy Chuong Titan: %d HC  |  Bao Hiem Hard Pity: %d/%d (Lan 5 -> 100%% Do Hi!)"),
			Tokens, DisplayedPityShopSnapshot.ConsecutiveDryKills, FMath::Max(1, DisplayedPityShopSnapshot.HardPityThreshold))));
	}

	PityShopListVBox->ClearChildren();
	if (DisplayedPityShopSnapshot.Entries.Num() == 0)
	{
		UTextBlock* EmptyT = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		EmptyT->SetText(FText::FromString(TEXT("Dang tai danh muc 7 vat pham Shop Bao Hiem Boss (pity_shop.json)...")));
		StyleNROText(EmptyT, 8, FLinearColor(0.95f, 0.92f, 0.84f));
		PityShopListVBox->AddChildToVerticalBox(EmptyT);
		return;
	}

	for (const FIU_PityShopEntryView& EV : DisplayedPityShopSnapshot.Entries)
	{
		UBorder* RowBdr = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		RowBdr->SetBrushColor(FLinearColor(0.31f, 0.22f, 0.12f, 0.96f));
		RowBdr->SetPadding(FMargin(6.0f, 4.0f));
		if (UVerticalBoxSlot* RS = PityShopListVBox->AddChildToVerticalBox(RowBdr))
		{
			RS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 3.5f));
		}

		UHorizontalBox* RowH = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		RowBdr->SetContent(RowH);

		const FString LimitStr = (EV.DailyLimit > 0)
			? FString::Printf(TEXT("GH Ngay: %d/%d"), EV.DailyPurchased, EV.DailyLimit)
			: TEXT("Khong GH");
		const FString QtyStr = (EV.RewardQuantity > 1) ? FString::Printf(TEXT(" x%d"), EV.RewardQuantity) : TEXT("");

		USizeBox* InfoW = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		InfoW->SetWidthOverride(302.0f);
		UTextBlock* InfoT = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		InfoT->SetText(FText::FromString(FString::Printf(
			TEXT("%s%s [%s]\nGia: %d Huy Chuong + %lld Vang  |  %s"),
			*EV.RewardName, *QtyStr, *EV.RewardRarity, EV.TokenCost, EV.StandardCoinCost, *LimitStr)));
		StyleNROText(InfoT, 8, GetRarityTintColor(EV.RewardRarity));
		InfoW->SetContent(InfoT);
		if (UHorizontalBoxSlot* IS = RowH->AddChildToHorizontalBox(InfoW))
		{
			IS->SetVerticalAlignment(VAlign_Center);
			IS->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
		}

		UButton* BuyBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		const bool bSoldOutToday = (EV.DailyLimit > 0 && EV.DailyPurchased >= EV.DailyLimit);
		BuyBtn->SetBackgroundColor(bSoldOutToday ? FLinearColor(0.35f, 0.32f, 0.30f, 1.0f) : FLinearColor(0.80f, 0.46f, 0.08f, 1.0f));
		UTextBlock* BuyLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		BuyLbl->SetText(FText::FromString(bSoldOutToday ? TEXT(" HET LUOT ") : TEXT(" DOI NGAY ")));
		StyleNROText(BuyLbl, 8, FLinearColor::White);
		BuyBtn->SetContent(BuyLbl);

		if (!bSoldOutToday)
		{
			UIU_InventoryActionProxy* Proxy = NewObject<UIU_InventoryActionProxy>(this);
			Proxy->OwnerWidget = this;
			Proxy->Action = EIU_InvRowAction::RedeemPityShopItem;
			Proxy->SlotID = EV.EntryID;
			ActiveActionProxies.Add(Proxy);
			BuyBtn->OnClicked.AddDynamic(Proxy, &UIU_InventoryActionProxy::HandleButtonClicked);
		}
		if (UHorizontalBoxSlot* BS = RowH->AddChildToHorizontalBox(BuyBtn))
		{
			BS->SetVerticalAlignment(VAlign_Center);
		}
	}
}

void UIU_InventoryEquipmentWidget::ApplyGuildSnapshot(const FIU_GuildSnapshot& Snapshot)
{
	DisplayedGuildSnapshot = Snapshot;
	bHasGuildSnapshotData = true;
	if (!Snapshot.ActionMessage.IsEmpty())
	{
		SetFeedbackMessage(Snapshot.ActionMessage, false);
	}
	RebuildGuildPaneView();
	UpdateAllReadouts();
}

void UIU_InventoryEquipmentWidget::RebuildGuildPaneView()
{
	if (!WidgetTree || !GuildModeSwitcher)
	{
		return;
	}

	GuildModeSwitcher->SetActiveWidgetIndex(DisplayedGuildSnapshot.bInGuild ? 1 : 0);

	if (!DisplayedGuildSnapshot.bInGuild)
	{
		if (!GuildDirectoryListBox) { return; }
		GuildDirectoryListBox->ClearChildren();

		if (DisplayedGuildSnapshot.Directory.Num() == 0)
		{
			UTextBlock* EmptyTxt = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			EmptyTxt->SetText(FText::FromString(TEXT("Chua co Bang Hoi nao tren may chu. Hay la nguoi dau tien lap Bang!")));
			StyleNROText(EmptyTxt, 9, FLinearColor(0.95f, 0.92f, 0.84f));
			GuildDirectoryListBox->AddChildToVerticalBox(EmptyTxt);
			return;
		}

		for (const FIU_GuildDirectoryEntry& Dir : DisplayedGuildSnapshot.Directory)
		{
			UBorder* RowBdr = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			RowBdr->SetBrushColor(FLinearColor(0.32f, 0.23f, 0.13f, 0.96f));
			RowBdr->SetPadding(FMargin(6.0f, 4.0f));
			if (UVerticalBoxSlot* RS = GuildDirectoryListBox->AddChildToVerticalBox(RowBdr))
			{
				RS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
			}

			UHorizontalBox* RowH = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			RowBdr->SetContent(RowH);

			USizeBox* InfoW = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			InfoW->SetWidthOverride(305.0f);
			UTextBlock* InfoT = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			InfoT->SetText(FText::FromString(FString::Printf(
				TEXT("[%s] %s (Lv.%d | TV: %d/%d)\nBang chu: %s"),
				*Dir.GuildTag, *Dir.GuildName, Dir.GuildLevel, Dir.MemberCount, Dir.MemberCap, *Dir.LeaderName)));
			StyleNROText(InfoT, 8, FLinearColor(0.98f, 0.95f, 0.85f));
			InfoW->SetContent(InfoT);
			RowH->AddChildToHorizontalBox(InfoW);

			UButton* JoinBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
			JoinBtn->SetBackgroundColor(FLinearColor(0.16f, 0.58f, 0.26f, 1.0f));
			UTextBlock* JoinLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			JoinLbl->SetText(FText::FromString(TEXT(" XIN VAO ")));
			StyleNROText(JoinLbl, 8, FLinearColor::White);
			JoinBtn->SetContent(JoinLbl);

			UIU_InventoryActionProxy* Proxy = NewObject<UIU_InventoryActionProxy>(this);
			Proxy->OwnerWidget = this;
			Proxy->Action = EIU_InvRowAction::JoinGuildByID;
			Proxy->SlotID = Dir.GuildID;
			ActiveActionProxies.Add(Proxy);
			JoinBtn->OnClicked.AddDynamic(Proxy, &UIU_InventoryActionProxy::HandleButtonClicked);
			RowH->AddChildToHorizontalBox(JoinBtn);
		}
		return;
	}

	if (GuildHeaderSummaryText)
	{
		GuildHeaderSummaryText->SetText(FText::FromString(FString::Printf(
			TEXT("[%s] %s  |  Cap Bang Lv.%d (EXP: %lld)\nQuy Bang: %lld Vang  |  Thanh vien: %d/%d  |  Vai tro: %s (CH: %lld)"),
			*DisplayedGuildSnapshot.GuildTag,
			*DisplayedGuildSnapshot.GuildName,
			DisplayedGuildSnapshot.GuildLevel,
			DisplayedGuildSnapshot.GuildExp,
			DisplayedGuildSnapshot.TreasuryStandardCoin,
			DisplayedGuildSnapshot.MemberCount,
			DisplayedGuildSnapshot.MemberCap,
			*DisplayedGuildSnapshot.MyRole,
			DisplayedGuildSnapshot.MyTotalContribution)));
	}
	if (GuildMOTDInputBox && !GuildMOTDInputBox->HasKeyboardFocus())
	{
		GuildMOTDInputBox->SetText(FText::FromString(DisplayedGuildSnapshot.MOTD));
	}

	if (!GuildMembersListBox) { return; }
	GuildMembersListBox->ClearChildren();

	const AIU_PlayerState* PS = BoundPS.Get();
	const FString MyCharID = PS ? PS->GetPersistedCharacterUUID() : TEXT("");
	const bool bAmLeader = (DisplayedGuildSnapshot.MyRole == TEXT("LEADER"));

	for (const FIU_GuildMemberView& MV : DisplayedGuildSnapshot.Members)
	{
		UBorder* RowBdr = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		RowBdr->SetBrushColor(FLinearColor(0.32f, 0.23f, 0.13f, 0.96f));
		RowBdr->SetPadding(FMargin(6.0f, 3.5f));
		if (UVerticalBoxSlot* RS = GuildMembersListBox->AddChildToVerticalBox(RowBdr))
		{
			RS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 3.5f));
		}

		UHorizontalBox* RowH = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		RowBdr->SetContent(RowH);

		USizeBox* InfoW = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		InfoW->SetWidthOverride(bAmLeader && MV.CharacterID != MyCharID ? 250.0f : 380.0f);
		UTextBlock* InfoT = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		InfoT->SetText(FText::FromString(FString::Printf(
			TEXT("[%s] Lv.%d %s  |  Cong hien: %lld V"),
			*MV.MemberRole, MV.Level, *MV.CharacterName, MV.TotalContribution)));
		StyleNROText(InfoT, 8, (MV.MemberRole == TEXT("LEADER")) ? FLinearColor(1.0f, 0.90f, 0.28f) : FLinearColor(0.95f, 0.95f, 0.88f));
		InfoW->SetContent(InfoT);
		RowH->AddChildToHorizontalBox(InfoW);

		if (bAmLeader && MV.CharacterID != MyCharID)
		{
			if (MV.MemberRole != TEXT("VICE_LEADER"))
			{
				UButton* PromBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
				PromBtn->SetBackgroundColor(FLinearColor(0.24f, 0.50f, 0.62f, 1.0f));
				UTextBlock* PLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
				PLbl->SetText(FText::FromString(TEXT(" PHO ")));
				StyleNROText(PLbl, 7, FLinearColor::White);
				PromBtn->SetContent(PLbl);

				UIU_InventoryActionProxy* PProxy = NewObject<UIU_InventoryActionProxy>(this);
				PProxy->OwnerWidget = this;
				PProxy->Action = EIU_InvRowAction::PromoteGuildVice;
				PProxy->ItemInstanceID = MV.CharacterID;
				ActiveActionProxies.Add(PProxy);
				PromBtn->OnClicked.AddDynamic(PProxy, &UIU_InventoryActionProxy::HandleButtonClicked);
				if (UHorizontalBoxSlot* S = RowH->AddChildToHorizontalBox(PromBtn))
				{
					S->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));
				}
			}

			UButton* KickBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
			KickBtn->SetBackgroundColor(FLinearColor(0.66f, 0.18f, 0.12f, 1.0f));
			UTextBlock* KLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			KLbl->SetText(FText::FromString(TEXT(" KICK ")));
			StyleNROText(KLbl, 7, FLinearColor::White);
			KickBtn->SetContent(KLbl);

			UIU_InventoryActionProxy* KProxy = NewObject<UIU_InventoryActionProxy>(this);
			KProxy->OwnerWidget = this;
			KProxy->Action = EIU_InvRowAction::KickGuildMember;
			KProxy->ItemInstanceID = MV.CharacterID;
			ActiveActionProxies.Add(KProxy);
			KickBtn->OnClicked.AddDynamic(KProxy, &UIU_InventoryActionProxy::HandleButtonClicked);
			RowH->AddChildToHorizontalBox(KickBtn);
		}
	}
}

void UIU_InventoryEquipmentWidget::HandleCreateGuildClicked()
{
	AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer());
	if (!IUPC) { return; }
	const FString NameStr = GuildCreateNameInput ? GuildCreateNameInput->GetText().ToString().TrimStartAndEnd() : TEXT("Zenith Vanguards");
	const FString TagStr = GuildCreateTagInput ? GuildCreateTagInput->GetText().ToString().TrimStartAndEnd().ToUpper() : TEXT("ZNT");
	SetFeedbackMessage(FString::Printf(TEXT("Dang thanh lap Bang [%s] %s (-200 Vang)..."), *TagStr, *NameStr));
	IUPC->Server_RequestCreateGuild(NameStr, TagStr, TEXT("Doan ket san Boss cung Bang hoi!"));
}

void UIU_InventoryEquipmentWidget::HandleRefreshGuildClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		SetFeedbackMessage(TEXT("Dang tai thong tin Bang hoi tu PostgreSQL 18..."));
		IUPC->Server_RequestRefreshGuild();
	}
}

void UIU_InventoryEquipmentWidget::HandleSaveGuildMOTDClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		const FString NewMOTD = GuildMOTDInputBox ? GuildMOTDInputBox->GetText().ToString() : TEXT("");
		IUPC->Server_RequestUpdateGuildMOTD(NewMOTD);
	}
}

void UIU_InventoryEquipmentWidget::HandleDonate50GuildClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		SetFeedbackMessage(TEXT("Dang quyen gop 50 Vang vao Quy Bang..."));
		IUPC->Server_RequestDonateGuildCoins(50);
	}
}

void UIU_InventoryEquipmentWidget::HandleDonate150GuildClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		SetFeedbackMessage(TEXT("Dang quyen gop 150 Vang vao Quy Bang..."));
		IUPC->Server_RequestDonateGuildCoins(150);
	}
}

void UIU_InventoryEquipmentWidget::HandleLeaveGuildClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		SetFeedbackMessage(TEXT("Dang xu ly roi / giai tan Bang hoi..."));
		IUPC->Server_RequestLeaveGuild();
	}
}

void UIU_InventoryEquipmentWidget::UpdateAllReadouts()
{
	const UIU_AttributeComponent* Attr = BoundAttr.Get();
	const AIU_PlayerState* PS = BoundPS.Get();

	const int32 Lv = PS ? PS->GetCharacterLevel() : (bHasSnapshotData ? DisplayedSnapshot.Level : 1);
	const int64 CurEXP = PS ? PS->GetCurrentEXP() : 0;
	const int64 NextEXP = PS ? PS->GetNextLevelEXP() : 100;
	const int64 PoolTN = PS ? PS->GetPotentialPoints() : 0;
	const int32 HPUpg = PS ? PS->GetHPUpgrades() : 0;
	const int32 KiUpg = PS ? PS->GetKiUpgrades() : 0;
	const int32 AtkUpg = PS ? PS->GetAtkUpgrades() : 0;
	const int64 HPCost = PS ? PS->GetNextHPCost() : 100;
	const int64 KiCost = PS ? PS->GetNextKiCost() : 100;
	const int64 AtkCost = PS ? PS->GetNextAtkCost() : 150;

	const int64 Coin = bHasSnapshotData ? DisplayedSnapshot.StandardCoin : (PS ? PS->GetStandardCoin() : 0);
	const int32 Ore = bHasSnapshotData ? DisplayedSnapshot.CrimsonOreCount : (PS ? PS->GetCrimsonOreCount() : 0);
	const int32 EqCount = bHasSnapshotData ? DisplayedSnapshot.EquippedItems.Num() : (PS ? PS->GetEquippedItemCount() : 0);
	const int32 UsedBag = bHasSnapshotData ? DisplayedSnapshot.UsedBagSlots : 0;

	const float CurHP = Attr ? Attr->GetHP() : (bHasSnapshotData ? DisplayedSnapshot.HP : 200.0f);
	const float MaxHP = Attr ? Attr->GetMaxHP() : (bHasSnapshotData ? DisplayedSnapshot.MaxHP : 200.0f);
	const float CurKi = Attr ? Attr->GetKi() : (bHasSnapshotData ? DisplayedSnapshot.Energy : 200.0f);
	const float MaxKi = Attr ? Attr->GetMaxKi() : (bHasSnapshotData ? DisplayedSnapshot.MaxEnergy : 200.0f);
	const float Atk = Attr ? Attr->GetAttackPower() : (bHasSnapshotData ? DisplayedSnapshot.AttackPower : 100.0f);
	const float Def = Attr ? Attr->GetDefense() : (bHasSnapshotData ? DisplayedSnapshot.Defense : 20.0f);
	const float MitPct = (Def > 0.0f) ? (100.0f - (10000.0f / (100.0f + Def))) : 0.0f;

	FString CharName = (PS && !PS->GetPersistedCharacterName().IsEmpty()) ? PS->GetPersistedCharacterName() : TEXT("Chien Binh");
	const FString GuildPrefix = (PS && !PS->GetGuildTag().IsEmpty()) ? FString::Printf(TEXT("[%s] "), *PS->GetGuildTag()) : TEXT("");

	if (AvatarBadgeText)
	{
		AvatarBadgeText->SetText(FText::FromString(FString::Printf(TEXT("ZENITH\nLV.%d"), Lv)));
	}
	if (HeaderHPText)
	{
		HeaderHPText->SetText(FText::FromString(FString::Printf(TEXT("HP: %.0f / %.0f"), CurHP, MaxHP)));
	}
	if (HeaderKiText)
	{
		HeaderKiText->SetText(FText::FromString(FString::Printf(TEXT("KI: %.0f / %.0f"), CurKi, MaxKi)));
	}
	if (HeaderAtkText)
	{
		HeaderAtkText->SetText(FText::FromString(FString::Printf(TEXT("Suc danh: %.0f"), Atk)));
	}
	if (HeaderDefCritText)
	{
		HeaderDefCritText->SetText(FText::FromString(FString::Printf(TEXT("Giap: %.0f (-%.1f%% ST), Chi mang: 5%%"), Def, MitPct)));
	}

	if (PaperdollNameplateText)
	{
		PaperdollNameplateText->SetText(FText::FromString(FString::Printf(TEXT("[ %s%s - Chien Binh Trai Dat ]"), *GuildPrefix, *CharName)));
	}
	if (PaperdollAvatarGlyphText)
	{
		PaperdollAvatarGlyphText->SetText(FText::FromString(
			FString::Printf(TEXT("<<  [ CHIEN BINH LV.%d ]  >>\nTrang bi: %d/8  |  Hanh trang: %d/48"), Lv, EqCount, UsedBag)));
	}
	if (PaperdollSubStatsText)
	{
		PaperdollSubStatsText->SetText(FText::FromString(FString::Printf(
			TEXT("Suc manh: %lld / %lld (Lv.%d)\n")
			TEXT("Tiem nang: %lld  |  Giam ST Giap: %.1f%%\n")
			TEXT("Hut HP: 0%%, Hut KI: 0%%  |  Khang choang: Khong"),
			CurEXP, NextEXP, Lv, PoolTN, MitPct)));
	}

	// Tab 2: Ky Nang & Potential Stat Upgrade labels
	if (PotentialPoolBannerText)
	{
		PotentialPoolBannerText->SetText(FText::FromString(
			FString::Printf(TEXT("Diem Tiem Nang hien co: %lld TN  |  Suc Manh: %lld/%lld"), PoolTN, CurEXP, NextEXP)));
	}
	if (PotHPRowText)
	{
		PotHPRowText->SetText(FText::FromString(FString::Printf(TEXT("HP Goc + Trang bi: %.0f\n(Da nang %d lan: +%d HP)"), MaxHP, HPUpg, HPUpg * 100)));
	}
	if (PotKiRowText)
	{
		PotKiRowText->SetText(FText::FromString(FString::Printf(TEXT("KI Goc + Trang bi: %.0f\n(Da nang %d lan: +%d KI)"), MaxKi, KiUpg, KiUpg * 100)));
	}
	if (PotAtkRowText)
	{
		PotAtkRowText->SetText(FText::FromString(FString::Printf(TEXT("Suc Danh: %.0f\n(Da nang %d lan: +%d SD)"), Atk, AtkUpg, AtkUpg * 10)));
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
		UpgradeAtkBtnLabel->SetText(FText::FromString(FString::Printf(TEXT(" +10 SD (%lld TN) "), AtkCost)));
	}

	// Tab 0: Nhiem Vu fallback checklist when offline (preserved when PostgreSQL quests haven't been loaded)
	int32 MaxEnhSeen = 0;
	for (const FIU_InventoryItemView& Eq : DisplayedSnapshot.EquippedItems)
	{
		MaxEnhSeen = FMath::Max(MaxEnhSeen, Eq.EnhanceLevel);
	}
	for (const FIU_InventoryItemView& Bg : DisplayedSnapshot.BagItems)
	{
		MaxEnhSeen = FMath::Max(MaxEnhSeen, Bg.EnhanceLevel);
	}
	if (QuestChecklistText && (!bHasQuestSnapshotData || DisplayedQuestSnapshot.Quests.Num() == 0))
	{
		QuestChecklistText->SetText(FText::FromString(FString::Printf(
			TEXT("NHIEM VU TAN THU HANH TINH TRAI DAT:\n\n")
			TEXT("[%s] 1. Mo Hanh Trang [Tab] mac Ao Vo Phuc Tan Thu (%d/1)\n\n")
			TEXT("[%s] 2. Danh Raider nhan 1.5x Dame SM/TN & Vang CSPRNG\n\n")
			TEXT("[%s] 3. Vao tab [Ky Nang] dung Tiem Nang nang chi so (%d lan)\n\n")
			TEXT("[%s] 4. Cuong hoa Trang bi len +1 an toan (Max hien tai: +%d)"),
			(EqCount >= 1) ? TEXT("HOAN THANH") : TEXT("DANG LAM"), FMath::Min(1, EqCount),
			(Coin > 0 || PoolTN > 0 || Lv > 1) ? TEXT("HOAN THANH") : TEXT("DANG LAM"),
			(HPUpg + KiUpg + AtkUpg > 0) ? TEXT("HOAN THANH") : TEXT("DANG LAM"), HPUpg + KiUpg + AtkUpg,
			(MaxEnhSeen >= 1) ? TEXT("HOAN THANH") : TEXT("DANG LAM"), MaxEnhSeen)));
	}

	const int32 TitanTokens = bHasPityShopData ? DisplayedPityShopSnapshot.TokenBalance : (PS ? PS->GetPityTitanTokens() : 0);

	// Bottom Currency Footer Bar (Image 1 + Step 6 Huy Chuong Titan!)
	if (FooterGoldText)
	{
		FooterGoldText->SetText(FText::FromString(FString::Printf(TEXT("[VANG] %lld"), Coin)));
	}
	if (FooterPotentialText)
	{
		FooterPotentialText->SetText(FText::FromString(FString::Printf(TEXT("[TN] %lld"), PoolTN)));
	}
	if (FooterOreText)
	{
		FooterOreText->SetText(FText::FromString(FString::Printf(TEXT("[DA CH] %d"), Ore)));
	}
	if (FooterTokenText)
	{
		FooterTokenText->SetText(FText::FromString(FString::Printf(TEXT("[HUY CHUONG] %d"), TitanTokens)));
	}
}

void UIU_InventoryEquipmentWidget::RebuildPaperdollAndBagGrid()
{
	if (!WidgetTree || !LeftEquipColumnBox || !RightEquipColumnBox || !BagUniformGrid)
	{
		return;
	}

	LeftEquipColumnBox->ClearChildren();
	RightEquipColumnBox->ClearChildren();
	BagUniformGrid->ClearChildren();
	ActiveActionProxies.Reset();

	// Auto-select first available item if current selection no longer exists
	bool bSelectionStillValid = false;
	if (!SelectedItemInstanceID.IsEmpty())
	{
		for (const FIU_InventoryItemView& Eq : DisplayedSnapshot.EquippedItems)
		{
			if (Eq.InstanceID == SelectedItemInstanceID) { bSelectionStillValid = true; break; }
		}
		if (!bSelectionStillValid)
		{
			for (const FIU_InventoryItemView& Bg : DisplayedSnapshot.BagItems)
			{
				if (Bg.InstanceID == SelectedItemInstanceID) { bSelectionStillValid = true; break; }
			}
		}
	}
	if (!bSelectionStillValid)
	{
		if (DisplayedSnapshot.EquippedItems.Num() > 0)
		{
			SelectedItemInstanceID = DisplayedSnapshot.EquippedItems[0].InstanceID;
			SelectedEquippedSlotID = DisplayedSnapshot.EquippedItems[0].EquippedSlotID;
			SelectedBagSlotIndex = -1;
		}
		else if (DisplayedSnapshot.BagItems.Num() > 0)
		{
			SelectedItemInstanceID = DisplayedSnapshot.BagItems[0].InstanceID;
			SelectedBagSlotIndex = DisplayedSnapshot.BagItems[0].BagSlotIndex;
			SelectedEquippedSlotID.Empty();
		}
		else
		{
			SelectedItemInstanceID.Empty();
			SelectedEquippedSlotID.Empty();
			SelectedBagSlotIndex = -1;
		}
	}

	// 1. Build 4 Left + 4 Right Square Paperdoll Slots
	for (const FIU_PaperdollSlotSpec& Spec : GPaperdollSlots)
	{
		const FIU_InventoryItemView* FoundEq = nullptr;
		for (const FIU_InventoryItemView& Eq : DisplayedSnapshot.EquippedItems)
		{
			if (Eq.EquippedSlotID.Equals(Spec.SlotID, ESearchCase::IgnoreCase))
			{
				FoundEq = &Eq;
				break;
			}
		}

		USizeBox* SlotSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		SlotSize->SetWidthOverride(44.0f);
		SlotSize->SetHeightOverride(40.0f);

		const bool bIsSelectedSlot = FoundEq && (FoundEq->InstanceID == SelectedItemInstanceID);
		UBorder* SlotFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		SlotFrame->SetBrush(bIsSelectedSlot ? BagCellSelectedBrush : (FoundEq ? EquipOccupiedSlotBrush : EquipEmptySlotBrush));
		SlotFrame->SetPadding(FMargin(2.5f));
		SlotSize->SetContent(SlotFrame);

		UBorder* InnerFill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		InnerFill->SetBrushColor(FoundEq ? GetShortItemBgColor(*FoundEq) : FLinearColor(0.56f, 0.46f, 0.34f, 0.70f));
		SlotFrame->SetContent(InnerFill);

		UCanvasPanel* CellCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		InnerFill->SetContent(CellCanvas);

		UTextBlock* CenterGlyph = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		CenterGlyph->SetText(FText::FromString(FoundEq ? GetShortItemIconGlyph(*FoundEq) : Spec.EmptyGlyph));
		CenterGlyph->SetJustification(ETextJustify::Center);
		StyleNROText(
			CenterGlyph,
			8,
			FoundEq ? GetRarityTintColor(FoundEq->Rarity) : FLinearColor(0.86f, 0.80f, 0.72f, 0.65f));
		if (UCanvasPanelSlot* GS = CellCanvas->AddChildToCanvas(CenterGlyph))
		{
			GS->SetAnchors(FAnchors(0.5f, 0.5f));
			GS->SetAlignment(FVector2D(0.5f, 0.5f));
			GS->SetAutoSize(true);
		}

		if (FoundEq && FoundEq->EnhanceLevel > 0)
		{
			UTextBlock* EnhBadge = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			EnhBadge->SetText(FText::FromString(FString::Printf(TEXT("+%d"), FoundEq->EnhanceLevel)));
			StyleNROText(EnhBadge, 8, FLinearColor(1.0f, 0.92f, 0.18f));
			if (UCanvasPanelSlot* ES = CellCanvas->AddChildToCanvas(EnhBadge))
			{
				ES->SetAnchors(FAnchors(1.0f, 0.0f));
				ES->SetAlignment(FVector2D(1.0f, 0.0f));
				ES->SetPosition(FVector2D(-1.0f, 0.0f));
				ES->SetAutoSize(true);
			}
		}

		if (FoundEq)
		{
			UButton* ClickBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
			ClickBtn->SetBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
			UIU_InventoryActionProxy* Proxy = NewObject<UIU_InventoryActionProxy>(this);
			Proxy->OwnerWidget = this;
			Proxy->Action = EIU_InvRowAction::SelectEquippedSlot;
			Proxy->ItemInstanceID = FoundEq->InstanceID;
			Proxy->SlotID = Spec.SlotID;
			ActiveActionProxies.Add(Proxy);
			ClickBtn->OnClicked.AddDynamic(Proxy, &UIU_InventoryActionProxy::HandleButtonClicked);
			if (UCanvasPanelSlot* BS = CellCanvas->AddChildToCanvas(ClickBtn))
			{
				BS->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
				BS->SetOffsets(FMargin(0.0f));
			}
		}

		UVerticalBox* TargetCol = Spec.bLeftColumn ? LeftEquipColumnBox : RightEquipColumnBox;
		if (UVerticalBoxSlot* CS = TargetCol->AddChildToVerticalBox(SlotSize))
		{
			CS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 3.0f));
		}
	}

	// 2. Build All 48 Square Bag Grid Cells (6 columns x 8 rows = 48 cells, matching Image 1!)
	TMap<int32, const FIU_InventoryItemView*> BagBySlot;
	int32 FallbackIdx = 0;
	for (const FIU_InventoryItemView& Item : DisplayedSnapshot.BagItems)
	{
		const int32 SlotIdx = (Item.BagSlotIndex >= 0 && Item.BagSlotIndex < 48) ? Item.BagSlotIndex : FallbackIdx++;
		BagBySlot.Add(SlotIdx, &Item);
	}

	const int32 GridCols = 6;
	for (int32 CellIdx = 0; CellIdx < 48; ++CellIdx)
	{
		const int32 Row = CellIdx / GridCols;
		const int32 Col = CellIdx % GridCols;
		const FIU_InventoryItemView* const* FoundPtr = BagBySlot.Find(CellIdx);
		const FIU_InventoryItemView* CellItem = FoundPtr ? *FoundPtr : nullptr;

		USizeBox* CellSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		CellSize->SetWidthOverride(64.0f);
		CellSize->SetHeightOverride(48.0f);

		const bool bIsSelected = CellItem && (CellItem->InstanceID == SelectedItemInstanceID);
		UBorder* CellBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		CellBorder->SetBrush(bIsSelected ? BagCellSelectedBrush : BagCellNormalBrush);
		CellBorder->SetPadding(FMargin(2.5f));
		CellSize->SetContent(CellBorder);

		if (CellItem)
		{
			UBorder* IconTile = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			IconTile->SetBrushColor(GetShortItemBgColor(*CellItem));
			CellBorder->SetContent(IconTile);

			UCanvasPanel* TileCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
			IconTile->SetContent(TileCanvas);

			UTextBlock* GlyphTxt = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			GlyphTxt->SetText(FText::FromString(GetShortItemIconGlyph(*CellItem)));
			GlyphTxt->SetJustification(ETextJustify::Center);
			StyleNROText(GlyphTxt, 9, GetRarityTintColor(CellItem->Rarity));
			if (UCanvasPanelSlot* GS = TileCanvas->AddChildToCanvas(GlyphTxt))
			{
				GS->SetAnchors(FAnchors(0.5f, 0.45f));
				GS->SetAlignment(FVector2D(0.5f, 0.5f));
				GS->SetAutoSize(true);
			}

			// Top-left +Enhance or Lock badge
			if (CellItem->EnhanceLevel > 0 || CellItem->bIsPlayerLocked)
			{
				UTextBlock* TopBadge = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
				const FString BadgeStr = (CellItem->EnhanceLevel > 0)
					? FString::Printf(TEXT("+%d%s"), CellItem->EnhanceLevel, CellItem->bIsPlayerLocked ? TEXT("K") : TEXT(""))
					: TEXT("[K]");
				TopBadge->SetText(FText::FromString(BadgeStr));
				StyleNROText(TopBadge, 7, FLinearColor(0.55f, 1.0f, 0.72f));
				if (UCanvasPanelSlot* TBS = TileCanvas->AddChildToCanvas(TopBadge))
				{
					TBS->SetAnchors(FAnchors(0.0f, 0.0f));
					TBS->SetPosition(FVector2D(2.0f, 1.0f));
					TBS->SetAutoSize(true);
				}
			}

			// Bottom-right bold yellow stack count (exact Image 1 look: 462, 366, 90, 82!)
			if (CellItem->StackCount > 1 || CellItem->MaxStack > 1)
			{
				UTextBlock* StackTxt = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
				StackTxt->SetText(FText::FromString(FString::Printf(TEXT("%d"), CellItem->StackCount)));
				StyleNROText(StackTxt, 9, FLinearColor(1.0f, 0.92f, 0.16f));
				if (UCanvasPanelSlot* SS = TileCanvas->AddChildToCanvas(StackTxt))
				{
					SS->SetAnchors(FAnchors(1.0f, 1.0f));
					SS->SetAlignment(FVector2D(1.0f, 1.0f));
					SS->SetPosition(FVector2D(-2.0f, -1.0f));
					SS->SetAutoSize(true);
				}
			}

			UButton* CellBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
			CellBtn->SetBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
			UIU_InventoryActionProxy* Proxy = NewObject<UIU_InventoryActionProxy>(this);
			Proxy->OwnerWidget = this;
			Proxy->Action = EIU_InvRowAction::SelectBagCell;
			Proxy->ItemInstanceID = CellItem->InstanceID;
			Proxy->BagSlotIndex = CellIdx;
			ActiveActionProxies.Add(Proxy);
			CellBtn->OnClicked.AddDynamic(Proxy, &UIU_InventoryActionProxy::HandleButtonClicked);
			if (UCanvasPanelSlot* BS = TileCanvas->AddChildToCanvas(CellBtn))
			{
				BS->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
				BS->SetOffsets(FMargin(0.0f));
			}
		}

		if (UUniformGridSlot* GridSlot = BagUniformGrid->AddChildToUniformGrid(CellSize, Row, Col))
		{
			GridSlot->SetHorizontalAlignment(HAlign_Center);
			GridSlot->SetVerticalAlignment(VAlign_Center);
		}
	}

	RebuildSelectedInspectorBar();
}

void UIU_InventoryEquipmentWidget::RebuildSelectedInspectorBar()
{
	if (!WidgetTree || !SelectedItemTitleText || !SelectedItemActionsBox)
	{
		return;
	}

	SelectedItemActionsBox->ClearChildren();

	const FIU_InventoryItemView* SelectedItem = nullptr;
	bool bSelectedIsEquipped = false;

	for (const FIU_InventoryItemView& Eq : DisplayedSnapshot.EquippedItems)
	{
		if (Eq.InstanceID == SelectedItemInstanceID)
		{
			SelectedItem = &Eq;
			bSelectedIsEquipped = true;
			break;
		}
	}
	if (!SelectedItem)
	{
		for (const FIU_InventoryItemView& Bg : DisplayedSnapshot.BagItems)
		{
			if (Bg.InstanceID == SelectedItemInstanceID)
			{
				SelectedItem = &Bg;
				bSelectedIsEquipped = false;
				break;
			}
		}
	}

	if (!SelectedItem)
	{
		SelectedItemTitleText->SetText(FText::FromString(TEXT("Chon 1 vat pham trong Paperdoll hoac O Hanh Trang ben duoi de xem chi tiet!")));
		return;
	}

	const FString EnhTag = (SelectedItem->EnhanceLevel > 0) ? FString::Printf(TEXT(" +%d"), SelectedItem->EnhanceLevel) : TEXT("");
	const FString StackTag = (SelectedItem->StackCount > 1) ? FString::Printf(TEXT(" (x%d)"), SelectedItem->StackCount) : TEXT("");
	const FString LockTag = SelectedItem->bIsPlayerLocked ? TEXT(" [DA KHOA]") : TEXT("");
	const FString StatPart = SelectedItem->StatSummaryText.IsEmpty() ? TEXT("") : FString::Printf(TEXT(" | %s"), *SelectedItem->StatSummaryText);

	SelectedItemTitleText->SetText(FText::FromString(FString::Printf(
		TEXT("%s%s%s%s (YC Lv.%d)%s"),
		*SelectedItem->DisplayName, *EnhTag, *StackTag, *LockTag, SelectedItem->RequiredLevel, *StatPart)));
	SelectedItemTitleText->SetColorAndOpacity(FSlateColor(GetRarityTintColor(SelectedItem->Rarity)));

	auto AddInspectorActionBtn = [&](const FString& Label, const FLinearColor& BgCol, EIU_InvRowAction Act, const FString& SlotID, bool bLockState = false)
	{
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		Btn->SetBackgroundColor(BgCol);
		UTextBlock* Lbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Lbl->SetText(FText::FromString(Label));
		StyleNROText(Lbl, 9, FLinearColor(1.0f, 0.98f, 0.88f));
		Btn->SetContent(Lbl);

		UIU_InventoryActionProxy* Proxy = NewObject<UIU_InventoryActionProxy>(this);
		Proxy->OwnerWidget = this;
		Proxy->Action = Act;
		Proxy->ItemInstanceID = SelectedItem->InstanceID;
		Proxy->SlotID = SlotID;
		Proxy->bTargetLockState = bLockState;
		ActiveActionProxies.Add(Proxy);
		Btn->OnClicked.AddDynamic(Proxy, &UIU_InventoryActionProxy::HandleButtonClicked);

		if (UHorizontalBoxSlot* BS = SelectedItemActionsBox->AddChildToHorizontalBox(Btn))
		{
			BS->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
		}
	};

	if (bSelectedIsEquipped)
	{
		AddInspectorActionBtn(TEXT(" THAO RA "), FLinearColor(0.62f, 0.24f, 0.14f, 1.0f), EIU_InvRowAction::Unequip, SelectedItem->EquippedSlotID);
		if (SelectedItem->MaxEnhanceLevel > 0 && SelectedItem->EnhanceLevel < SelectedItem->MaxEnhanceLevel)
		{
			AddInspectorActionBtn(
				FString::Printf(TEXT(" +CH +%d (%lldV) "), SelectedItem->EnhanceLevel + 1, SelectedItem->NextEnhanceCoin),
				FLinearColor(0.76f, 0.48f, 0.08f, 1.0f),
				EIU_InvRowAction::Enhance,
				SelectedItem->EquippedSlotID);
		}
	}
	else
	{
		if (SelectedItem->Category.Equals(TEXT("EQUIPMENT"), ESearchCase::IgnoreCase))
		{
			AddInspectorActionBtn(TEXT(" MAC TRANG BI "), FLinearColor(0.16f, 0.56f, 0.24f, 1.0f), EIU_InvRowAction::Equip, SelectedItem->EquipSlot);
			if (SelectedItem->MaxEnhanceLevel > 0 && SelectedItem->EnhanceLevel < SelectedItem->MaxEnhanceLevel)
			{
				AddInspectorActionBtn(
					FString::Printf(TEXT(" +CH +%d (%lldV) "), SelectedItem->EnhanceLevel + 1, SelectedItem->NextEnhanceCoin),
					FLinearColor(0.76f, 0.48f, 0.08f, 1.0f),
					EIU_InvRowAction::Enhance,
					TEXT(""));
			}
		}
		else if (SelectedItem->Category.Equals(TEXT("CONSUMABLE"), ESearchCase::IgnoreCase))
		{
			AddInspectorActionBtn(TEXT(" SU DUNG "), FLinearColor(0.16f, 0.58f, 0.28f, 1.0f), EIU_InvRowAction::UseConsumable, TEXT(""));
		}

		AddInspectorActionBtn(
			SelectedItem->bIsPlayerLocked ? TEXT(" MO KHOA ") : TEXT(" KHOA "),
			SelectedItem->bIsPlayerLocked ? FLinearColor(0.58f, 0.38f, 0.12f, 1.0f) : FLinearColor(0.24f, 0.34f, 0.48f, 1.0f),
			EIU_InvRowAction::ToggleLock,
			TEXT(""),
			!SelectedItem->bIsPlayerLocked);

		if (SelectedItem->SellPriceCoin > 0 && !SelectedItem->bIsPlayerLocked)
		{
			AddInspectorActionBtn(
				FString::Printf(TEXT(" BAN (+%lldV) "), SelectedItem->SellPriceCoin),
				FLinearColor(0.56f, 0.20f, 0.14f, 1.0f),
				EIU_InvRowAction::SellOne,
				TEXT(""));
		}
	}
}

void UIU_InventoryEquipmentWidget::RequestPotentialStatUpgrade(EIU_PotentialStatType StatType)
{
	AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer());
	if (!IUPC)
	{
		return;
	}

	if (const AIU_PlayerState* PS = BoundPS.Get())
	{
		const int64 Needed = (StatType == EIU_PotentialStatType::MaxHP) ? PS->GetNextHPCost()
			: (StatType == EIU_PotentialStatType::MaxKi) ? PS->GetNextKiCost()
			: PS->GetNextAtkCost();
		if (PS->GetPotentialPoints() < Needed)
		{
			SetFeedbackMessage(
				FString::Printf(TEXT("Khong du Tiem Nang! Can %lld TN (Hien co: %lld TN)"), Needed, PS->GetPotentialPoints()),
				true);
			return;
		}
	}

	SetFeedbackMessage(TEXT("Dang nang chi so bang Diem Tiem Nang..."));
	IUPC->Server_RequestAllocatePotential(StatType);
}

void UIU_InventoryEquipmentWidget::HandleTabNhiemVuClicked()   { SetActiveProfileTab(EIU_CharacterProfileTab::NhiemVu); }
void UIU_InventoryEquipmentWidget::HandleTabHanhTrangClicked() { SetActiveProfileTab(EIU_CharacterProfileTab::HanhTrang); }
void UIU_InventoryEquipmentWidget::HandleTabKyNangClicked()    { SetActiveProfileTab(EIU_CharacterProfileTab::KyNang); }
void UIU_InventoryEquipmentWidget::HandleTabBangHoiClicked()   { SetActiveProfileTab(EIU_CharacterProfileTab::BangHoi); }
void UIU_InventoryEquipmentWidget::HandleTabChucNangClicked()  { SetActiveProfileTab(EIU_CharacterProfileTab::ChucNang); }

void UIU_InventoryEquipmentWidget::HandleUpgradeHPClicked()  { RequestPotentialStatUpgrade(EIU_PotentialStatType::MaxHP); }
void UIU_InventoryEquipmentWidget::HandleUpgradeKiClicked()  { RequestPotentialStatUpgrade(EIU_PotentialStatType::MaxKi); }
void UIU_InventoryEquipmentWidget::HandleUpgradeAtkClicked() { RequestPotentialStatUpgrade(EIU_PotentialStatType::AttackPower); }

void UIU_InventoryEquipmentWidget::HandleFuncToggleDebugClicked()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (AIU_HUDManager* HUD = Cast<AIU_HUDManager>(PC->GetHUD()))
		{
			HUD->ToggleDebugTelemetry();
		}
	}
}

void UIU_InventoryEquipmentWidget::HandleFuncToggleRotClicked()
{
	if (AIU_PlayerCharacter* PChar = Cast<AIU_PlayerCharacter>(GetOwningPlayerPawn()))
	{
		PChar->ToggleRotationModeInput();
		SetFeedbackMessage(TEXT("Da chuyen doi che do xoay Camera!"));
	}
}

void UIU_InventoryEquipmentWidget::HandleFuncTogglePvPClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		IUPC->Server_ToggleGlobalPvPSparring();
		SetFeedbackMessage(TEXT("Da gui yeu cau bat/tat PvP Sparring!"));
	}
}

void UIU_InventoryEquipmentWidget::HandleFuncAuthPanelClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		IUPC->SetInventoryPanelVisible(false, false);
		IUPC->OpenLoginCharacterModal();
	}
}

void UIU_InventoryEquipmentWidget::HandleRefreshClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		SetFeedbackMessage(TEXT("Dang dong bo Ho So & Hanh Trang tu Go Backend..."));
		IUPC->Server_RequestRefreshInventory();
	}
}

void UIU_InventoryEquipmentWidget::HandleRefreshQuestsClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		SetFeedbackMessage(TEXT("Dang tai danh sach Nhiem Vu tu PostgreSQL 18..."));
		IUPC->Server_RequestRefreshQuests();
	}
}

void UIU_InventoryEquipmentWidget::HandleFuncSubModeChannelsClicked()
{
	SetActiveFuncSubMode(0);
}

void UIU_InventoryEquipmentWidget::HandleFuncSubModePityShopClicked()
{
	SetActiveFuncSubMode(1);
}

void UIU_InventoryEquipmentWidget::HandleFuncSubModeSettingsClicked()
{
	SetActiveFuncSubMode(2);
}

void UIU_InventoryEquipmentWidget::HandleRefreshChannelsClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		SetFeedbackMessage(TEXT("Dang cap nhat danh sach Khu / Vung truc tuyen..."));
		IUPC->Server_RequestRefreshChannels();
	}
}

void UIU_InventoryEquipmentWidget::HandleCloseClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		IUPC->SetInventoryPanelVisible(false, true);
	}
	else
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UIU_InventoryEquipmentWidget::HandleAnyVitalOrEconomyChanged(float, float, float) { UpdateAllReadouts(); }
void UIU_InventoryEquipmentWidget::HandleAttackOrDefenseChanged(float)                 { UpdateAllReadouts(); }
void UIU_InventoryEquipmentWidget::HandleCharacterEXPChanged(int64, int64, int32)      { UpdateAllReadouts(); }
void UIU_InventoryEquipmentWidget::HandleCharacterPotentialChanged(int64, int64, int64, int64)
{
	SetFeedbackMessage(TEXT("Da cap nhat Tiem Nang & Chi So thanh cong!"));
	UpdateAllReadouts();
}
void UIU_InventoryEquipmentWidget::HandleEconomySummaryChanged(int64, int32, int32, int32)
{
	UpdateAllReadouts();
}
