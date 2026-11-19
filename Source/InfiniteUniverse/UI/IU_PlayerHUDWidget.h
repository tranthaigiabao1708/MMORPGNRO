// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_PlayerHUDWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UIU_AttributeComponent;
class UIU_CombatComponent;
class UIU_FlightComponent;

/**
 * Event-driven UMG HUD Widget displaying Player HP, Ki, Combo Step, and Flight Mode (Zero-Tick).
 * Builds a clean default Slate/UMG hierarchy automatically if no Blueprint Designer tree is bound,
 * and NEVER mutates gameplay attributes.
 */
UCLASS()
class INFINITEUNIVERSE_API UIU_PlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Binds read-only event listeners to the player's components. */
	UFUNCTION(BlueprintCallable, Category = "IU|UI")
	void BindToPlayerPawn(APawn* NewPawn);

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "IU|UI")
	TObjectPtr<UProgressBar> HealthProgressBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "IU|UI")
	TObjectPtr<UProgressBar> KiProgressBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "IU|UI")
	TObjectPtr<UTextBlock> HealthValueText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "IU|UI")
	TObjectPtr<UTextBlock> KiValueText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "IU|UI")
	TObjectPtr<UTextBlock> StatusReadoutText;

private:
	UFUNCTION()
	void HandleHPChanged(float CurrentHP, float MaxHP, float Delta);

	UFUNCTION()
	void HandleKiChanged(float CurrentKi, float MaxKi, float Delta);

	UFUNCTION()
	void HandleCombatStateChanged(EIU_CombatState OldState, EIU_CombatState NewState);

	UFUNCTION()
	void HandleComboStepExecuted(int32 ComboStep);

	UFUNCTION()
	void HandleFlightStateChanged(bool bIsFlying);

	void UnbindCurrentComponents();
	void RefreshStatusText();

	TWeakObjectPtr<UIU_AttributeComponent> BoundAttributeComp;
	TWeakObjectPtr<UIU_CombatComponent> BoundCombatComp;
	TWeakObjectPtr<UIU_FlightComponent> BoundFlightComp;

	int32 DisplayedComboStep = 0;
	bool bDisplayedFlying = false;
	EIU_CombatState DisplayedCombatState = EIU_CombatState::Idle;
};
