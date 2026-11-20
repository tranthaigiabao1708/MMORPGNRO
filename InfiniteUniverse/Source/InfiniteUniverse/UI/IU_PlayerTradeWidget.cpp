// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "UI/IU_PlayerTradeWidget.h"
#include "Core/IU_PlayerController.h"
#include "Core/IU_PlayerState.h"
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
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Fonts/SlateFontInfo.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UIU_TradeItemToggleProxy::HandleClicked()
{
	if (UIU_PlayerTradeWidget* W = OwnerWidget.Get())
	{
		W->ToggleOfferedBagItem(ItemInstanceID);
	}
}

namespace
{
	static void StyleTradeTxt(UTextBlock* TB, int32 Pt, const FLinearColor& Col)
	{
		if (!TB) { return; }
		FSlateFontInfo F = TB->GetFont();
		F.Size = Pt;
		TB->SetFont(F);
		TB->SetColorAndOpacity(FSlateColor(Col));
		TB->SetShadowOffset(FVector2D(1.0f, 1.0f));
		TB->SetShadowColorAndOpacity(FLinearColor(0.04f, 0.02f, 0.01f, 0.92f));
	}
}

TSharedRef<SWidget> UIU_PlayerTradeWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		OuterCardBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.85f, 0.76f, 0.62f, 0.98f),
			8.0f,
			FLinearColor(0.32f, 0.19f, 0.07f, 1.0f),
			2.5f);
		HeaderBrownBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.52f, 0.38f, 0.21f, 1.0f),
			6.0f,
			FLinearColor(0.30f, 0.18f, 0.08f, 1.0f),
			1.5f);
		ColumnPanelBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.42f, 0.31f, 0.18f, 0.98f),
			5.0f,
			FLinearColor(0.75f, 0.58f, 0.34f, 1.0f),
			1.4f);
		RedCloseBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.86f, 0.20f, 0.10f, 1.0f),
			5.0f,
			FLinearColor(0.35f, 0.05f, 0.02f, 1.0f),
			1.5f);

		UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TradeRootCanvas"));
		WidgetTree->RootWidget = RootCanvas;

		UBorder* MainCard = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TradeMainCard"));
		MainCard->SetBrush(OuterCardBrush);
		MainCard->SetPadding(FMargin(10.0f, 8.0f));
		if (UCanvasPanelSlot* CS = RootCanvas->AddChildToCanvas(MainCard))
		{
			CS->SetAnchors(FAnchors(0.5f, 0.5f));
			CS->SetAlignment(FVector2D(0.5f, 0.5f));
			CS->SetPosition(FVector2D(0.0f, -12.0f));
			CS->SetAutoSize(true);
		}

		USizeBox* CardWidth = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TradeWidthBox"));
		CardWidth->SetWidthOverride(536.0f);
		MainCard->SetContent(CardWidth);

		UVerticalBox* MainVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TradeMainVBox"));
		CardWidth->SetContent(MainVBox);

		// 1. Header Row
		UBorder* HeaderBdr = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TradeHeader"));
		HeaderBdr->SetBrush(HeaderBrownBrush);
		HeaderBdr->SetPadding(FMargin(8.0f, 5.0f));
		if (UVerticalBoxSlot* HS = MainVBox->AddChildToVerticalBox(HeaderBdr))
		{
			HS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
		}

		UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HeaderBdr->SetContent(HeaderRow);

		USizeBox* TitleW = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		TitleW->SetWidthOverride(468.0f);
		HeaderTitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TradeHeaderTitle"));
		HeaderTitleText->SetText(FText::FromString(TEXT("GIAO DICH TRUC TIEP 2 BUOC (CHONG LUA DAO & THUE 5% VANG)")));
		StyleTradeTxt(HeaderTitleText, 10, FLinearColor(1.0f, 0.92f, 0.25f));
		TitleW->SetContent(HeaderTitleText);
		if (UHorizontalBoxSlot* TS = HeaderRow->AddChildToHorizontalBox(TitleW))
		{
			TS->SetVerticalAlignment(VAlign_Center);
		}

		USizeBox* CloseSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		CloseSize->SetWidthOverride(30.0f);
		CloseSize->SetHeightOverride(28.0f);
		UBorder* CloseBg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		CloseBg->SetBrush(RedCloseBrush);
		HeaderCloseRedBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		HeaderCloseRedBtn->SetBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
		UTextBlock* XLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		XLbl->SetText(FText::FromString(TEXT("X")));
		StyleTradeTxt(XLbl, 11, FLinearColor::White);
		HeaderCloseRedBtn->SetContent(XLbl);
		CloseBg->SetContent(HeaderCloseRedBtn);
		CloseSize->SetContent(CloseBg);
		HeaderRow->AddChildToHorizontalBox(CloseSize);

		// 2. Dual Columns: Your Offer (Left) vs Partner Offer (Right)
		UHorizontalBox* ColumnsRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TradeColumnsRow"));
		if (UVerticalBoxSlot* CRS = MainVBox->AddChildToVerticalBox(ColumnsRow))
		{
			CRS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
		}

		auto BuildOfferColumn = [&](const TCHAR* Name, TObjectPtr<UTextBlock>& OutHdr, TObjectPtr<UTextBlock>& OutBody, bool bIncludeCoinBtns) -> USizeBox*
		{
			USizeBox* ColW = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), Name);
			ColW->SetWidthOverride(254.0f);
			UBorder* ColBdr = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			ColBdr->SetBrush(ColumnPanelBrush);
			ColBdr->SetPadding(FMargin(7.0f, 6.0f));
			ColW->SetContent(ColBdr);

			UVerticalBox* ColV = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
			ColBdr->SetContent(ColV);

			OutHdr = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			StyleTradeTxt(OutHdr, 10, FLinearColor(1.0f, 0.90f, 0.30f));
			if (UVerticalBoxSlot* S1 = ColV->AddChildToVerticalBox(OutHdr))
			{
				S1->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
			}

			USizeBox* BodyH = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			BodyH->SetHeightOverride(112.0f);
			OutBody = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			StyleTradeTxt(OutBody, 9, FLinearColor(0.96f, 0.95f, 0.88f));
			BodyH->SetContent(OutBody);
			ColV->AddChildToVerticalBox(BodyH);

			if (bIncludeCoinBtns)
			{
				UHorizontalBox* CoinBtns = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
				ColV->AddChildToVerticalBox(CoinBtns);

				auto MakeSmallBtn = [&](const TCHAR* Lbl, const FLinearColor& Bg, TObjectPtr<UButton>& OutB)
				{
					OutB = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
					OutB->SetBackgroundColor(Bg);
					UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
					T->SetText(FText::FromString(Lbl));
					StyleTradeTxt(T, 8, FLinearColor::White);
					OutB->SetContent(T);
					if (UHorizontalBoxSlot* BS = CoinBtns->AddChildToHorizontalBox(OutB))
					{
						BS->SetPadding(FMargin(0.0f, 2.0f, 4.0f, 0.0f));
					}
				};
				MakeSmallBtn(TEXT(" +50 V "), FLinearColor(0.76f, 0.50f, 0.08f, 1.0f), Add50CoinBtn);
				MakeSmallBtn(TEXT(" +100 V "), FLinearColor(0.84f, 0.56f, 0.08f, 1.0f), Add100CoinBtn);
				MakeSmallBtn(TEXT(" 0 V "), FLinearColor(0.45f, 0.25f, 0.16f, 1.0f), ClearCoinBtn);
			}
			return ColW;
		};

		if (UHorizontalBoxSlot* LS = ColumnsRow->AddChildToHorizontalBox(BuildOfferColumn(TEXT("MyOfferCol"), MyOfferHeaderText, MyOfferSummaryText, true)))
		{
			LS->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		}
		ColumnsRow->AddChildToHorizontalBox(BuildOfferColumn(TEXT("PartnerOfferCol"), PartnerOfferHeaderText, PartnerOfferSummaryText, false));

		// 3. Unbound Bag Item Picker
		UBorder* PickerBdr = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TradeBagPickerBdr"));
		PickerBdr->SetBrushColor(FLinearColor(0.34f, 0.24f, 0.14f, 0.98f));
		PickerBdr->SetPadding(FMargin(6.0f, 5.0f));
		if (UVerticalBoxSlot* PS = MainVBox->AddChildToVerticalBox(PickerBdr))
		{
			PS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
		}

		UVerticalBox* PickerV = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		PickerBdr->SetContent(PickerV);

		UTextBlock* PickerTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		PickerTitle->SetText(FText::FromString(TEXT("CHON VAT PHAM KHONG KHOA TRONG TUI DE DAT LEN BAN GIAO DICH:")));
		StyleTradeTxt(PickerTitle, 8, FLinearColor(0.95f, 0.88f, 0.68f));
		PickerV->AddChildToVerticalBox(PickerTitle);

		UScrollBox* PickerScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
		PickerScroll->SetOrientation(Orient_Horizontal);
		UnboundBagPickerBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		PickerScroll->AddChild(UnboundBagPickerBox);
		PickerV->AddChildToVerticalBox(PickerScroll);

		// 4. Countdown + 2-Step Action Buttons ([1. KHOA] -> 3.0s -> [2. XAC NHAN] | [HUY])
		CountdownStatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TradeCountdownTxt"));
		CountdownStatusText->SetText(FText::FromString(TEXT("Buoc 1: Dat vat pham/Vang roi bam [1. KHOA GIAO DICH]")));
		StyleTradeTxt(CountdownStatusText, 9, FLinearColor(0.20f, 0.12f, 0.04f));
		if (UVerticalBoxSlot* CDS = MainVBox->AddChildToVerticalBox(CountdownStatusText))
		{
			CDS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
		}

		UHorizontalBox* ActionRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		MainVBox->AddChildToVerticalBox(ActionRow);

		auto MakeMainBtn = [&](const TCHAR* Lbl, const FLinearColor& Bg, TObjectPtr<UButton>& OutB)
		{
			OutB = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
			OutB->SetBackgroundColor(Bg);
			UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			T->SetText(FText::FromString(Lbl));
			StyleTradeTxt(T, 9, FLinearColor::White);
			OutB->SetContent(T);
			if (UHorizontalBoxSlot* BS = ActionRow->AddChildToHorizontalBox(OutB))
			{
				BS->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
			}
		};
		MakeMainBtn(TEXT(" 1. KHOA GIAO DICH "), FLinearColor(0.84f, 0.50f, 0.08f, 1.0f), LockOfferBtn);
		MakeMainBtn(TEXT(" 2. XAC NHAN (SAU 3S) "), FLinearColor(0.16f, 0.62f, 0.26f, 1.0f), ConfirmTradeBtn);
		MakeMainBtn(TEXT(" HUY GIAO DICH "), FLinearColor(0.72f, 0.18f, 0.12f, 1.0f), CancelTradeBtn);

		RebuildTradeView();
	}
	return Super::RebuildWidget();
}

void UIU_PlayerTradeWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (Add50CoinBtn) { Add50CoinBtn->OnClicked.AddUniqueDynamic(this, &UIU_PlayerTradeWidget::HandleAdd50CoinClicked); }
	if (Add100CoinBtn) { Add100CoinBtn->OnClicked.AddUniqueDynamic(this, &UIU_PlayerTradeWidget::HandleAdd100CoinClicked); }
	if (ClearCoinBtn) { ClearCoinBtn->OnClicked.AddUniqueDynamic(this, &UIU_PlayerTradeWidget::HandleClearCoinClicked); }
	if (LockOfferBtn) { LockOfferBtn->OnClicked.AddUniqueDynamic(this, &UIU_PlayerTradeWidget::HandleLockOfferClicked); }
	if (ConfirmTradeBtn) { ConfirmTradeBtn->OnClicked.AddUniqueDynamic(this, &UIU_PlayerTradeWidget::HandleConfirmTradeClicked); }
	if (CancelTradeBtn) { CancelTradeBtn->OnClicked.AddUniqueDynamic(this, &UIU_PlayerTradeWidget::HandleCancelTradeClicked); }
	if (HeaderCloseRedBtn) { HeaderCloseRedBtn->OnClicked.AddUniqueDynamic(this, &UIU_PlayerTradeWidget::HandleCancelTradeClicked); }

	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().SetTimer(InspectionTickTimer, this, &UIU_PlayerTradeWidget::TickInspectionCountdown, 0.15f, true);
	}
	RebuildTradeView();
}

void UIU_PlayerTradeWidget::NativeDestruct()
{
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(InspectionTickTimer);
	}
	ItemProxies.Empty();
	Super::NativeDestruct();
}

void UIU_PlayerTradeWidget::ApplyTradeSnapshot(const FIU_TradeSessionSnapshot& Snapshot)
{
	CurrentTrade = Snapshot;

	const AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer());
	const AIU_PlayerState* PS = IUPC ? IUPC->GetPlayerState<AIU_PlayerState>() : nullptr;
	const FString MyCharID = PS ? PS->GetPersistedCharacterUUID() : TEXT("");
	const bool bAmInitiator = (!MyCharID.IsEmpty() && CurrentTrade.InitiatorCharID == MyCharID);

	if (CurrentTrade.OfferVersion != LastSyncedOfferVersion)
	{
		LastSyncedOfferVersion = CurrentTrade.OfferVersion;
		LocalDraftCoinOffer = bAmInitiator ? CurrentTrade.InitiatorCoin : CurrentTrade.RecipientCoin;
		LocalDraftItemIDs.Reset();
		const TArray<FIU_TradeItemView>& MyItems = bAmInitiator ? CurrentTrade.InitiatorItems : CurrentTrade.RecipientItems;
		for (const FIU_TradeItemView& It : MyItems)
		{
			LocalDraftItemIDs.Add(It.InstanceID);
		}
	}

	if (CurrentTrade.State == TEXT("BOTH_LOCKED") && CurrentTrade.RemainingInspectMs > 0)
	{
		if (const UWorld* W = GetWorld())
		{
			LocalCountdownEndTime = W->GetTimeSeconds() + (static_cast<double>(CurrentTrade.RemainingInspectMs) / 1000.0);
		}
	}
	else if (CurrentTrade.State != TEXT("BOTH_LOCKED"))
	{
		LocalCountdownEndTime = 0.0;
	}

	RebuildTradeView();
}

void UIU_PlayerTradeWidget::RefreshFromCachedInventory()
{
	RebuildTradeView();
}

void UIU_PlayerTradeWidget::ToggleOfferedBagItem(const FString& ItemInstanceID)
{
	if (CurrentTrade.State != TEXT("OPEN") || ItemInstanceID.IsEmpty())
	{
		return;
	}
	if (LocalDraftItemIDs.Contains(ItemInstanceID))
	{
		LocalDraftItemIDs.Remove(ItemInstanceID);
	}
	else if (LocalDraftItemIDs.Num() < 8)
	{
		LocalDraftItemIDs.Add(ItemInstanceID);
	}
	PushUpdatedOfferToServer();
}

void UIU_PlayerTradeWidget::HandleAdd50CoinClicked()
{
	if (CurrentTrade.State != TEXT("OPEN")) { return; }
	LocalDraftCoinOffer += 50;
	PushUpdatedOfferToServer();
}

void UIU_PlayerTradeWidget::HandleAdd100CoinClicked()
{
	if (CurrentTrade.State != TEXT("OPEN")) { return; }
	LocalDraftCoinOffer += 100;
	PushUpdatedOfferToServer();
}

void UIU_PlayerTradeWidget::HandleClearCoinClicked()
{
	if (CurrentTrade.State != TEXT("OPEN")) { return; }
	LocalDraftCoinOffer = 0;
	PushUpdatedOfferToServer();
}

void UIU_PlayerTradeWidget::PushUpdatedOfferToServer()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		IUPC->Server_RequestTradeUpdateOffer(LocalDraftCoinOffer, LocalDraftItemIDs);
	}
}

void UIU_PlayerTradeWidget::HandleLockOfferClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		IUPC->Server_RequestTradeLock();
	}
}

void UIU_PlayerTradeWidget::HandleConfirmTradeClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		IUPC->Server_RequestTradeConfirm();
	}
}

void UIU_PlayerTradeWidget::HandleCancelTradeClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		IUPC->Server_RequestTradeCancel();
		IUPC->SetPlayerTradePanelVisible(false, true);
	}
}

void UIU_PlayerTradeWidget::TickInspectionCountdown()
{
	if (CurrentTrade.State == TEXT("BOTH_LOCKED"))
	{
		RebuildTradeView();
	}
}

void UIU_PlayerTradeWidget::RebuildTradeView()
{
	if (!WidgetTree || !MyOfferHeaderText || !PartnerOfferHeaderText || !MyOfferSummaryText || !PartnerOfferSummaryText || !UnboundBagPickerBox)
	{
		return;
	}

	const AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer());
	const AIU_PlayerState* PS = IUPC ? IUPC->GetPlayerState<AIU_PlayerState>() : nullptr;
	const FString MyCharID = PS ? PS->GetPersistedCharacterUUID() : TEXT("");
	const bool bAmInitiator = (MyCharID.IsEmpty() || CurrentTrade.InitiatorCharID == MyCharID);

	const FString MyName = bAmInitiator ? CurrentTrade.InitiatorName : CurrentTrade.RecipientName;
	const FString PartnerName = bAmInitiator ? CurrentTrade.RecipientName : CurrentTrade.InitiatorName;
	const int64 MyCoin = bAmInitiator ? CurrentTrade.InitiatorCoin : CurrentTrade.RecipientCoin;
	const int64 PartnerCoin = bAmInitiator ? CurrentTrade.RecipientCoin : CurrentTrade.InitiatorCoin;
	const bool bMyLocked = bAmInitiator ? CurrentTrade.bInitiatorLocked : CurrentTrade.bRecipientLocked;
	const bool bPartnerLocked = bAmInitiator ? CurrentTrade.bRecipientLocked : CurrentTrade.bInitiatorLocked;
	const bool bMyConf = bAmInitiator ? CurrentTrade.bInitiatorConfirmed : CurrentTrade.bRecipientConfirmed;
	const bool bPartnerConf = bAmInitiator ? CurrentTrade.bRecipientConfirmed : CurrentTrade.bInitiatorConfirmed;
	const TArray<FIU_TradeItemView>& MyItems = bAmInitiator ? CurrentTrade.InitiatorItems : CurrentTrade.RecipientItems;
	const TArray<FIU_TradeItemView>& PartnerItems = bAmInitiator ? CurrentTrade.RecipientItems : CurrentTrade.InitiatorItems;

	MyOfferHeaderText->SetText(FText::FromString(FString::Printf(
		TEXT("CUA BAN (%s) [%s%s]"),
		MyName.IsEmpty() ? TEXT("Ban") : *MyName,
		bMyLocked ? TEXT("DA KHOA") : TEXT("MO"),
		bMyConf ? TEXT(" | DA XN") : TEXT(""))));

	PartnerOfferHeaderText->SetText(FText::FromString(FString::Printf(
		TEXT("DOI PHUONG (%s) [%s%s]"),
		PartnerName.IsEmpty() ? TEXT("Doi Tac") : *PartnerName,
		bPartnerLocked ? TEXT("DA KHOA") : TEXT("MO"),
		bPartnerConf ? TEXT(" | DA XN") : TEXT(""))));

	auto BuildItemsText = [](int64 Coin, const TArray<FIU_TradeItemView>& Items) -> FString
	{
		const int64 Tax = (Coin * 500) / 10000;
		FString Out = FString::Printf(TEXT("Vang trao: %lld V (Thue 5%%: %lld V)\n"), Coin, Tax);
		if (Items.Num() == 0)
		{
			Out += TEXT("- (Chua dat vat pham nao)");
		}
		else
		{
			for (const FIU_TradeItemView& It : Items)
			{
				const FString Enh = (It.EnhanceLevel > 0) ? FString::Printf(TEXT(" +%d"), It.EnhanceLevel) : TEXT("");
				const FString Af = It.StatSummaryText.IsEmpty() ? TEXT("") : FString::Printf(TEXT(" [%s]"), *It.StatSummaryText);
				Out += FString::Printf(TEXT("- %s%s x%d%s\n"), *It.DisplayName, *Enh, It.StackCount, *Af);
			}
		}
		return Out;
	};

	MyOfferSummaryText->SetText(FText::FromString(BuildItemsText(MyCoin, MyItems)));
	PartnerOfferSummaryText->SetText(FText::FromString(BuildItemsText(PartnerCoin, PartnerItems)));

	UnboundBagPickerBox->ClearChildren();
	ItemProxies.Reset();
	if (IUPC && IUPC->HasCachedInventorySnapshot())
	{
		for (const FIU_InventoryItemView& BagIt : IUPC->GetCachedInventorySnapshot().BagItems)
		{
			if (BagIt.bIsBound || BagIt.bIsPlayerLocked)
			{
				continue;
			}
			const bool bChosen = LocalDraftItemIDs.Contains(BagIt.InstanceID);
			UButton* PickBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
			PickBtn->SetBackgroundColor(bChosen
				? FLinearColor(0.16f, 0.64f, 0.28f, 1.0f)
				: FLinearColor(0.48f, 0.36f, 0.22f, 1.0f));
			UTextBlock* Lbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Lbl->SetText(FText::FromString(FString::Printf(
				TEXT(" %s%s "),
				bChosen ? TEXT("[CHON] ") : TEXT(""),
				*BagIt.DisplayName.Left(18))));
			StyleTradeTxt(Lbl, 8, FLinearColor::White);
			PickBtn->SetContent(Lbl);

			UIU_TradeItemToggleProxy* Proxy = NewObject<UIU_TradeItemToggleProxy>(this);
			Proxy->OwnerWidget = this;
			Proxy->ItemInstanceID = BagIt.InstanceID;
			ItemProxies.Add(Proxy);
			PickBtn->OnClicked.AddDynamic(Proxy, &UIU_TradeItemToggleProxy::HandleClicked);

			if (UHorizontalBoxSlot* HS = UnboundBagPickerBox->AddChildToHorizontalBox(PickBtn))
			{
				HS->SetPadding(FMargin(0.0f, 2.0f, 6.0f, 0.0f));
			}
		}
	}

	const UWorld* W = GetWorld();
	const double RemSec = (W && LocalCountdownEndTime > 0.0) ? FMath::Max(0.0, LocalCountdownEndTime - W->GetTimeSeconds()) : 0.0;
	const bool bCanConfirm = (CurrentTrade.State == TEXT("BOTH_LOCKED")) && (RemSec <= 0.05) && !bMyConf;

	if (ConfirmTradeBtn)
	{
		ConfirmTradeBtn->SetIsEnabled(bCanConfirm);
	}
	if (CountdownStatusText)
	{
		if (CurrentTrade.State == TEXT("BOTH_LOCKED"))
		{
			if (RemSec > 0.05)
			{
				CountdownStatusText->SetText(FText::FromString(
					FString::Printf(TEXT("[CHONG LUA DAO] Dang dem nguoc kiem tra do & Vang: con %.1f giay..."), RemSec)));
			}
			else if (bMyConf)
			{
				CountdownStatusText->SetText(FText::FromString(TEXT("Ban da XAC NHAN! Dang cho doi phuong bam [2. XAC NHAN]...")));
			}
			else
			{
				CountdownStatusText->SetText(FText::FromString(TEXT("Da du 3.0s an toan! Bam [2. XAC NHAN] de hoan tat giao dich ACID!")));
			}
		}
		else if (!CurrentTrade.ActionMessage.IsEmpty())
		{
			CountdownStatusText->SetText(FText::FromString(CurrentTrade.ActionMessage));
		}
	}
}
