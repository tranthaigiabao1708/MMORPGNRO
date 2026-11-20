// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/IU_GameplayTypes.h"
#include "Network/IU_BackendSubsystem.h"
#include "Styling/SlateBrush.h"
#include "IU_InventoryEquipmentWidget.generated.h"

class UTextBlock;
class UButton;
class UBorder;
class UVerticalBox;
class UHorizontalBox;
class UScrollBox;
class UUniformGridPanel;
class UWidgetSwitcher;
class UEditableTextBox;
class UIU_AttributeComponent;
class AIU_PlayerState;
class UIU_InventoryEquipmentWidget;

UENUM(BlueprintType)
enum class EIU_CharacterProfileTab : uint8
{
	NhiemVu   = 0,
	HanhTrang = 1,
	KyNang    = 2,
	BangHoi   = 3,
	ChucNang  = 4
};

UENUM()
enum class EIU_InvRowAction : uint8
{
	SelectEquippedSlot,
	SelectBagCell,
	Equip,
	Unequip,
	UseConsumable,
	Enhance,
	ToggleLock,
	SellOne,
	JoinGuildByID,
	PromoteGuildVice,
	KickGuildMember,
	SwitchZoneChannel,
	ClaimQuestReward,
	RepeatQuest,
	RedeemPityShopItem
};

/**
 * Lightweight GC-managed UObject proxy binding dynamic NRO Paperdoll slots, 48-cell Bag Grid tiles, and Action buttons.
 */
UCLASS()
class INFINITEUNIVERSE_API UIU_InventoryActionProxy : public UObject
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UIU_InventoryEquipmentWidget> OwnerWidget;
	EIU_InvRowAction Action = EIU_InvRowAction::SelectBagCell;
	FString ItemInstanceID;
	FString SlotID;
	int32 BagSlotIndex = -1;
	bool bTargetLockState = false;

	UFUNCTION()
	void HandleButtonClicked();
};

/**
 * NRO-Style Unified Character Profile Modal (Key: Tab; Shortcuts: I/B -> Hanh Trang, P -> Ky Nang):
 * - Top Brown-Gold Header: Character Portrait + HP / KI / Suc danh / Giap & Chi mang + Red [X] Close Button.
 * - 5 Classic NRO Menu Tabs: [Nhiem Vu] | [Hanh Trang] | [Ky Nang] | [Bang Hoi] | [Chuc Nang].
 * - Tab [Hanh Trang] (Exact Image 1 Layout):
 *     * 3-Column Paperdoll: 4 Left Equip Slots + Center Character Stage & Sub-Stats + 4 Right Equip Slots
 *     * Selected Item Inspector & One-Click Action Bar ([MAC] / [THAO] / [DUNG] / [+CH] / [KHOA] / [BAN])
 *     * 48-Slot Square Bag Grid (6 cols x 8 rows in ScrollBox) with rarity borders & bottom-right yellow stack counts
 * - Tab [Ky Nang]:
 *     * Authoritative Potential Point Stat Upgrades (+100 HP, +100 KI, +10 Suc danh — UI click ONLY, never keys 1/2/3!)
 *     * Active Combat Skill Hub Reference (Slots 1..6)
 * - Bottom Currency Footer Bar: Gold Vang + Emerald Tiem Nang + Ruby Da Cuong Hoa.
 */
UCLASS()
class INFINITEUNIVERSE_API UIU_InventoryEquipmentWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Switches among the 5 NRO Character Profile tabs (Nhiem Vu, Hanh Trang, Ky Nang, Bang Hoi, Chuc Nang). */
	UFUNCTION(BlueprintCallable, Category = "IU|UI")
	void SetActiveProfileTab(EIU_CharacterProfileTab NewTab);

	UFUNCTION(BlueprintPure, Category = "IU|UI")
	EIU_CharacterProfileTab GetActiveProfileTab() const { return CurrentProfileTab; }

	/** Binds read-only attribute & PlayerState delegates and rebuilds Paperdoll + 48-cell Bag Grid. */
	void RefreshBindingsAndRebuild();

	/** Updates UI immediately from a newly received authoritative FIU_InventorySnapshot. */
	void ApplyInventorySnapshot(const FIU_InventorySnapshot& Snapshot);

	/** Updates Tab [Bang Hoi] immediately from a newly received authoritative FIU_GuildSnapshot. */
	void ApplyGuildSnapshot(const FIU_GuildSnapshot& Snapshot);

	/** Updates Tab [Nhiem Vu] immediately from a newly received authoritative FIU_QuestSnapshot. */
	void ApplyQuestSnapshot(const FIU_QuestSnapshot& Snapshot);

	/** Updates Tab [Chuc Nang -> Doi Khu] immediately from live Dedicated Server channel list. */
	void ApplyWorldChannels(const TArray<FIU_ZoneChannelView>& Channels);

	/** Updates Tab [Chuc Nang -> Shop Bao Hiem Boss] immediately from authoritative FIU_PityShopSnapshot. */
	void ApplyPityShopSnapshot(const FIU_PityShopSnapshot& Snapshot);

	/** Switches Tab [Chuc Nang] sub-view (0 = Doi Khu / Vung, 1 = Shop Bao Hiem Boss, 2 = Cai Dat). */
	UFUNCTION(BlueprintCallable, Category = "IU|UI")
	void SetActiveFuncSubMode(int32 SubModeIndex);

	int32 GetDisplayedQuestCount() const { return DisplayedQuestSnapshot.Quests.Num(); }
	int32 GetDisplayedPityEntryCount() const { return DisplayedPityShopSnapshot.Entries.Num(); }
	int32 GetDisplayedChannelCount() const { return DisplayedWorldChannels.Num(); }

	/** Displays a feedback status line in the bottom footer. */
	void SetFeedbackMessage(const FString& Message, bool bIsError = false);

	void ExecuteRowAction(
		EIU_InvRowAction Action,
		const FString& ItemInstanceID,
		const FString& SlotID,
		int32 BagSlotIndex,
		bool bTargetLockState);

private:
	UFUNCTION()
	void HandleTabNhiemVuClicked();
	UFUNCTION()
	void HandleTabHanhTrangClicked();
	UFUNCTION()
	void HandleTabKyNangClicked();
	UFUNCTION()
	void HandleTabBangHoiClicked();
	UFUNCTION()
	void HandleTabChucNangClicked();

	UFUNCTION()
	void HandleUpgradeHPClicked();
	UFUNCTION()
	void HandleUpgradeKiClicked();
	UFUNCTION()
	void HandleUpgradeAtkClicked();

	UFUNCTION()
	void HandleCreateGuildClicked();
	UFUNCTION()
	void HandleRefreshGuildClicked();
	UFUNCTION()
	void HandleSaveGuildMOTDClicked();
	UFUNCTION()
	void HandleDonate50GuildClicked();
	UFUNCTION()
	void HandleDonate150GuildClicked();
	UFUNCTION()
	void HandleLeaveGuildClicked();

	UFUNCTION()
	void HandleFuncToggleDebugClicked();
	UFUNCTION()
	void HandleFuncToggleRotClicked();
	UFUNCTION()
	void HandleFuncTogglePvPClicked();
	UFUNCTION()
	void HandleFuncAuthPanelClicked();
	UFUNCTION()
	void HandleRefreshClicked();
	UFUNCTION()
	void HandleRefreshQuestsClicked();
	UFUNCTION()
	void HandleFuncSubModeChannelsClicked();
	UFUNCTION()
	void HandleFuncSubModePityShopClicked();
	UFUNCTION()
	void HandleFuncSubModeSettingsClicked();
	UFUNCTION()
	void HandleRefreshChannelsClicked();
	UFUNCTION()
	void HandleCloseClicked();

	UFUNCTION()
	void HandleAnyVitalOrEconomyChanged(float CurrentVal, float MaxVal, float Delta);
	UFUNCTION()
	void HandleAttackOrDefenseChanged(float NewValue);
	UFUNCTION()
	void HandleCharacterEXPChanged(int64 CurrentEXP, int64 NextLevelEXP, int32 CurrentLevel);
	UFUNCTION()
	void HandleCharacterPotentialChanged(int64 PotentialPoints, int64 NextHPCost, int64 NextKiCost, int64 NextAtkCost);
	UFUNCTION()
	void HandleEconomySummaryChanged(int64 StandardCoin, int32 ElixirCount, int32 CrimsonOreCount, int32 EquippedCount);

	void RequestPotentialStatUpgrade(EIU_PotentialStatType StatType);
	void UnbindAll();
	void UpdateTabHighlightVisuals();
	void UpdateAllReadouts();
	void RebuildPaperdollAndBagGrid();
	void RebuildSelectedInspectorBar();
	void RebuildGuildPaneView();
	void RebuildQuestPaneView();
	void RebuildChannelListView();
	void RebuildPityShopView();

	// Header & Tab Bar Widgets
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> AvatarBadgeText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeaderHPText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeaderKiText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeaderAtkText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeaderDefCritText;
	UPROPERTY(Transient)
	TObjectPtr<UButton> CloseRedXButton;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> TabOuterBorders; // 5 tabs
	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> TabButtons; // 5 tabs
	UPROPERTY(Transient)
	TObjectPtr<UWidgetSwitcher> TabContentSwitcher;

	// Pane 0: Nhiem Vu (Step 6 PostgreSQL character_quests + Turn-In)
	UPROPERTY(Transient)
	TObjectPtr<UButton> QuestRefreshButton;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> QuestListVBox;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> QuestChecklistText;

	// Pane 1: Hanh Trang (Paperdoll + Inspector + 48-Slot Grid)
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> LeftEquipColumnBox;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> RightEquipColumnBox;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PaperdollNameplateText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PaperdollAvatarGlyphText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PaperdollSubStatsText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SelectedItemTitleText;
	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> SelectedItemActionsBox;
	UPROPERTY(Transient)
	TObjectPtr<UUniformGridPanel> BagUniformGrid;

	// Pane 2: Ky Nang & Cong Chi So Tiem Nang
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PotentialPoolBannerText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PotHPRowText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PotKiRowText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PotAtkRowText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> UpgradeHPBtnLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> UpgradeKiBtnLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> UpgradeAtkBtnLabel;
	UPROPERTY(Transient)
	TObjectPtr<UButton> UpgradeHPButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> UpgradeKiButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> UpgradeAtkButton;

	// Pane 3: Bang Hoi (Step 5 PostgreSQL 18 Guild Management)
	UPROPERTY(Transient)
	TObjectPtr<UWidgetSwitcher> GuildModeSwitcher;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> GuildCreateNameInput;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> GuildCreateTagInput;
	UPROPERTY(Transient)
	TObjectPtr<UButton> GuildCreateButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> GuildRefreshDirButton;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> GuildDirectoryListBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GuildHeaderSummaryText;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> GuildMOTDInputBox;
	UPROPERTY(Transient)
	TObjectPtr<UButton> GuildSaveMOTDButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> GuildDonate50Button;
	UPROPERTY(Transient)
	TObjectPtr<UButton> GuildDonate150Button;
	UPROPERTY(Transient)
	TObjectPtr<UButton> GuildRefreshRosterButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> GuildLeaveButton;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> GuildMembersListBox;

	// Pane 4: Chuc Nang (Step 6 Doi Khu / Chuyen Vung + Shop Bao Hiem Boss + Cai Dat)
	UPROPERTY(Transient)
	TObjectPtr<UWidgetSwitcher> FuncModeSwitcher;
	UPROPERTY(Transient)
	TObjectPtr<UButton> FuncTabChannelsBtn;
	UPROPERTY(Transient)
	TObjectPtr<UButton> FuncTabPityShopBtn;
	UPROPERTY(Transient)
	TObjectPtr<UButton> FuncTabSettingsBtn;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ChannelHeaderInfoText;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ChannelRefreshButton;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ChannelListVBox;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PityHeaderInfoText;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> PityShopListVBox;
	UPROPERTY(Transient)
	TObjectPtr<UButton> FuncDebugButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> FuncRotModeButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> FuncPvPButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> FuncAuthButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> FuncRefreshButton;

	// Bottom Currency Footer & Feedback
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FooterGoldText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FooterPotentialText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FooterOreText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FooterTokenText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FeedbackStatusText;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UIU_InventoryActionProxy>> ActiveActionProxies;

	FSlateBrush CardOuterBrush;
	FSlateBrush HeaderBannerBrush;
	FSlateBrush CloseRedBtnBrush;
	FSlateBrush ActiveTabGreenBrush;
	FSlateBrush InactiveTabCreamBrush;
	FSlateBrush EquipOccupiedSlotBrush;
	FSlateBrush EquipEmptySlotBrush;
	FSlateBrush BagCellSelectedBrush;
	FSlateBrush BagCellNormalBrush;

	EIU_CharacterProfileTab CurrentProfileTab = EIU_CharacterProfileTab::HanhTrang;
	FString SelectedItemInstanceID;
	FString SelectedEquippedSlotID;
	int32 SelectedBagSlotIndex = -1;

	FIU_InventorySnapshot DisplayedSnapshot;
	bool bHasSnapshotData = false;
	FIU_GuildSnapshot DisplayedGuildSnapshot;
	bool bHasGuildSnapshotData = false;
	FIU_QuestSnapshot DisplayedQuestSnapshot;
	bool bHasQuestSnapshotData = false;
	TArray<FIU_ZoneChannelView> DisplayedWorldChannels;
	bool bHasWorldChannelData = false;
	FIU_PityShopSnapshot DisplayedPityShopSnapshot;
	bool bHasPityShopData = false;
	int32 CurrentFuncSubMode = 0;

	TWeakObjectPtr<UIU_AttributeComponent> BoundAttr;
	TWeakObjectPtr<AIU_PlayerState> BoundPS;
};
