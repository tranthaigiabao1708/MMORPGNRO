// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "IU_LoginCharacterWidget.generated.h"

class UEditableTextBox;
class UTextBlock;
class UButton;

/**
 * Step 3 Client Login / Register / Character Creation & World Entry UMG Modal (Toggle with F5 or -ShowLoginUI).
 * Builds its Slate/UMG hierarchy programmatically so zero binary .uasset designer files are required.
 */
UCLASS()
class INFINITEUNIVERSE_API UIU_LoginCharacterWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void HandleQuickLoginAndEnterClicked();

	UFUNCTION()
	void HandleRegisterAndEnterClicked();

	UFUNCTION()
	void HandleCycleRaceClicked();

	UFUNCTION()
	void HandleClosePanelClicked();

	void SetStatusText(const FString& InMessage, const FLinearColor& InColor);

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> UsernameInput;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> PasswordInput;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> CharacterNameInput;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SelectedRaceText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusBannerText;

	UPROPERTY(Transient)
	TObjectPtr<UButton> LoginEnterButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> RegisterEnterButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> CycleRaceButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> CloseButton;

	int32 SelectedRaceIndex = 0;
};
