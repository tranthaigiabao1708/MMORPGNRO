// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Network/IU_BackendSubsystem.h"
#include "IU_InventoryEquipmentWidget.generated.h"

class UTextBlock;
class UButton;
class UVerticalBox;
class UScrollBox;
class UIU_AttributeComponent;
class AIU_PlayerState;
class UIU_InventoryEquipmentWidget;

UENUM()
enum class EIU_InvRowAction : uint8
{
	Equip,
	Unequip,
	UseConsumable,
	Enhance,
	ToggleLock,
	SellOne
};

/**
 * Lightweight GC-managed UObject proxy binding dynamic UMG Bag & 8-Slot Equipment buttons cleanly to UIU_InventoryEquipmentWidget.
 */
UCLASS()
class INFINITEUNIVERSE_API UIU_InventoryActionProxy : public UObject
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UIU_InventoryEquipmentWidget> OwnerWidget;
	EIU_InvRowAction Action = EIU_InvRowAction::Equip;
	FString ItemInstanceID;
	FString SlotID;
	bool bTargetLockState = false;

	UFUNCTION()
	void HandleButtonClicked();
};

/**
 * Step 4 Programmatic UMG Modal (Keys I / B):
 * - Displays Wallet (Vang), Quick-Use Dau Than [Key 4] & Da Cuong Hoa counts, and live compiled Stats (HP, Ki, SucDanh, Giap).
 * - Left Column: 8 Canonical Equipment Slots (head, body, legs, hands, feet, acc_1, acc_2, special) with [THAO] & [+CH] buttons.
 * - Right Column: 48-Slot Bag ScrollBox with [MAC], [DUNG], [+CH], [KHOA/MO], and [BAN] actions.
 * - Strictly client-intent only: dispatches Server RPCs via AIU_PlayerController and renders authoritative FIU_InventorySnapshot updates.
 */
UCLASS()
class INFINITEUNIVERSE_API UIU_InventoryEquipmentWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Binds read-only attribute & PlayerState delegates and rebuilds rows from OwningController's cached snapshot. */
	void RefreshBindingsAndRebuild();

	/** Updates UI immediately from a newly received authoritative FIU_InventorySnapshot. */
	void ApplyInventorySnapshot(const FIU_InventorySnapshot& Snapshot);

	/** Displays a transient feedback line in the modal footer (e.g. cooldown warning or loot toast). */
	void SetFeedbackMessage(const FString& Message, bool bIsError = false);

	void ExecuteRowAction(EIU_InvRowAction Action, const FString& ItemInstanceID, const FString& SlotID, bool bTargetLockState);

private:
	UFUNCTION()
	void HandleQuickUseElixirClicked();

	UFUNCTION()
	void HandleRefreshClicked();

	UFUNCTION()
	void HandleCloseClicked();

	UFUNCTION()
	void HandleAnyVitalOrEconomyChanged(float CurrentVal, float MaxVal, float Delta);

	UFUNCTION()
	void HandleAttackOrDefenseChanged(float NewValue);

	UFUNCTION()
	void HandleEconomySummaryChanged(int64 StandardCoin, int32 ElixirCount, int32 CrimsonOreCount, int32 EquippedCount);

	void UnbindAll();
	void RebuildDynamicRows();
	void UpdateHeaderSummaryLabels();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WalletAndBagSummaryText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CompiledStatsSummaryText;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> EquippedSlotsBox;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> BagItemsScrollBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FeedbackStatusText;

	UPROPERTY(Transient)
	TObjectPtr<UButton> QuickElixirButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> RefreshButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ClosePanelButton;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UIU_InventoryActionProxy>> ActiveActionProxies;

	FIU_InventorySnapshot DisplayedSnapshot;
	bool bHasSnapshotData = false;

	TWeakObjectPtr<UIU_AttributeComponent> BoundAttr;
	TWeakObjectPtr<AIU_PlayerState> BoundPS;
};
