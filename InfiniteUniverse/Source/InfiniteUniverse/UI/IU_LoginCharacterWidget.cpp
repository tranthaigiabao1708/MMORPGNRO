// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "UI/IU_LoginCharacterWidget.h"
#include "Network/IU_BackendSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

namespace
{
	static const TCHAR* Step3RaceIDs[] = {
		TEXT("race.zenith_warrior"),
		TEXT("race.terran_adept"),
		TEXT("race.mystic_elder"),
		TEXT("race.frost_archon")
	};

	static const TCHAR* Step3RaceLabels[] = {
		TEXT("Race: Zenith Warrior (+Atk Scaling)"),
		TEXT("Race: Terran Adept (Balanced)"),
		TEXT("Race: Mystic Elder (+Ki Pool)"),
		TEXT("Race: Frost Archon (+Vitality)")
	};
}

TSharedRef<SWidget> UIU_LoginCharacterWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("AuthRootCanvas"));
		WidgetTree->RootWidget = RootCanvas;

		UBorder* ModalCard = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("AuthModalCard"));
		ModalCard->SetBrushColor(FLinearColor(0.02f, 0.04f, 0.09f, 0.94f));
		ModalCard->SetPadding(FMargin(24.0f, 20.0f));
		if (UCanvasPanelSlot* CardSlot = RootCanvas->AddChildToCanvas(ModalCard))
		{
			CardSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CardSlot->SetPosition(FVector2D(0.0f, 0.0f));
			CardSlot->SetSize(FVector2D(460.0f, 390.0f));
		}

		UVerticalBox* VBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AuthVBox"));
		ModalCard->SetContent(VBox);

		UTextBlock* TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AuthTitle"));
		TitleText->SetText(FText::FromString(TEXT("INFINITE UNIVERSE — STEP 3 GATEWAY LOGIN")));
		TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.92f, 1.0f)));
		if (UVerticalBoxSlot* S = VBox->AddChildToVerticalBox(TitleText))
		{
			S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
		}

		UsernameInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("UsernameInput"));
		UsernameInput->SetHintText(FText::FromString(TEXT("Account Username (3..32 chars)")));
		UsernameInput->SetText(FText::FromString(TEXT("HeroWarrior")));
		if (UVerticalBoxSlot* S = VBox->AddChildToVerticalBox(UsernameInput))
		{
			S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		}

		PasswordInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("PasswordInput"));
		PasswordInput->SetHintText(FText::FromString(TEXT("Password (Argon2id Protected)")));
		PasswordInput->SetIsPassword(true);
		PasswordInput->SetText(FText::FromString(TEXT("Step3HeroPass!2026")));
		if (UVerticalBoxSlot* S = VBox->AddChildToVerticalBox(PasswordInput))
		{
			S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		}

		CharacterNameInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("CharacterNameInput"));
		CharacterNameInput->SetHintText(FText::FromString(TEXT("Character Name (Created automatically if new)")));
		CharacterNameInput->SetText(FText::FromString(TEXT("ZenithHero")));
		if (UVerticalBoxSlot* S = VBox->AddChildToVerticalBox(CharacterNameInput))
		{
			S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
		}

		CycleRaceButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CycleRaceBtn"));
		SelectedRaceText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SelectedRaceText"));
		SelectedRaceText->SetText(FText::FromString(Step3RaceLabels[0]));
		CycleRaceButton->SetContent(SelectedRaceText);
		if (UVerticalBoxSlot* S = VBox->AddChildToVerticalBox(CycleRaceButton))
		{
			S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
		}

		UHorizontalBox* ActionRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ActionRow"));
		if (UVerticalBoxSlot* S = VBox->AddChildToVerticalBox(ActionRow))
		{
			S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
		}

		LoginEnterButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("LoginEnterBtn"));
		UTextBlock* LoginLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LoginLbl"));
		LoginLbl->SetText(FText::FromString(TEXT(" LOGIN & ENTER ")));
		LoginEnterButton->SetContent(LoginLbl);
		if (UHorizontalBoxSlot* HS = ActionRow->AddChildToHorizontalBox(LoginEnterButton))
		{
			HS->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
		}

		RegisterEnterButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("RegisterEnterBtn"));
		UTextBlock* RegLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RegLbl"));
		RegLbl->SetText(FText::FromString(TEXT(" REGISTER NEW ")));
		RegisterEnterButton->SetContent(RegLbl);
		if (UHorizontalBoxSlot* HS = ActionRow->AddChildToHorizontalBox(RegisterEnterButton))
		{
			HS->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
		}

		CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseAuthBtn"));
		UTextBlock* CloseLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CloseLbl"));
		CloseLbl->SetText(FText::FromString(TEXT(" CLOSE (F5) ")));
		CloseButton->SetContent(CloseLbl);
		ActionRow->AddChildToHorizontalBox(CloseButton);

		StatusBannerText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusBannerText"));
		StatusBannerText->SetText(FText::FromString(TEXT("Ready — Gateway http://127.0.0.1:8080 -> Dedicated Server :7777")));
		StatusBannerText->SetColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.92f, 1.0f)));
		VBox->AddChildToVerticalBox(StatusBannerText);
	}

	return Super::RebuildWidget();
}

void UIU_LoginCharacterWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (LoginEnterButton)
	{
		LoginEnterButton->OnClicked.AddUniqueDynamic(this, &UIU_LoginCharacterWidget::HandleQuickLoginAndEnterClicked);
	}
	if (RegisterEnterButton)
	{
		RegisterEnterButton->OnClicked.AddUniqueDynamic(this, &UIU_LoginCharacterWidget::HandleRegisterAndEnterClicked);
	}
	if (CycleRaceButton)
	{
		CycleRaceButton->OnClicked.AddUniqueDynamic(this, &UIU_LoginCharacterWidget::HandleCycleRaceClicked);
	}
	if (CloseButton)
	{
		CloseButton->OnClicked.AddUniqueDynamic(this, &UIU_LoginCharacterWidget::HandleClosePanelClicked);
	}
}

void UIU_LoginCharacterWidget::SetStatusText(const FString& InMessage, const FLinearColor& InColor)
{
	if (StatusBannerText)
	{
		StatusBannerText->SetText(FText::FromString(InMessage));
		StatusBannerText->SetColorAndOpacity(FSlateColor(InColor));
	}
}

void UIU_LoginCharacterWidget::HandleCycleRaceClicked()
{
	SelectedRaceIndex = (SelectedRaceIndex + 1) % UE_ARRAY_COUNT(Step3RaceIDs);
	if (SelectedRaceText)
	{
		SelectedRaceText->SetText(FText::FromString(Step3RaceLabels[SelectedRaceIndex]));
	}
}

void UIU_LoginCharacterWidget::HandleQuickLoginAndEnterClicked()
{
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend)
	{
		return;
	}

	const FString User = UsernameInput ? UsernameInput->GetText().ToString() : TEXT("HeroWarrior");
	const FString Pass = PasswordInput ? PasswordInput->GetText().ToString() : TEXT("Step3HeroPass!2026");
	const FString CharName = CharacterNameInput ? CharacterNameInput->GetText().ToString() : User;
	const FString RaceID = Step3RaceIDs[SelectedRaceIndex];

	SetStatusText(TEXT("Authenticating with Go Edge Gateway..."), FLinearColor(1.0f, 0.88f, 0.30f));
	TWeakObjectPtr<UIU_LoginCharacterWidget> WeakThis(this);

	Backend->QuickAuthenticateAndEnterWorld(User, Pass, CharName, RaceID, GetOwningPlayer(),
		[WeakThis](bool bSuccess, const FString& Msg)
		{
			if (UIU_LoginCharacterWidget* Self = WeakThis.Get())
			{
				Self->SetStatusText(Msg, bSuccess ? FLinearColor(0.28f, 1.0f, 0.52f) : FLinearColor(1.0f, 0.30f, 0.25f));
				if (bSuccess)
				{
					Self->HandleClosePanelClicked();
				}
			}
		});
}

void UIU_LoginCharacterWidget::HandleRegisterAndEnterClicked()
{
	HandleQuickLoginAndEnterClicked();
}

void UIU_LoginCharacterWidget::HandleClosePanelClicked()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->bShowMouseCursor = false;
		FInputModeGameOnly GameInput;
		PC->SetInputMode(GameInput);
	}
	SetVisibility(ESlateVisibility::Collapsed);
}
