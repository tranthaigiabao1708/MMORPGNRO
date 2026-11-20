// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "UI/IU_InventoryEquipmentWidget.h"
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
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/Pawn.h"

void UIU_InventoryActionProxy::HandleButtonClicked()
{
	if (UIU_InventoryEquipmentWidget* W = OwnerWidget.Get())
	{
		W->ExecuteRowAction(Action, ItemInstanceID, SlotID, bTargetLockState);
	}
}

namespace
{
	struct FIU_EquipSlotLabelDef
	{
		const TCHAR* SlotID;
		const TCHAR* ShortLabel;
	};

	static const FIU_EquipSlotLabelDef GCanonicalEquipSlots[8] = {
		{ TEXT("slot.head"),    TEXT("1. Dau (Head)") },
		{ TEXT("slot.body"),    TEXT("2. Giap Than") },
		{ TEXT("slot.legs"),    TEXT("3. Quan Giap") },
		{ TEXT("slot.hands"),   TEXT("4. Gang Tay") },
		{ TEXT("slot.feet"),    TEXT("5. Giay Chien") },
		{ TEXT("slot.acc_1"),   TEXT("6. Phu Kien 1") },
		{ TEXT("slot.acc_2"),   TEXT("7. May Do/PK2") },
		{ TEXT("slot.special"), TEXT("8. Vu Khi/DB") }
	};

	static FLinearColor RarityToSlateColor(const FString& Rarity)
	{
		if (Rarity.Equals(TEXT("MYTHIC"), ESearchCase::IgnoreCase)) { return FLinearColor(1.0f, 0.35f, 0.30f); }
		if (Rarity.Equals(TEXT("LEGENDARY"), ESearchCase::IgnoreCase)) { return FLinearColor(1.0f, 0.78f, 0.22f); }
		if (Rarity.Equals(TEXT("EPIC"), ESearchCase::IgnoreCase)) { return FLinearColor(0.82f, 0.45f, 1.0f); }
		if (Rarity.Equals(TEXT("RARE"), ESearchCase::IgnoreCase)) { return FLinearColor(0.35f, 0.78f, 1.0f); }
		if (Rarity.Equals(TEXT("UNCOMMON"), ESearchCase::IgnoreCase)) { return FLinearColor(0.42f, 1.0f, 0.58f); }
		return FLinearColor(0.92f, 0.95f, 0.98f);
	}

	static void StyleInvText(UTextBlock* TB, int32 SizePt, const FLinearColor& Color)
	{
		if (!TB)
		{
			return;
		}
		FSlateFontInfo F = TB->GetFont();
		F.Size = SizePt;
		TB->SetFont(F);
		TB->SetColorAndOpacity(FSlateColor(Color));
		TB->SetShadowOffset(FVector2D(1.1f, 1.1f));
		TB->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.90f));
	}
}

TSharedRef<SWidget> UIU_InventoryEquipmentWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("InvRootCanvas"));
		WidgetTree->RootWidget = RootCanvas;

		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InvModalCard"));
		Card->SetBrushColor(FLinearColor(0.02f, 0.04f, 0.09f, 0.96f));
		Card->SetPadding(FMargin(16.0f, 12.0f));
		if (UCanvasPanelSlot* CardSlot = RootCanvas->AddChildToCanvas(Card))
		{
			CardSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CardSlot->SetPosition(FVector2D(0.0f, 8.0f));
			CardSlot->SetAutoSize(true);
		}

		UVerticalBox* MainVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("InvMainVBox"));
		Card->SetContent(MainVBox);

		UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("InvTitle"));
		Title->SetText(FText::FromString(TEXT("TUI DO CA NHAN (48 O) & TRANG BI 8 O  |  CUONG HOA AN TOAN (+1..+10)  [PHIM I / B]")));
		StyleInvText(Title, 13, FLinearColor(1.0f, 0.88f, 0.28f));
		if (UVerticalBoxSlot* S = MainVBox->AddChildToVerticalBox(Title))
		{
			S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 3.0f));
		}

		WalletAndBagSummaryText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("WalletAndBagSummaryText"));
		StyleInvText(WalletAndBagSummaryText, 11, FLinearColor(0.35f, 1.0f, 0.72f));
		if (UVerticalBoxSlot* S = MainVBox->AddChildToVerticalBox(WalletAndBagSummaryText))
		{
			S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
		}

		CompiledStatsSummaryText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CompiledStatsSummaryText"));
		StyleInvText(CompiledStatsSummaryText, 11, FLinearColor(0.78f, 0.92f, 1.0f));
		if (UVerticalBoxSlot* S = MainVBox->AddChildToVerticalBox(CompiledStatsSummaryText))
		{
			S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		}

		// Two-column body: Left = 8 Equipment Slots, Right = 48-Slot Scrollable Bag
		UHorizontalBox* ColumnsRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("InvColumnsRow"));
		if (UVerticalBoxSlot* ColSlot = MainVBox->AddChildToVerticalBox(ColumnsRow))
		{
			ColSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		}

		// Left Column (8 Equipment Slots)
		USizeBox* LeftSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LeftEquipSizeBox"));
		LeftSizeBox->SetMinDesiredWidth(355.0f);
		UBorder* LeftBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LeftEquipBorder"));
		LeftBorder->SetBrushColor(FLinearColor(0.04f, 0.07f, 0.14f, 0.90f));
		LeftBorder->SetPadding(FMargin(8.0f, 6.0f));
		LeftSizeBox->SetContent(LeftBorder);

		UVerticalBox* LeftColVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LeftColVBox"));
		LeftBorder->SetContent(LeftColVBox);

		UTextBlock* LeftHdr = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LeftEquipHdr"));
		LeftHdr->SetText(FText::FromString(TEXT("TRANG BI DANG MAC (8 O CHUAN):")));
		StyleInvText(LeftHdr, 11, FLinearColor(1.0f, 0.82f, 0.36f));
		if (UVerticalBoxSlot* LHS = LeftColVBox->AddChildToVerticalBox(LeftHdr))
		{
			LHS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		}

		EquippedSlotsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("EquippedSlotsBox"));
		LeftColVBox->AddChildToVerticalBox(EquippedSlotsBox);

		if (UHorizontalBoxSlot* LeftHSlot = ColumnsRow->AddChildToHorizontalBox(LeftSizeBox))
		{
			LeftHSlot->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
			LeftHSlot->SetVerticalAlignment(VAlign_Top);
		}

		// Right Column (48-Slot Bag ScrollBox)
		USizeBox* RightSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RightBagSizeBox"));
		RightSizeBox->SetMinDesiredWidth(450.0f);
		UBorder* RightBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RightBagBorder"));
		RightBorder->SetBrushColor(FLinearColor(0.04f, 0.07f, 0.14f, 0.90f));
		RightBorder->SetPadding(FMargin(8.0f, 6.0f));
		RightSizeBox->SetContent(RightBorder);

		UVerticalBox* RightColVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RightColVBox"));
		RightBorder->SetContent(RightColVBox);

		UTextBlock* RightHdr = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RightBagHdr"));
		RightHdr->SetText(FText::FromString(TEXT("HANH TRANG 48 O (MAC / DUNG / +CH / KHOA / BAN):")));
		StyleInvText(RightHdr, 11, FLinearColor(0.48f, 0.92f, 1.0f));
		if (UVerticalBoxSlot* RHS = RightColVBox->AddChildToVerticalBox(RightHdr))
		{
			RHS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		}

		USizeBox* ScrollHeightCap = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BagScrollHeightCap"));
		ScrollHeightCap->SetMinDesiredHeight(195.0f);
		ScrollHeightCap->SetMaxDesiredHeight(225.0f);
		BagItemsScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("BagItemsScrollBox"));
		ScrollHeightCap->SetContent(BagItemsScrollBox);
		RightColVBox->AddChildToVerticalBox(ScrollHeightCap);

		if (UHorizontalBoxSlot* RightHSlot = ColumnsRow->AddChildToHorizontalBox(RightSizeBox))
		{
			RightHSlot->SetVerticalAlignment(VAlign_Top);
		}

		// Footer Row
		UHorizontalBox* FooterRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("InvFooterRow"));
		MainVBox->AddChildToVerticalBox(FooterRow);

		USizeBox* FeedbackBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("InvFeedbackSizeBox"));
		FeedbackBox->SetMinDesiredWidth(465.0f);
		FeedbackStatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("InvFeedbackText"));
		FeedbackStatusText->SetText(FText::FromString(TEXT("San sang: Click [MAC], [+CH], [KHOA], [BAN] hoac bam [4] dung Dau Than!")));
		StyleInvText(FeedbackStatusText, 10, FLinearColor(0.85f, 0.94f, 1.0f));
		FeedbackBox->SetContent(FeedbackStatusText);
		if (UHorizontalBoxSlot* FBS = FooterRow->AddChildToHorizontalBox(FeedbackBox))
		{
			FBS->SetVerticalAlignment(VAlign_Center);
			FBS->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
		}

		auto MakeFooterButton = [&](const TCHAR* Name, const TCHAR* Label, const FLinearColor& BgCol, TObjectPtr<UButton>& OutBtn)
		{
			OutBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
			OutBtn->SetBackgroundColor(BgCol);
			UTextBlock* Lbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Lbl->SetText(FText::FromString(Label));
			StyleInvText(Lbl, 10, FLinearColor(1.0f, 0.96f, 0.86f));
			OutBtn->SetContent(Lbl);
			if (UHorizontalBoxSlot* BS = FooterRow->AddChildToHorizontalBox(OutBtn))
			{
				BS->SetVerticalAlignment(VAlign_Center);
				BS->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
			}
		};

		MakeFooterButton(TEXT("QuickElixirBtn"), TEXT(" [4] DAU THAN (+150) "), FLinearColor(0.10f, 0.38f, 0.22f, 1.0f), QuickElixirButton);
		MakeFooterButton(TEXT("RefreshInvBtn"), TEXT(" LAM MOI "), FLinearColor(0.12f, 0.26f, 0.44f, 1.0f), RefreshButton);
		MakeFooterButton(TEXT("CloseInvBtn"), TEXT(" DONG (I) "), FLinearColor(0.32f, 0.14f, 0.16f, 1.0f), ClosePanelButton);

		UpdateHeaderSummaryLabels();
		RebuildDynamicRows();
	}

	return Super::RebuildWidget();
}

void UIU_InventoryEquipmentWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (QuickElixirButton)
	{
		QuickElixirButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleQuickUseElixirClicked);
	}
	if (RefreshButton)
	{
		RefreshButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleRefreshClicked);
	}
	if (ClosePanelButton)
	{
		ClosePanelButton->OnClicked.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleCloseClicked);
	}

	RefreshBindingsAndRebuild();
}

void UIU_InventoryEquipmentWidget::NativeDestruct()
{
	UnbindAll();
	ActiveActionProxies.Empty();
	Super::NativeDestruct();
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
		PS->OnCharacterEconomyChanged.AddUniqueDynamic(this, &UIU_InventoryEquipmentWidget::HandleEconomySummaryChanged);
	}

	if (const AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		if (IUPC->HasCachedInventorySnapshot())
		{
			DisplayedSnapshot = IUPC->GetCachedInventorySnapshot();
			bHasSnapshotData = true;
		}
	}

	UpdateHeaderSummaryLabels();
	RebuildDynamicRows();
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

	UpdateHeaderSummaryLabels();
	RebuildDynamicRows();
}

void UIU_InventoryEquipmentWidget::SetFeedbackMessage(const FString& Message, bool bIsError)
{
	if (FeedbackStatusText)
	{
		FeedbackStatusText->SetText(FText::FromString(Message));
		FeedbackStatusText->SetColorAndOpacity(FSlateColor(
			bIsError ? FLinearColor(1.0f, 0.42f, 0.32f) : FLinearColor(0.42f, 1.0f, 0.68f)));
	}
}

void UIU_InventoryEquipmentWidget::ExecuteRowAction(
	EIU_InvRowAction Action,
	const FString& ItemInstanceID,
	const FString& SlotID,
	bool bTargetLockState)
{
	AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer());
	if (!IUPC)
	{
		return;
	}

	switch (Action)
	{
	case EIU_InvRowAction::Equip:
		SetFeedbackMessage(TEXT("Dang mac trang bi..."));
		IUPC->Server_RequestEquipItem(ItemInstanceID);
		break;
	case EIU_InvRowAction::Unequip:
		SetFeedbackMessage(TEXT("Dang thao trang bi ve tui do..."));
		IUPC->Server_RequestUnequipSlot(SlotID);
		break;
	case EIU_InvRowAction::UseConsumable:
		SetFeedbackMessage(TEXT("Dang su dung vat pham hoi phuc..."));
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
		SetFeedbackMessage(TEXT("Dang ban vat pham nhan Vang (Currency Ledger)..."));
		IUPC->Server_RequestSellItem(ItemInstanceID, 1);
		break;
	}
}

void UIU_InventoryEquipmentWidget::UpdateHeaderSummaryLabels()
{
	const UIU_AttributeComponent* Attr = BoundAttr.Get();
	const AIU_PlayerState* PS = BoundPS.Get();

	const int64 Coin = bHasSnapshotData ? DisplayedSnapshot.StandardCoin : (PS ? PS->GetStandardCoin() : 0);
	const int32 Elixirs = bHasSnapshotData ? DisplayedSnapshot.ElixirCount : (PS ? PS->GetElixirCount() : 0);
	const int32 Ore = bHasSnapshotData ? DisplayedSnapshot.CrimsonOreCount : (PS ? PS->GetCrimsonOreCount() : 0);
	const int32 UsedSlots = bHasSnapshotData ? DisplayedSnapshot.UsedBagSlots : 0;
	const int32 MaxSlots = bHasSnapshotData ? FMath::Max(48, DisplayedSnapshot.MaxBagSlots) : 48;

	const float CurHP = Attr ? Attr->GetHP() : (bHasSnapshotData ? DisplayedSnapshot.HP : 200.0f);
	const float MaxHP = Attr ? Attr->GetMaxHP() : (bHasSnapshotData ? DisplayedSnapshot.MaxHP : 200.0f);
	const float CurKi = Attr ? Attr->GetKi() : (bHasSnapshotData ? DisplayedSnapshot.Energy : 200.0f);
	const float MaxKi = Attr ? Attr->GetMaxKi() : (bHasSnapshotData ? DisplayedSnapshot.MaxEnergy : 200.0f);
	const float Atk = Attr ? Attr->GetAttackPower() : (bHasSnapshotData ? DisplayedSnapshot.AttackPower : 100.0f);
	const float Def = Attr ? Attr->GetDefense() : (bHasSnapshotData ? DisplayedSnapshot.Defense : 20.0f);
	const float MitPct = (Def > 0.0f) ? (100.0f - (10000.0f / (100.0f + Def))) : 0.0f;

	if (WalletAndBagSummaryText)
	{
		WalletAndBagSummaryText->SetText(FText::FromString(FString::Printf(
			TEXT("Vang: %lld  |  Dau Than [Phim 4]: %d  |  Da Cuong Hoa Do: %d  |  Tui Do: %d / %d o"),
			Coin, Elixirs, Ore, UsedSlots, MaxSlots)));
	}

	if (CompiledStatsSummaryText)
	{
		CompiledStatsSummaryText->SetText(FText::FromString(FString::Printf(
			TEXT("Chi So Tong (Tiem Nang + Trang Bi):  HP %.0f/%.0f  |  KI %.0f/%.0f  |  Suc Danh %.0f  |  Giap %.0f (-%.1f%% ST)"),
			CurHP, MaxHP, CurKi, MaxKi, Atk, Def, MitPct)));
	}
}

void UIU_InventoryEquipmentWidget::RebuildDynamicRows()
{
	if (!WidgetTree || !EquippedSlotsBox || !BagItemsScrollBox)
	{
		return;
	}

	EquippedSlotsBox->ClearChildren();
	BagItemsScrollBox->ClearChildren();
	ActiveActionProxies.Reset();

	auto MakeActionBtn = [&](UHorizontalBox* ParentRow, const FString& Label, const FLinearColor& BgCol, EIU_InvRowAction Act, const FString& InstID, const FString& SlotID, bool bLockState = false)
	{
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		Btn->SetBackgroundColor(BgCol);
		UTextBlock* Lbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Lbl->SetText(FText::FromString(Label));
		StyleInvText(Lbl, 9, FLinearColor(1.0f, 0.97f, 0.88f));
		Btn->SetContent(Lbl);

		UIU_InventoryActionProxy* Proxy = NewObject<UIU_InventoryActionProxy>(this);
		Proxy->OwnerWidget = this;
		Proxy->Action = Act;
		Proxy->ItemInstanceID = InstID;
		Proxy->SlotID = SlotID;
		Proxy->bTargetLockState = bLockState;
		ActiveActionProxies.Add(Proxy);
		Btn->OnClicked.AddDynamic(Proxy, &UIU_InventoryActionProxy::HandleButtonClicked);

		if (UHorizontalBoxSlot* BS = ParentRow->AddChildToHorizontalBox(Btn))
		{
			BS->SetVerticalAlignment(VAlign_Center);
			BS->SetPadding(FMargin(3.0f, 0.0f, 0.0f, 0.0f));
		}
	};

	// 1. Populate all 8 Canonical Equipment Slots
	for (const FIU_EquipSlotLabelDef& SlotDef : GCanonicalEquipSlots)
	{
		const FIU_InventoryItemView* FoundEq = nullptr;
		for (const FIU_InventoryItemView& EqItem : DisplayedSnapshot.EquippedItems)
		{
			if (EqItem.EquippedSlotID.Equals(SlotDef.SlotID, ESearchCase::IgnoreCase))
			{
				FoundEq = &EqItem;
				break;
			}
		}

		UHorizontalBox* EqRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		if (UVerticalBoxSlot* RS = EquippedSlotsBox->AddChildToVerticalBox(EqRow))
		{
			RS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 3.0f));
		}

		USizeBox* InfoBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		InfoBox->SetMinDesiredWidth(235.0f);
		UTextBlock* InfoText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());

		if (FoundEq)
		{
			const FString EnhTag = (FoundEq->EnhanceLevel > 0) ? FString::Printf(TEXT(" +%d"), FoundEq->EnhanceLevel) : TEXT("");
			InfoText->SetText(FText::FromString(FString::Printf(
				TEXT("%s: %s%s (%s)"),
				SlotDef.ShortLabel, *FoundEq->DisplayName, *EnhTag, *FoundEq->StatSummaryText)));
			StyleInvText(InfoText, 10, RarityToSlateColor(FoundEq->Rarity));
		}
		else
		{
			InfoText->SetText(FText::FromString(FString::Printf(TEXT("%s: [Trong]"), SlotDef.ShortLabel)));
			StyleInvText(InfoText, 10, FLinearColor(0.55f, 0.62f, 0.70f));
		}

		InfoBox->SetContent(InfoText);
		if (UHorizontalBoxSlot* IS = EqRow->AddChildToHorizontalBox(InfoBox))
		{
			IS->SetVerticalAlignment(VAlign_Center);
		}

		if (FoundEq)
		{
			if (FoundEq->MaxEnhanceLevel > 0 && FoundEq->EnhanceLevel < FoundEq->MaxEnhanceLevel)
			{
				const FString EnhLbl = FString::Printf(TEXT("+%d(%lldV)"), FoundEq->EnhanceLevel + 1, FoundEq->NextEnhanceCoin);
				MakeActionBtn(EqRow, EnhLbl, FLinearColor(0.45f, 0.30f, 0.08f, 1.0f), EIU_InvRowAction::Enhance, FoundEq->InstanceID, SlotDef.SlotID);
			}
			MakeActionBtn(EqRow, TEXT("THAO"), FLinearColor(0.28f, 0.16f, 0.20f, 1.0f), EIU_InvRowAction::Unequip, FoundEq->InstanceID, SlotDef.SlotID);
		}
	}

	// 2. Populate Scrollable Bag Items (0..47)
	if (DisplayedSnapshot.BagItems.Num() == 0)
	{
		UTextBlock* EmptyLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		EmptyLbl->SetText(FText::FromString(
			bHasSnapshotData
				? TEXT("Tui do hien dang trong (0/48). Hay danh quai de nhat Vang & Trang bi!")
				: TEXT("Dang dong bo du lieu Tui do tu Go Backend ( hoac che do Offline )...")));
		StyleInvText(EmptyLbl, 10, FLinearColor(0.68f, 0.78f, 0.88f));
		BagItemsScrollBox->AddChild(EmptyLbl);
		return;
	}

	for (const FIU_InventoryItemView& Item : DisplayedSnapshot.BagItems)
	{
		UHorizontalBox* BagRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		BagItemsScrollBox->AddChild(BagRow);

		USizeBox* DescBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		DescBox->SetMinDesiredWidth(258.0f);
		UTextBlock* DescText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());

		const FString EnhStr = (Item.EnhanceLevel > 0) ? FString::Printf(TEXT(" +%d"), Item.EnhanceLevel) : TEXT("");
		const FString StackStr = (Item.MaxStack > 1 || Item.StackCount > 1) ? FString::Printf(TEXT(" x%d"), Item.StackCount) : TEXT("");
		const FString LockStr = Item.bIsPlayerLocked ? TEXT(" [KHOA]") : TEXT("");
		const FString StatSuffix = Item.StatSummaryText.IsEmpty() ? TEXT("") : FString::Printf(TEXT(" | %s"), *Item.StatSummaryText);

		DescText->SetText(FText::FromString(FString::Printf(
			TEXT("[#%02d] %s%s%s%s%s"),
			FMath::Max(0, Item.BagSlotIndex), *Item.DisplayName, *EnhStr, *StackStr, *LockStr, *StatSuffix)));
		StyleInvText(DescText, 10, RarityToSlateColor(Item.Rarity));
		DescBox->SetContent(DescText);

		if (UHorizontalBoxSlot* DS = BagRow->AddChildToHorizontalBox(DescBox))
		{
			DS->SetVerticalAlignment(VAlign_Center);
			DS->SetPadding(FMargin(0.0f, 2.0f, 4.0f, 2.0f));
		}

		if (Item.Category.Equals(TEXT("EQUIPMENT"), ESearchCase::IgnoreCase))
		{
			MakeActionBtn(BagRow, TEXT("MAC"), FLinearColor(0.12f, 0.36f, 0.24f, 1.0f), EIU_InvRowAction::Equip, Item.InstanceID, Item.EquipSlot);
			if (Item.MaxEnhanceLevel > 0 && Item.EnhanceLevel < Item.MaxEnhanceLevel)
			{
				MakeActionBtn(BagRow, FString::Printf(TEXT("+%d"), Item.EnhanceLevel + 1), FLinearColor(0.45f, 0.30f, 0.08f, 1.0f), EIU_InvRowAction::Enhance, Item.InstanceID, TEXT(""));
			}
		}
		else if (Item.Category.Equals(TEXT("CONSUMABLE"), ESearchCase::IgnoreCase))
		{
			MakeActionBtn(BagRow, TEXT("DUNG"), FLinearColor(0.10f, 0.40f, 0.25f, 1.0f), EIU_InvRowAction::UseConsumable, Item.InstanceID, TEXT(""));
		}

		MakeActionBtn(
			BagRow,
			Item.bIsPlayerLocked ? TEXT("MO") : TEXT("KHOA"),
			Item.bIsPlayerLocked ? FLinearColor(0.38f, 0.26f, 0.12f, 1.0f) : FLinearColor(0.18f, 0.24f, 0.36f, 1.0f),
			EIU_InvRowAction::ToggleLock,
			Item.InstanceID,
			TEXT(""),
			!Item.bIsPlayerLocked);

		if (Item.SellPriceCoin > 0 && !Item.bIsPlayerLocked)
		{
			MakeActionBtn(
				BagRow,
				FString::Printf(TEXT("BAN(+%lldV)"), Item.SellPriceCoin),
				FLinearColor(0.36f, 0.18f, 0.14f, 1.0f),
				EIU_InvRowAction::SellOne,
				Item.InstanceID,
				TEXT(""));
		}
	}
}

void UIU_InventoryEquipmentWidget::HandleQuickUseElixirClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		SetFeedbackMessage(TEXT("Dang su dung Dau Than (+150 HP & +150 Ki, CD 3s)..."));
		IUPC->Server_RequestUseConsumable(TEXT(""));
	}
}

void UIU_InventoryEquipmentWidget::HandleRefreshClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		SetFeedbackMessage(TEXT("Dang tai lai Tui do & Trang bi tu Go Backend..."));
		IUPC->Server_RequestRefreshInventory();
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

void UIU_InventoryEquipmentWidget::HandleAnyVitalOrEconomyChanged(float, float, float)
{
	UpdateHeaderSummaryLabels();
}

void UIU_InventoryEquipmentWidget::HandleAttackOrDefenseChanged(float)
{
	UpdateHeaderSummaryLabels();
}

void UIU_InventoryEquipmentWidget::HandleEconomySummaryChanged(int64, int32, int32, int32)
{
	UpdateHeaderSummaryLabels();
}
