// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Network/IU_BackendSubsystem.h"
#include "Styling/SlateBrush.h"
#include "IU_PlayerTradeWidget.generated.h"

class UTextBlock;
class UButton;
class UBorder;
class UVerticalBox;
class UHorizontalBox;
class UScrollBox;
class UIU_PlayerTradeWidget;

UCLASS()
class INFINITEUNIVERSE_API UIU_TradeItemToggleProxy : public UObject
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UIU_PlayerTradeWidget> OwnerWidget;
	FString ItemInstanceID;

	UFUNCTION()
	void HandleClicked();
};

/**
 * NRO-Style Atomic 2-Step P2P Direct Trade Modal (Key: T):
 * - Dual-column parchment window: [PHAN CUA BAN] vs [PHAN DOI PHUONG]
 * - Displays unbound unlocked bag items for one-click offer toggle + Vang offer buttons (+50V, +100V, Clear)
 * - Enforces Step 1 [KHOA GIAO DICH] -> 3.0s Anti-Scam Inspection Countdown -> Step 2 [XAC NHAN GIAO DICH]
 * - Displays 5% Vang Trade Tax preview and full partner item Rarity / +Enhance / [Af] Affix stats.
 */
UCLASS()
class INFINITEUNIVERSE_API UIU_PlayerTradeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	void ApplyTradeSnapshot(const FIU_TradeSessionSnapshot& Snapshot);
	void RefreshFromCachedInventory();
	void ToggleOfferedBagItem(const FString& ItemInstanceID);

	const FIU_TradeSessionSnapshot& GetDisplayedTradeSnapshot() const { return CurrentTrade; }

private:
	UFUNCTION()
	void HandleAdd50CoinClicked();
	UFUNCTION()
	void HandleAdd100CoinClicked();
	UFUNCTION()
	void HandleClearCoinClicked();
	UFUNCTION()
	void HandleLockOfferClicked();
	UFUNCTION()
	void HandleConfirmTradeClicked();
	UFUNCTION()
	void HandleCancelTradeClicked();

	void TickInspectionCountdown();
	void PushUpdatedOfferToServer();
	void RebuildTradeView();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeaderTitleText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MyOfferHeaderText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PartnerOfferHeaderText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MyOfferSummaryText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PartnerOfferSummaryText;
	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> UnboundBagPickerBox;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CountdownStatusText;

	UPROPERTY(Transient)
	TObjectPtr<UButton> Add50CoinBtn;
	UPROPERTY(Transient)
	TObjectPtr<UButton> Add100CoinBtn;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ClearCoinBtn;
	UPROPERTY(Transient)
	TObjectPtr<UButton> LockOfferBtn;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ConfirmTradeBtn;
	UPROPERTY(Transient)
	TObjectPtr<UButton> CancelTradeBtn;
	UPROPERTY(Transient)
	TObjectPtr<UButton> HeaderCloseRedBtn;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UIU_TradeItemToggleProxy>> ItemProxies;

	FSlateBrush OuterCardBrush;
	FSlateBrush HeaderBrownBrush;
	FSlateBrush ColumnPanelBrush;
	FSlateBrush RedCloseBrush;

	FIU_TradeSessionSnapshot CurrentTrade;
	int64 LocalDraftCoinOffer = 0;
	TArray<FString> LocalDraftItemIDs;
	int32 LastSyncedOfferVersion = -1;
	double LocalCountdownEndTime = 0.0;
	FTimerHandle InspectionTickTimer;
};
