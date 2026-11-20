// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_PotentialUpgradeWidget.generated.h"

class UTextBlock;
class UButton;
class UIU_AttributeComponent;
class AIU_PlayerState;

/**
 * Step 3 NRO-Style Potential & Stat Allocation UMG Modal (Toggle with P, Quick Keys 1 / 2 / 3).
 * - Displays Level & LV EXP (which determine Level ONLY and never auto-increase combat stats).
 * - Displays accumulated Potential Points (Diem Tiem Nang) earned from defeating monsters.
 * - Allows players to spend Potential Points on MaxHP (+100), MaxKi (+100), or AttackPower (+10 Suc Danh)
 *   with independent escalating Potential costs per stat track.
 */
UCLASS()
class INFINITEUNIVERSE_API UIU_PotentialUpgradeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Binds read-only UI listeners to the owning player's Pawn & AIU_PlayerState. */
	UFUNCTION(BlueprintCallable, Category = "IU|UI")
	void RefreshBindings();

private:
	UFUNCTION()
	void HandleUpgradeHPClicked();

	UFUNCTION()
	void HandleUpgradeKiClicked();

	UFUNCTION()
	void HandleUpgradeAtkClicked();

	UFUNCTION()
	void HandleCloseClicked();

	UFUNCTION()
	void HandleHPChanged(float CurrentHP, float MaxHP, float Delta);

	UFUNCTION()
	void HandleKiChanged(float CurrentKi, float MaxKi, float Delta);

	UFUNCTION()
	void HandleAttackPowerChanged(float NewAttackPower);

	UFUNCTION()
	void HandleCharacterEXPChanged(int64 CurrentEXP, int64 NextLevelEXP, int32 CurrentLevel);

	UFUNCTION()
	void HandleCharacterPotentialChanged(int64 PotentialPoints, int64 NextHPCost, int64 NextKiCost, int64 NextAtkCost);

	void RequestStatUpgrade(EIU_PotentialStatType StatType);
	void UnbindAll();
	void UpdateAllLabels();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LevelSummaryText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PotentialPoolText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HPStatText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> KiStatText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> AtkStatText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> UpgradeHPBtnLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> UpgradeKiBtnLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> UpgradeAtkBtnLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FeedbackStatusText;

	UPROPERTY(Transient)
	TObjectPtr<UButton> UpgradeHPButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> UpgradeKiButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> UpgradeAtkButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ClosePanelButton;

	TWeakObjectPtr<UIU_AttributeComponent> BoundAttr;
	TWeakObjectPtr<AIU_PlayerState> BoundPS;
};
