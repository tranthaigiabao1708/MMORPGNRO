// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/IU_GameplayTypes.h"
#include "Network/IU_BackendSubsystem.h"
#include "Styling/SlateBrush.h"
#include "IU_PlayerHUDWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UBorder;
class UButton;
class UEditableTextBox;
class UIU_AttributeComponent;
class UIU_CombatComponent;
class UIU_FlightComponent;
class AIU_PlayerState;

/**
 * Event-driven NRO Action RPG Outer Gameplay HUD Widget:
 * - Top-Left: Level / Suc Manh bar, HP bar, Ki bar, Tiem Nang / Suc Danh / Giap / Vang summary, and "[Tab] Ho So" shortcut pill.
 * - Bottom-Left (Hub O Skill - NRO Layout): 5 horizontal skill slots [1..5] + slot [6] stacked above slot [1],
 *   with vibrant green active border on the selected slot and golden-orange frames on others.
 * - Bottom-Right (Hub Dau Than - NRO Layout): Circular glossy orange medallion with a crisp white heart in the center,
 *   coral-red remaining Dau Than stack count inside the heart, and live 3.0s cooldown indicator.
 */
UCLASS()
class INFINITEUNIVERSE_API UIU_PlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Binds read-only event listeners to the player's components & replicated PlayerState. */
	UFUNCTION(BlueprintCallable, Category = "IU|UI")
	void BindToPlayerPawn(APawn* NewPawn);

	/** Ensures late-replicated AIU_PlayerState on NM_Client is bound immediately when it arrives. */
	void EnsurePlayerStateBound(APawn* CurrentPawn);

	/** Highlights the active skill slot (1..6) on the bottom-left NRO Skill Hub with a green frame. */
	UFUNCTION(BlueprintCallable, Category = "IU|UI")
	void SetActiveSkillSlot(int32 SlotNumber);

	UFUNCTION(BlueprintPure, Category = "IU|UI")
	int32 GetActiveSkillSlot() const { return ActiveSkillSlotNumber; }

	UFUNCTION(BlueprintPure, Category = "IU|UI")
	int32 GetDisplayedElixirCount() const { return DisplayedElixirCount; }

	/** Starts a local visual countdown on the bottom-right Dau Than Hub when triggered. */
	void NotifyElixirCooldownStarted(float DurationSeconds = 3.0f);

	// ========================================================================
	// Step 5 Party Frame, Invite Banners & Bottom-Center NRO Chat Box
	// ========================================================================

	void ApplyPartySnapshot(const FIU_PartySnapshot& Snapshot);
	void ApplyTradeInviteBanner(const FIU_TradeSessionSnapshot& Snapshot);
	void AppendChatMessage(const FIU_ChatMessageView& Message);
	void FocusOrSubmitChatBar();
	const FString& GetSelectedChatChannel() const { return SelectedChatChannel; }

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "IU|UI")
	TObjectPtr<UProgressBar> HealthProgressBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "IU|UI")
	TObjectPtr<UProgressBar> KiProgressBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "IU|UI")
	TObjectPtr<UProgressBar> EXPProgressBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "IU|UI")
	TObjectPtr<UTextBlock> HealthValueText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "IU|UI")
	TObjectPtr<UTextBlock> KiValueText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "IU|UI")
	TObjectPtr<UTextBlock> LevelEXPValueText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "IU|UI")
	TObjectPtr<UTextBlock> PotentialValueText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "IU|UI")
	TObjectPtr<UTextBlock> StatusReadoutText;

private:
	UFUNCTION()
	void HandleHPChanged(float CurrentHP, float MaxHP, float Delta);

	UFUNCTION()
	void HandleKiChanged(float CurrentKi, float MaxKi, float Delta);

	UFUNCTION()
	void HandleAttackPowerChanged(float NewAttackPower);

	UFUNCTION()
	void HandleDefenseChanged(float NewDefense);

	UFUNCTION()
	void HandleCharacterEXPChanged(int64 CurrentEXP, int64 NextLevelEXP, int32 CurrentLevel);

	UFUNCTION()
	void HandleCharacterPotentialChanged(int64 PotentialPoints, int64 NextHPCost, int64 NextKiCost, int64 NextAtkCost);

	UFUNCTION()
	void HandleCharacterEconomyChanged(int64 StandardCoin, int32 ElixirCount, int32 CrimsonOreCount, int32 EquippedCount);

	UFUNCTION()
	void HandleCharacterSocialChanged(const FString& GuildTag, const FString& GuildName, const FString& PartyID);

	UFUNCTION()
	void HandleCombatStateChanged(EIU_CombatState OldState, EIU_CombatState NewState);

	UFUNCTION()
	void HandleComboStepExecuted(int32 ComboStep);

	UFUNCTION()
	void HandleFlightStateChanged(bool bIsFlying);

	UFUNCTION()
	void HandleMobilityStyleChanged(EIU_MobilityStepStyle NewStyle);

	UFUNCTION()
	void HandleSkillSlot1Clicked();
	UFUNCTION()
	void HandleSkillSlot2Clicked();
	UFUNCTION()
	void HandleSkillSlot3Clicked();
	UFUNCTION()
	void HandleSkillSlot4Clicked();
	UFUNCTION()
	void HandleSkillSlot5Clicked();
	UFUNCTION()
	void HandleSkillSlot6Clicked();
	UFUNCTION()
	void HandleElixirHubClicked();
	UFUNCTION()
	void HandleOpenProfilePillClicked();
	UFUNCTION()
	void HandlePartyActionBtnClicked();
	UFUNCTION()
	void HandleCycleChatChannelClicked();
	UFUNCTION()
	void HandleSendChatClicked();
	UFUNCTION()
	void HandleChatTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	void DispatchSkillSlotClick(int32 SlotNumber);
	void UpdateSkillSlotFrameColors();
	void UpdateElixirHubVisuals();
	void TickElixirCooldownLabel();
	void UnbindCurrentComponents();
	void RefreshStatusText();
	void RefreshPotentialReadout();
	void RebuildPartyOverlayText();
	void RebuildChatHistoryText();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> SkillSlotOuterFrames; // Index 0..5 -> Slots 1..6

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> SkillSlotButtons; // Index 0..5 -> Slots 1..6

	UPROPERTY(Transient)
	TObjectPtr<UBorder> ElixirCircleBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ElixirHubCountText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ElixirCooldownText;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ElixirHubButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> OpenProfileButton;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> PartyOverlayBorder;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PartyOverlayText;
	UPROPERTY(Transient)
	TObjectPtr<UButton> PartyQuickActionBtn;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PartyQuickActionLbl;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ChatHistoryText;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ChatChannelButton;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ChatChannelLabel;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> ChatInputBox;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ChatSendButton;

	FSlateBrush ActiveSkillFrameBrush;
	FSlateBrush InactiveSkillFrameBrush;
	FSlateBrush ElixirReadyCircleBrush;
	FSlateBrush ElixirCooldownCircleBrush;
	FSlateBrush WhiteHeartLobeBrush;
	FSlateBrush SocialOverlayBrush;

	FIU_PartySnapshot DisplayedParty;
	FIU_TradeSessionSnapshot DisplayedTradeInvite;
	TArray<FString> RecentChatLines;
	FString SelectedChatChannel = TEXT("LOCAL");

	TWeakObjectPtr<UIU_AttributeComponent> BoundAttributeComp;
	TWeakObjectPtr<UIU_CombatComponent> BoundCombatComp;
	TWeakObjectPtr<UIU_FlightComponent> BoundFlightComp;
	TWeakObjectPtr<AIU_PlayerState> BoundPlayerState;

	int32 ActiveSkillSlotNumber = 1;
	int32 DisplayedElixirCount = 0;
	double ElixirCooldownEndWorldTime = 0.0;
	FTimerHandle ElixirCooldownVisualTimer;

	int32 DisplayedComboStep = 0;
	bool bDisplayedFlying = false;
	EIU_CombatState DisplayedCombatState = EIU_CombatState::Idle;
	EIU_MobilityStepStyle DisplayedMobilityStyle = EIU_MobilityStepStyle::Dash;
};
