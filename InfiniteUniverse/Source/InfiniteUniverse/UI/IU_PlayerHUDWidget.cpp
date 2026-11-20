// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "UI/IU_PlayerHUDWidget.h"
#include "Core/IU_PlayerController.h"
#include "Core/IU_PlayerState.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Combat/IU_CombatComponent.h"
#include "Movement/IU_FlightComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Fonts/SlateFontInfo.h"
#include "Engine/World.h"
#include "TimerManager.h"

TSharedRef<SWidget> UIU_PlayerHUDWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// Pre-build rounded NRO Slate brushes for Skill Hub & Circular Dau Than Medallion
		ActiveSkillFrameBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.14f, 0.86f, 0.42f, 1.0f),
			7.0f,
			FLinearColor(0.03f, 0.25f, 0.10f, 0.96f),
			2.0f);
		InactiveSkillFrameBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.92f, 0.50f, 0.05f, 1.0f),
			7.0f,
			FLinearColor(0.24f, 0.11f, 0.02f, 0.96f),
			2.0f);
		ElixirReadyCircleBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.96f, 0.54f, 0.06f, 1.0f),
			34.0f,
			FLinearColor(0.28f, 0.13f, 0.02f, 0.96f),
			2.5f);
		ElixirCooldownCircleBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.42f, 0.28f, 0.14f, 0.92f),
			34.0f,
			FLinearColor(0.15f, 0.08f, 0.03f, 0.96f),
			2.5f);
		WhiteHeartLobeBrush = FSlateRoundedBoxBrush(
			FLinearColor(1.0f, 1.0f, 1.0f, 1.0f),
			9.5f);
		SocialOverlayBrush = FSlateRoundedBoxBrush(
			FLinearColor(0.06f, 0.08f, 0.12f, 0.80f),
			6.0f,
			FLinearColor(0.86f, 0.54f, 0.14f, 0.90f),
			1.4f);

		UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
		WidgetTree->RootWidget = RootCanvas;

		auto StyleHUDText = [](UTextBlock* TB, const FLinearColor& Col, int32 FontPt = 12, bool bShadow = true)
		{
			if (!TB)
			{
				return;
			}
			FSlateFontInfo F = TB->GetFont();
			F.Size = FontPt;
			TB->SetFont(F);
			TB->SetColorAndOpacity(FSlateColor(Col));
			if (bShadow)
			{
				TB->SetShadowOffset(FVector2D(1.4f, 1.4f));
				TB->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.92f));
			}
			else
			{
				TB->SetShadowOffset(FVector2D::ZeroVector);
			}
		};

		// ====================================================================
		// 1. TOP-LEFT VITALS & NRO PROFILE PILL ([Tab] Ho So)
		// ====================================================================
		UVerticalBox* VitalsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("VitalsBox"));
		if (UCanvasPanelSlot* BoxSlot = RootCanvas->AddChildToCanvas(VitalsBox))
		{
			BoxSlot->SetAnchors(FAnchors(0.0f, 0.0f));
			BoxSlot->SetPosition(FVector2D(28.0f, 14.0f));
			BoxSlot->SetSize(FVector2D(445.0f, 210.0f));
		}

		UHorizontalBox* TopNameRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TopNameRow"));
		VitalsBox->AddChildToVerticalBox(TopNameRow);

		LevelEXPValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LevelEXPValueText"));
		StyleHUDText(LevelEXPValueText, FLinearColor(1.0f, 0.88f, 0.32f), 12);
		LevelEXPValueText->SetText(FText::FromString(TEXT("LV. 1  |  SUC MANH: 0 / 100")));
		if (UHorizontalBoxSlot* LvSlot = TopNameRow->AddChildToHorizontalBox(LevelEXPValueText))
		{
			LvSlot->SetVerticalAlignment(VAlign_Center);
			LvSlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
		}

		OpenProfileButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("OpenProfileButton"));
		OpenProfileButton->SetBackgroundColor(FLinearColor(0.86f, 0.52f, 0.12f, 0.95f));
		UTextBlock* ProfBtnLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OpenProfileBtnLbl"));
		ProfBtnLbl->SetText(FText::FromString(TEXT(" [TAB] HO SO ")));
		StyleHUDText(ProfBtnLbl, FLinearColor(1.0f, 0.98f, 0.88f), 9);
		OpenProfileButton->SetContent(ProfBtnLbl);
		if (UHorizontalBoxSlot* ProfSlot = TopNameRow->AddChildToHorizontalBox(OpenProfileButton))
		{
			ProfSlot->SetVerticalAlignment(VAlign_Center);
		}

		EXPProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("EXPProgressBar"));
		EXPProgressBar->SetFillColorAndOpacity(FLinearColor(0.95f, 0.75f, 0.15f, 1.0f));
		if (UVerticalBoxSlot* EXPSlot = VitalsBox->AddChildToVerticalBox(EXPProgressBar))
		{
			EXPSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 4.0f));
		}

		HealthValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HealthValueText"));
		StyleHUDText(HealthValueText, FLinearColor(0.95f, 1.0f, 0.95f), 12);
		VitalsBox->AddChildToVerticalBox(HealthValueText);

		HealthProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HealthProgressBar"));
		HealthProgressBar->SetFillColorAndOpacity(FLinearColor(0.18f, 0.85f, 0.32f, 1.0f));
		if (UVerticalBoxSlot* HPSlot = VitalsBox->AddChildToVerticalBox(HealthProgressBar))
		{
			HPSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 4.0f));
		}

		KiValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("KiValueText"));
		StyleHUDText(KiValueText, FLinearColor(0.75f, 0.95f, 1.0f), 12);
		VitalsBox->AddChildToVerticalBox(KiValueText);

		KiProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("KiProgressBar"));
		KiProgressBar->SetFillColorAndOpacity(FLinearColor(0.10f, 0.68f, 1.0f, 1.0f));
		if (UVerticalBoxSlot* KiSlot = VitalsBox->AddChildToVerticalBox(KiProgressBar))
		{
			KiSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 4.0f));
		}

		PotentialValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PotentialValueText"));
		StyleHUDText(PotentialValueText, FLinearColor(0.35f, 1.0f, 0.72f), 11);
		PotentialValueText->SetText(FText::FromString(TEXT("TN: 0 | SD: 100 | GIAP: 20 | VANG: 0")));
		if (UVerticalBoxSlot* PotSlot = VitalsBox->AddChildToVerticalBox(PotentialValueText))
		{
			PotSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
		}

		StatusReadoutText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusReadoutText"));
		StyleHUDText(StatusReadoutText, FLinearColor(1.0f, 0.88f, 0.35f), 11);
		VitalsBox->AddChildToVerticalBox(StatusReadoutText);

		// ====================================================================
		// 2. BOTTOM-LEFT NRO SKILL HUB (Slots 1..5 horizontal + Slot 6 above Slot 1 — Image 2!)
		// ====================================================================
		SkillSlotOuterFrames.SetNum(6);
		SkillSlotButtons.SetNum(6);

		struct FIU_SkillIconSpec
		{
			int32 SlotNum;
			const TCHAR* GlyphTop;
			const TCHAR* GlyphSub;
			FLinearColor BgColor;
			FLinearColor CoreColor;
		};
		static const FIU_SkillIconSpec GSkillSpecs[6] = {
			{ 1, TEXT("DAM"),  TEXT("COMBO"), FLinearColor(0.08f, 0.58f, 0.92f, 1.0f), FLinearColor(0.95f, 0.98f, 1.00f, 0.90f) },
			{ 2, TEXT("KI"),   TEXT("15 KI"), FLinearColor(0.04f, 0.42f, 0.90f, 1.0f), FLinearColor(0.38f, 0.92f, 1.00f, 0.95f) },
			{ 3, TEXT("NE"),   TEXT("ANH"),   FLinearColor(0.82f, 0.24f, 0.08f, 1.0f), FLinearColor(1.00f, 0.90f, 0.32f, 0.95f) },
			{ 4, TEXT("TOC"),  TEXT("BIEN"),  FLinearColor(0.86f, 0.66f, 0.08f, 1.0f), FLinearColor(1.00f, 1.00f, 0.82f, 0.95f) },
			{ 5, TEXT("RADA"), TEXT("KHOA"),  FLinearColor(0.58f, 0.08f, 0.08f, 1.0f), FLinearColor(0.32f, 0.96f, 0.28f, 0.95f) },
			{ 6, TEXT("BAY"),  TEXT("3D"),    FLinearColor(0.06f, 0.52f, 0.96f, 1.0f), FLinearColor(0.75f, 0.96f, 1.00f, 0.95f) }
		};

		auto BuildSkillBoxWidget = [&](const FIU_SkillIconSpec& Spec) -> USizeBox*
		{
			const int32 ArrIdx = Spec.SlotNum - 1;
			USizeBox* OuterSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			OuterSize->SetWidthOverride(48.0f);
			OuterSize->SetHeightOverride(48.0f);

			UBorder* FrameBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			FrameBorder->SetBrush(Spec.SlotNum == ActiveSkillSlotNumber ? ActiveSkillFrameBrush : InactiveSkillFrameBrush);
			FrameBorder->SetPadding(FMargin(4.0f));
			OuterSize->SetContent(FrameBorder);
			SkillSlotOuterFrames[ArrIdx] = FrameBorder;

			UBorder* IconBg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			IconBg->SetBrushColor(Spec.BgColor);
			IconBg->SetPadding(FMargin(0.0f));
			FrameBorder->SetContent(IconBg);

			UCanvasPanel* IconCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
			IconBg->SetContent(IconCanvas);

			// Stylized NRO gloss highlight strip along top of icon frame
			UBorder* TopGloss = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			TopGloss->SetBrushColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.32f));
			if (UCanvasPanelSlot* GS = IconCanvas->AddChildToCanvas(TopGloss))
			{
				GS->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 0.0f));
				GS->SetOffsets(FMargin(2.0f, 2.0f, 2.0f, 5.0f));
			}

			// Inner energy core diamond/streak
			UBorder* CoreGlow = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			CoreGlow->SetBrushColor(FLinearColor(Spec.CoreColor.R, Spec.CoreColor.G, Spec.CoreColor.B, 0.28f));
			CoreGlow->SetRenderTransformAngle(45.0f);
			if (UCanvasPanelSlot* CS = IconCanvas->AddChildToCanvas(CoreGlow))
			{
				CS->SetAnchors(FAnchors(0.5f, 0.5f));
				CS->SetAlignment(FVector2D(0.5f, 0.5f));
				CS->SetSize(FVector2D(22.0f, 22.0f));
			}

			UVerticalBox* TxtStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
			if (UCanvasPanelSlot* TS = IconCanvas->AddChildToCanvas(TxtStack))
			{
				TS->SetAnchors(FAnchors(0.5f, 0.5f));
				TS->SetAlignment(FVector2D(0.5f, 0.5f));
				TS->SetAutoSize(true);
			}

			UTextBlock* TopGlyph = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			TopGlyph->SetText(FText::FromString(Spec.GlyphTop));
			TopGlyph->SetJustification(ETextJustify::Center);
			StyleHUDText(TopGlyph, FLinearColor(1.0f, 0.99f, 0.92f), 9);
			TxtStack->AddChildToVerticalBox(TopGlyph);

			UTextBlock* SubGlyph = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			SubGlyph->SetText(FText::FromString(Spec.GlyphSub));
			SubGlyph->SetJustification(ETextJustify::Center);
			StyleHUDText(SubGlyph, Spec.CoreColor, 7);
			TxtStack->AddChildToVerticalBox(SubGlyph);

			// Transparent full-slot clickable UButton overlay
			UButton* ClickBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
			ClickBtn->SetBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
			if (UCanvasPanelSlot* BS = IconCanvas->AddChildToCanvas(ClickBtn))
			{
				BS->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
				BS->SetOffsets(FMargin(0.0f));
			}
			SkillSlotButtons[ArrIdx] = ClickBtn;

			return OuterSize;
		};

		UVerticalBox* SkillHubRoot = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SkillHubRoot"));
		if (UCanvasPanelSlot* HubCanvasSlot = RootCanvas->AddChildToCanvas(SkillHubRoot))
		{
			HubCanvasSlot->SetAnchors(FAnchors(0.0f, 1.0f));
			HubCanvasSlot->SetAlignment(FVector2D(0.0f, 1.0f));
			HubCanvasSlot->SetPosition(FVector2D(28.0f, -12.0f));
			HubCanvasSlot->SetAutoSize(true);
		}

		// Upper Row: Slot 6 stacked directly above Slot 1 (exact NRO Image 2 layout!)
		UVerticalBox* Slot6Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Slot6Column"));
		if (UVerticalBoxSlot* S6Wrap = SkillHubRoot->AddChildToVerticalBox(Slot6Col))
		{
			S6Wrap->SetHorizontalAlignment(HAlign_Left);
			S6Wrap->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		}

		UTextBlock* Key6Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Key6Label"));
		Key6Label->SetText(FText::FromString(TEXT("6")));
		Key6Label->SetJustification(ETextJustify::Center);
		StyleHUDText(Key6Label, FLinearColor::White, 11);
		if (UVerticalBoxSlot* K6S = Slot6Col->AddChildToVerticalBox(Key6Label))
		{
			K6S->SetHorizontalAlignment(HAlign_Center);
			K6S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
		}
		Slot6Col->AddChildToVerticalBox(BuildSkillBoxWidget(GSkillSpecs[5]));

		// Lower Row: Slots 1, 2, 3, 4, 5 horizontally with numbers 1..5 underneath
		UHorizontalBox* BottomSkillsRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BottomSkillsRow"));
		SkillHubRoot->AddChildToVerticalBox(BottomSkillsRow);

		for (int32 i = 0; i < 5; ++i)
		{
			UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
			Col->AddChildToVerticalBox(BuildSkillBoxWidget(GSkillSpecs[i]));

			UTextBlock* NumLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			NumLbl->SetText(FText::FromString(FString::Printf(TEXT("%d"), i + 1)));
			NumLbl->SetJustification(ETextJustify::Center);
			StyleHUDText(NumLbl, FLinearColor::White, 11);
			if (UVerticalBoxSlot* NS = Col->AddChildToVerticalBox(NumLbl))
			{
				NS->SetHorizontalAlignment(HAlign_Center);
				NS->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
			}

			if (UHorizontalBoxSlot* HS = BottomSkillsRow->AddChildToHorizontalBox(Col))
			{
				HS->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
			}
		}

		// ====================================================================
		// 3. BOTTOM-RIGHT DAU THAN HUB (Circular Orange Button + White Heart + Count — Image 3!)
		// ====================================================================
		UVerticalBox* ElixirHubWrap = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ElixirHubWrap"));
		if (UCanvasPanelSlot* ElixirSlot = RootCanvas->AddChildToCanvas(ElixirHubWrap))
		{
			ElixirSlot->SetAnchors(FAnchors(1.0f, 1.0f));
			ElixirSlot->SetAlignment(FVector2D(1.0f, 1.0f));
			ElixirSlot->SetPosition(FVector2D(-26.0f, -12.0f));
			ElixirSlot->SetAutoSize(true);
		}

		USizeBox* CircleSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ElixirCircleSize"));
		CircleSize->SetWidthOverride(68.0f);
		CircleSize->SetHeightOverride(68.0f);
		ElixirHubWrap->AddChildToVerticalBox(CircleSize);

		ElixirCircleBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ElixirCircleBorder"));
		ElixirCircleBorder->SetBrush(ElixirReadyCircleBrush);
		ElixirCircleBorder->SetPadding(FMargin(0.0f));
		CircleSize->SetContent(ElixirCircleBorder);

		UCanvasPanel* CircleCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ElixirCircleCanvas"));
		ElixirCircleBorder->SetContent(CircleCanvas);

		// Crisp Geometric White Heart centered inside the orange medallion (2 rounded lobes + 1 rotated diamond point)
		UBorder* HeartLeftLobe = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HeartLeftLobe"));
		HeartLeftLobe->SetBrush(WhiteHeartLobeBrush);
		if (UCanvasPanelSlot* LSlot = CircleCanvas->AddChildToCanvas(HeartLeftLobe))
		{
			LSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			LSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			LSlot->SetPosition(FVector2D(-7.5f, -6.5f));
			LSlot->SetSize(FVector2D(19.0f, 19.0f));
		}

		UBorder* HeartRightLobe = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HeartRightLobe"));
		HeartRightLobe->SetBrush(WhiteHeartLobeBrush);
		if (UCanvasPanelSlot* RSlot = CircleCanvas->AddChildToCanvas(HeartRightLobe))
		{
			RSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			RSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			RSlot->SetPosition(FVector2D(7.5f, -6.5f));
			RSlot->SetSize(FVector2D(19.0f, 19.0f));
		}

		UBorder* HeartBottomDiamond = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HeartBottomDiamond"));
		HeartBottomDiamond->SetBrushColor(FLinearColor::White);
		HeartBottomDiamond->SetRenderTransformAngle(45.0f);
		if (UCanvasPanelSlot* DSlot = CircleCanvas->AddChildToCanvas(HeartBottomDiamond))
		{
			DSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			DSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			DSlot->SetPosition(FVector2D(0.0f, 2.0f));
			DSlot->SetSize(FVector2D(20.5f, 20.5f));
		}

		// Coral-red stack count centered inside the White Heart (exact Image 3 look!)
		ElixirHubCountText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ElixirHubCountText"));
		ElixirHubCountText->SetText(FText::FromString(TEXT("0")));
		ElixirHubCountText->SetJustification(ETextJustify::Center);
		StyleHUDText(ElixirHubCountText, FLinearColor(0.90f, 0.22f, 0.22f, 1.0f), 11, /*bShadow=*/false);
		if (UCanvasPanelSlot* CountSlot = CircleCanvas->AddChildToCanvas(ElixirHubCountText))
		{
			CountSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			CountSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CountSlot->SetPosition(FVector2D(0.0f, -1.0f));
			CountSlot->SetAutoSize(true);
		}

		ElixirHubButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ElixirHubButton"));
		ElixirHubButton->SetBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
		if (UCanvasPanelSlot* EBtnSlot = CircleCanvas->AddChildToCanvas(ElixirHubButton))
		{
			EBtnSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			EBtnSlot->SetOffsets(FMargin(0.0f));
		}

		ElixirCooldownText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ElixirCooldownText"));
		ElixirCooldownText->SetText(FText::FromString(TEXT("DAU [G]")));
		ElixirCooldownText->SetJustification(ETextJustify::Center);
		StyleHUDText(ElixirCooldownText, FLinearColor(1.0f, 0.92f, 0.72f, 0.95f), 9);
		if (UVerticalBoxSlot* CDSlot = ElixirHubWrap->AddChildToVerticalBox(ElixirCooldownText))
		{
			CDSlot->SetHorizontalAlignment(HAlign_Center);
			CDSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
		}

		// ====================================================================
		// 4. TOP-LEFT PARTY SYNERGY & INVITE OVERLAY (Step 5 Co-op 5-Player Party)
		// ====================================================================
		PartyOverlayBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PartyOverlayBorder"));
		PartyOverlayBorder->SetBrush(SocialOverlayBrush);
		PartyOverlayBorder->SetPadding(FMargin(8.0f, 5.0f));
		PartyOverlayBorder->SetVisibility(ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* PartySlot = RootCanvas->AddChildToCanvas(PartyOverlayBorder))
		{
			PartySlot->SetAnchors(FAnchors(0.0f, 0.0f));
			PartySlot->SetPosition(FVector2D(28.0f, 198.0f));
			PartySlot->SetAutoSize(true);
		}

		UVerticalBox* PartyVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PartyVBox"));
		PartyOverlayBorder->SetContent(PartyVBox);

		PartyOverlayText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PartyOverlayText"));
		StyleHUDText(PartyOverlayText, FLinearColor(0.48f, 1.0f, 0.68f), 9);
		PartyVBox->AddChildToVerticalBox(PartyOverlayText);

		PartyQuickActionBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("PartyQuickActionBtn"));
		PartyQuickActionBtn->SetBackgroundColor(FLinearColor(0.18f, 0.56f, 0.26f, 0.95f));
		PartyQuickActionLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PartyQuickActionLbl"));
		PartyQuickActionLbl->SetText(FText::FromString(TEXT(" [Y] DONG Y ")));
		StyleHUDText(PartyQuickActionLbl, FLinearColor::White, 8);
		PartyQuickActionBtn->SetContent(PartyQuickActionLbl);
		if (UVerticalBoxSlot* PBtnSlot = PartyVBox->AddChildToVerticalBox(PartyQuickActionBtn))
		{
			PBtnSlot->SetHorizontalAlignment(HAlign_Left);
			PBtnSlot->SetPadding(FMargin(0.0f, 3.0f, 0.0f, 0.0f));
		}

		// ====================================================================
		// 5. BOTTOM-CENTER NRO MULTI-CHANNEL CHAT BAR (Between Skill Hub & Dau Than!)
		// ====================================================================
		UBorder* ChatCard = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("NROChatCard"));
		ChatCard->SetBrush(SocialOverlayBrush);
		ChatCard->SetPadding(FMargin(7.0f, 5.0f));
		if (UCanvasPanelSlot* ChatSlot = RootCanvas->AddChildToCanvas(ChatCard))
		{
			ChatSlot->SetAnchors(FAnchors(0.5f, 1.0f));
			ChatSlot->SetAlignment(FVector2D(0.5f, 1.0f));
			ChatSlot->SetPosition(FVector2D(18.0f, -12.0f));
			ChatSlot->SetAutoSize(true);
		}

		USizeBox* ChatWidth = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ChatWidthBox"));
		ChatWidth->SetWidthOverride(420.0f);
		ChatCard->SetContent(ChatWidth);

		UVerticalBox* ChatVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ChatVBox"));
		ChatWidth->SetContent(ChatVBox);

		ChatHistoryText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ChatHistoryText"));
		StyleHUDText(ChatHistoryText, FLinearColor(0.95f, 0.96f, 0.90f), 8);
		if (UVerticalBoxSlot* CHS = ChatVBox->AddChildToVerticalBox(ChatHistoryText))
		{
			CHS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		}

		UHorizontalBox* ChatInputRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ChatInputRow"));
		ChatVBox->AddChildToVerticalBox(ChatInputRow);

		ChatChannelButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ChatChannelButton"));
		ChatChannelButton->SetBackgroundColor(FLinearColor(0.84f, 0.48f, 0.08f, 1.0f));
		ChatChannelLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ChatChannelLabel"));
		ChatChannelLabel->SetText(FText::FromString(TEXT(" [KV] ")));
		StyleHUDText(ChatChannelLabel, FLinearColor::White, 8);
		ChatChannelButton->SetContent(ChatChannelLabel);
		if (UHorizontalBoxSlot* CCS = ChatInputRow->AddChildToHorizontalBox(ChatChannelButton))
		{
			CCS->SetVerticalAlignment(VAlign_Center);
			CCS->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));
		}

		USizeBox* EditWidth = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ChatEditWidth"));
		EditWidth->SetWidthOverride(296.0f);
		ChatInputBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("ChatInputBox"));
		ChatInputBox->SetHintText(FText::FromString(TEXT("Enter: Chat | Y: Doi | T: Giao dich...")));
		EditWidth->SetContent(ChatInputBox);
		if (UHorizontalBoxSlot* CIS = ChatInputRow->AddChildToHorizontalBox(EditWidth))
		{
			CIS->SetVerticalAlignment(VAlign_Center);
			CIS->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));
		}

		ChatSendButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ChatSendButton"));
		ChatSendButton->SetBackgroundColor(FLinearColor(0.16f, 0.58f, 0.28f, 1.0f));
		UTextBlock* SendLbl = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ChatSendLbl"));
		SendLbl->SetText(FText::FromString(TEXT(" GUI ")));
		StyleHUDText(SendLbl, FLinearColor::White, 8);
		ChatSendButton->SetContent(SendLbl);
		ChatInputRow->AddChildToHorizontalBox(ChatSendButton);

		RecentChatLines.Add(TEXT("[HE THONG] Enter: Chat (Bong bong 3D) | Y: Moi Party | T: Giao dich P2P | Alt+LMB: Ping!"));
		RebuildChatHistoryText();
	}

	return Super::RebuildWidget();
}

void UIU_PlayerHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (SkillSlotButtons.Num() >= 6)
	{
		if (SkillSlotButtons[0]) { SkillSlotButtons[0]->OnClicked.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleSkillSlot1Clicked); }
		if (SkillSlotButtons[1]) { SkillSlotButtons[1]->OnClicked.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleSkillSlot2Clicked); }
		if (SkillSlotButtons[2]) { SkillSlotButtons[2]->OnClicked.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleSkillSlot3Clicked); }
		if (SkillSlotButtons[3]) { SkillSlotButtons[3]->OnClicked.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleSkillSlot4Clicked); }
		if (SkillSlotButtons[4]) { SkillSlotButtons[4]->OnClicked.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleSkillSlot5Clicked); }
		if (SkillSlotButtons[5]) { SkillSlotButtons[5]->OnClicked.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleSkillSlot6Clicked); }
	}
	if (ElixirHubButton)
	{
		ElixirHubButton->OnClicked.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleElixirHubClicked);
	}
	if (OpenProfileButton)
	{
		OpenProfileButton->OnClicked.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleOpenProfilePillClicked);
	}
	if (PartyQuickActionBtn)
	{
		PartyQuickActionBtn->OnClicked.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandlePartyActionBtnClicked);
	}
	if (ChatChannelButton)
	{
		ChatChannelButton->OnClicked.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleCycleChatChannelClicked);
	}
	if (ChatSendButton)
	{
		ChatSendButton->OnClicked.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleSendChatClicked);
	}
	if (ChatInputBox)
	{
		ChatInputBox->OnTextCommitted.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleChatTextCommitted);
	}

	BindToPlayerPawn(GetOwningPlayerPawn());
	UpdateSkillSlotFrameColors();
	UpdateElixirHubVisuals();
}

void UIU_PlayerHUDWidget::NativeDestruct()
{
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(ElixirCooldownVisualTimer);
	}
	UnbindCurrentComponents();
	Super::NativeDestruct();
}

void UIU_PlayerHUDWidget::SetActiveSkillSlot(int32 SlotNumber)
{
	ActiveSkillSlotNumber = FMath::Clamp(SlotNumber, 1, 6);
	UpdateSkillSlotFrameColors();
}

void UIU_PlayerHUDWidget::UpdateSkillSlotFrameColors()
{
	for (int32 i = 0; i < SkillSlotOuterFrames.Num(); ++i)
	{
		if (UBorder* Frame = SkillSlotOuterFrames[i])
		{
			Frame->SetBrush(((i + 1) == ActiveSkillSlotNumber) ? ActiveSkillFrameBrush : InactiveSkillFrameBrush);
		}
	}
}

void UIU_PlayerHUDWidget::NotifyElixirCooldownStarted(float DurationSeconds)
{
	UWorld* W = GetWorld();
	if (!W)
	{
		return;
	}
	ElixirCooldownEndWorldTime = W->GetTimeSeconds() + FMath::Max(0.1f, DurationSeconds);
	UpdateElixirHubVisuals();
	W->GetTimerManager().SetTimer(
		ElixirCooldownVisualTimer,
		this,
		&UIU_PlayerHUDWidget::TickElixirCooldownLabel,
		0.10f,
		true);
}

void UIU_PlayerHUDWidget::TickElixirCooldownLabel()
{
	UWorld* W = GetWorld();
	if (!W)
	{
		return;
	}
	const double Rem = ElixirCooldownEndWorldTime - W->GetTimeSeconds();
	if (Rem <= 0.0)
	{
		ElixirCooldownEndWorldTime = 0.0;
		W->GetTimerManager().ClearTimer(ElixirCooldownVisualTimer);
	}
	UpdateElixirHubVisuals();
}

void UIU_PlayerHUDWidget::UpdateElixirHubVisuals()
{
	if (const AIU_PlayerState* PS = BoundPlayerState.Get())
	{
		DisplayedElixirCount = PS->GetElixirCount();
	}

	if (ElixirHubCountText)
	{
		ElixirHubCountText->SetText(FText::FromString(FString::Printf(TEXT("%d"), DisplayedElixirCount)));
	}

	const UWorld* W = GetWorld();
	const double Rem = (W && ElixirCooldownEndWorldTime > 0.0) ? (ElixirCooldownEndWorldTime - W->GetTimeSeconds()) : 0.0;
	const bool bCoolingDown = (Rem > 0.05);

	if (ElixirCircleBorder)
	{
		ElixirCircleBorder->SetBrush((bCoolingDown || DisplayedElixirCount <= 0) ? ElixirCooldownCircleBrush : ElixirReadyCircleBrush);
	}
	if (ElixirCooldownText)
	{
		if (bCoolingDown)
		{
			ElixirCooldownText->SetText(FText::FromString(FString::Printf(TEXT("CD %.1fs"), Rem)));
			ElixirCooldownText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.45f, 0.35f)));
		}
		else
		{
			ElixirCooldownText->SetText(FText::FromString(TEXT("DAU [G]")));
			ElixirCooldownText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.92f, 0.72f)));
		}
	}
}

void UIU_PlayerHUDWidget::DispatchSkillSlotClick(int32 SlotNumber)
{
	SetActiveSkillSlot(SlotNumber);
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		IUPC->ExecuteHubSkillSlot(SlotNumber);
	}
}

void UIU_PlayerHUDWidget::HandleSkillSlot1Clicked() { DispatchSkillSlotClick(1); }
void UIU_PlayerHUDWidget::HandleSkillSlot2Clicked() { DispatchSkillSlotClick(2); }
void UIU_PlayerHUDWidget::HandleSkillSlot3Clicked() { DispatchSkillSlotClick(3); }
void UIU_PlayerHUDWidget::HandleSkillSlot4Clicked() { DispatchSkillSlotClick(4); }
void UIU_PlayerHUDWidget::HandleSkillSlot5Clicked() { DispatchSkillSlotClick(5); }
void UIU_PlayerHUDWidget::HandleSkillSlot6Clicked() { DispatchSkillSlotClick(6); }

void UIU_PlayerHUDWidget::HandleElixirHubClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		IUPC->TriggerQuickUseElixirFromHUD();
	}
}

void UIU_PlayerHUDWidget::HandleOpenProfilePillClicked()
{
	if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
	{
		IUPC->SetInventoryPanelVisible(!IUPC->IsInventoryPanelVisible(), true);
	}
}

void UIU_PlayerHUDWidget::BindToPlayerPawn(APawn* NewPawn)
{
	UnbindCurrentComponents();
	if (!NewPawn)
	{
		return;
	}

	if (UIU_AttributeComponent* Attr = NewPawn->FindComponentByClass<UIU_AttributeComponent>())
	{
		BoundAttributeComp = Attr;
		Attr->OnHPChanged.AddDynamic(this, &UIU_PlayerHUDWidget::HandleHPChanged);
		Attr->OnKiChanged.AddDynamic(this, &UIU_PlayerHUDWidget::HandleKiChanged);
		Attr->OnAttackPowerChanged.AddDynamic(this, &UIU_PlayerHUDWidget::HandleAttackPowerChanged);
		Attr->OnDefenseChanged.AddDynamic(this, &UIU_PlayerHUDWidget::HandleDefenseChanged);

		HandleHPChanged(Attr->GetHP(), Attr->GetMaxHP(), 0.0f);
		HandleKiChanged(Attr->GetKi(), Attr->GetMaxKi(), 0.0f);
	}

	if (UIU_CombatComponent* Combat = NewPawn->FindComponentByClass<UIU_CombatComponent>())
	{
		BoundCombatComp = Combat;
		Combat->OnCombatStateChanged.AddDynamic(this, &UIU_PlayerHUDWidget::HandleCombatStateChanged);
		Combat->OnComboStepExecuted.AddDynamic(this, &UIU_PlayerHUDWidget::HandleComboStepExecuted);
		Combat->OnMobilityStyleChanged.AddDynamic(this, &UIU_PlayerHUDWidget::HandleMobilityStyleChanged);
		DisplayedCombatState = Combat->GetCombatState();
		DisplayedComboStep = Combat->GetCurrentComboStep();
		DisplayedMobilityStyle = Combat->GetMobilityStyle();
	}

	if (UIU_FlightComponent* Flight = NewPawn->FindComponentByClass<UIU_FlightComponent>())
	{
		BoundFlightComp = Flight;
		Flight->OnFlightStateChanged.AddDynamic(this, &UIU_PlayerHUDWidget::HandleFlightStateChanged);
		bDisplayedFlying = Flight->IsFlying();
	}

	EnsurePlayerStateBound(NewPawn);
	RefreshPotentialReadout();
	RefreshStatusText();
	UpdateElixirHubVisuals();
}

void UIU_PlayerHUDWidget::EnsurePlayerStateBound(APawn* CurrentPawn)
{
	if (!CurrentPawn)
	{
		return;
	}
	AIU_PlayerState* IUPS = CurrentPawn->GetPlayerState<AIU_PlayerState>();
	if (!IUPS || BoundPlayerState.Get() == IUPS)
	{
		return;
	}

	if (AIU_PlayerState* OldPS = BoundPlayerState.Get())
	{
		OldPS->OnCharacterEXPChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterEXPChanged);
		OldPS->OnCharacterPotentialChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterPotentialChanged);
		OldPS->OnCharacterEconomyChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterEconomyChanged);
		OldPS->OnCharacterSocialChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterSocialChanged);
	}

	BoundPlayerState = IUPS;
	IUPS->OnCharacterEXPChanged.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterEXPChanged);
	IUPS->OnCharacterPotentialChanged.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterPotentialChanged);
	IUPS->OnCharacterEconomyChanged.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterEconomyChanged);
	IUPS->OnCharacterSocialChanged.AddUniqueDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterSocialChanged);
	HandleCharacterEXPChanged(IUPS->GetCurrentEXP(), IUPS->GetNextLevelEXP(), IUPS->GetCharacterLevel());
	RefreshPotentialReadout();
	UpdateElixirHubVisuals();
}

void UIU_PlayerHUDWidget::UnbindCurrentComponents()
{
	if (UIU_AttributeComponent* Attr = BoundAttributeComp.Get())
	{
		Attr->OnHPChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleHPChanged);
		Attr->OnKiChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleKiChanged);
		Attr->OnAttackPowerChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleAttackPowerChanged);
		Attr->OnDefenseChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleDefenseChanged);
	}
	if (UIU_CombatComponent* Combat = BoundCombatComp.Get())
	{
		Combat->OnCombatStateChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCombatStateChanged);
		Combat->OnComboStepExecuted.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleComboStepExecuted);
		Combat->OnMobilityStyleChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleMobilityStyleChanged);
	}
	if (UIU_FlightComponent* Flight = BoundFlightComp.Get())
	{
		Flight->OnFlightStateChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleFlightStateChanged);
	}
	if (AIU_PlayerState* PS = BoundPlayerState.Get())
	{
		PS->OnCharacterEXPChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterEXPChanged);
		PS->OnCharacterPotentialChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterPotentialChanged);
		PS->OnCharacterEconomyChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterEconomyChanged);
		PS->OnCharacterSocialChanged.RemoveDynamic(this, &UIU_PlayerHUDWidget::HandleCharacterSocialChanged);
	}

	BoundAttributeComp.Reset();
	BoundCombatComp.Reset();
	BoundFlightComp.Reset();
	BoundPlayerState.Reset();
}

void UIU_PlayerHUDWidget::HandleHPChanged(float CurrentHP, float MaxHP, float Delta)
{
	if (HealthProgressBar)
	{
		HealthProgressBar->SetPercent(MaxHP > 0.0f ? (CurrentHP / MaxHP) : 0.0f);
	}
	if (HealthValueText)
	{
		HealthValueText->SetText(FText::FromString(FString::Printf(TEXT("HP: %.0f / %.0f"), CurrentHP, MaxHP)));
	}
}

void UIU_PlayerHUDWidget::HandleKiChanged(float CurrentKi, float MaxKi, float Delta)
{
	if (KiProgressBar)
	{
		KiProgressBar->SetPercent(MaxKi > 0.0f ? (CurrentKi / MaxKi) : 0.0f);
	}
	if (KiValueText)
	{
		KiValueText->SetText(FText::FromString(FString::Printf(TEXT("KI: %.0f / %.0f"), CurrentKi, MaxKi)));
	}
}

void UIU_PlayerHUDWidget::HandleCharacterEXPChanged(int64 CurrentEXP, int64 NextLevelEXP, int32 CurrentLevel)
{
	if (EXPProgressBar)
	{
		const float Pct = NextLevelEXP > 0 ? FMath::Clamp(static_cast<float>(CurrentEXP) / static_cast<float>(NextLevelEXP), 0.0f, 1.0f) : 1.0f;
		EXPProgressBar->SetPercent(Pct);
	}
	if (LevelEXPValueText)
	{
		FString NamePrefix;
		if (const AIU_PlayerState* PS = BoundPlayerState.Get())
		{
			if (!PS->GetGuildTag().IsEmpty())
			{
				NamePrefix += FString::Printf(TEXT("[%s] "), *PS->GetGuildTag());
			}
			if (!PS->GetPersistedCharacterName().IsEmpty())
			{
				NamePrefix += FString::Printf(TEXT("[%s] "), *PS->GetPersistedCharacterName());
			}
		}
		LevelEXPValueText->SetText(FText::FromString(
			FString::Printf(TEXT("%sLV.%d | SM: %lld/%lld"), *NamePrefix, CurrentLevel, CurrentEXP, NextLevelEXP)));
	}
}

void UIU_PlayerHUDWidget::HandleAttackPowerChanged(float NewAttackPower)
{
	RefreshPotentialReadout();
}

void UIU_PlayerHUDWidget::HandleDefenseChanged(float NewDefense)
{
	RefreshPotentialReadout();
}

void UIU_PlayerHUDWidget::HandleCharacterPotentialChanged(int64 PotentialPoints, int64 NextHPCost, int64 NextKiCost, int64 NextAtkCost)
{
	RefreshPotentialReadout();
}

void UIU_PlayerHUDWidget::HandleCharacterEconomyChanged(int64 StandardCoin, int32 ElixirCount, int32 CrimsonOreCount, int32 EquippedCount)
{
	DisplayedElixirCount = ElixirCount;
	RefreshPotentialReadout();
	UpdateElixirHubVisuals();
}

void UIU_PlayerHUDWidget::HandleCharacterSocialChanged(const FString&, const FString&, const FString&)
{
	if (const AIU_PlayerState* PS = BoundPlayerState.Get())
	{
		HandleCharacterEXPChanged(PS->GetCurrentEXP(), PS->GetNextLevelEXP(), PS->GetCharacterLevel());
	}
}

void UIU_PlayerHUDWidget::ApplyPartySnapshot(const FIU_PartySnapshot& Snapshot)
{
	DisplayedParty = Snapshot;
	RebuildPartyOverlayText();
}

void UIU_PlayerHUDWidget::ApplyTradeInviteBanner(const FIU_TradeSessionSnapshot& Snapshot)
{
	DisplayedTradeInvite = Snapshot;
	RebuildPartyOverlayText();
}

void UIU_PlayerHUDWidget::RebuildPartyOverlayText()
{
	if (!PartyOverlayBorder || !PartyOverlayText)
	{
		return;
	}

	const AIU_PlayerState* PS = BoundPlayerState.Get();
	const FString MyCharID = PS ? PS->GetPersistedCharacterUUID() : TEXT("");
	const bool bHasPendingTradeInvite = DisplayedTradeInvite.bHasActiveTrade &&
		DisplayedTradeInvite.State == TEXT("PENDING_INVITE") &&
		DisplayedTradeInvite.RecipientCharID == MyCharID;
	const bool bHasPendingPartyInvite = DisplayedParty.PendingInvites.Num() > 0;

	if (!DisplayedParty.bInParty && !bHasPendingPartyInvite && !bHasPendingTradeInvite)
	{
		PartyOverlayBorder->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	PartyOverlayBorder->SetVisibility(ESlateVisibility::Visible);
	FString Lines;
	if (DisplayedParty.bInParty)
	{
		const int32 Count = DisplayedParty.Members.Num();
		const int32 BonusPct = FMath::RoundToInt((AIU_PlayerState::ComputePartySynergyMultiplier(Count) - 1.0f) * 100.0f);
		Lines += FString::Printf(TEXT("[TO DOI %d/5 | CONG HUONG +%d%% SM & TN]\n"), Count, BonusPct);
		for (const FIU_PartyMemberView& M : DisplayedParty.Members)
		{
			Lines += FString::Printf(
				TEXT("%sLv.%d %s (HP %.0f/%.0f)\n"),
				M.bIsLeader ? TEXT("[DT] ") : TEXT(" - "),
				M.Level,
				*M.CharacterName,
				M.HP,
				M.MaxHP);
		}
	}
	if (bHasPendingPartyInvite)
	{
		Lines += FString::Printf(TEXT("[LOI MOI DOI] %s moi vao Party! -> Bam [Y]\n"), *DisplayedParty.PendingInvites[0].InviterName);
	}
	if (bHasPendingTradeInvite)
	{
		Lines += FString::Printf(TEXT("[GIAO DICH P2P] %s moi giao dich! -> Bam [T]\n"), *DisplayedTradeInvite.InitiatorName);
	}
	Lines.TrimEndInline();
	PartyOverlayText->SetText(FText::FromString(Lines));

	if (PartyQuickActionBtn && PartyQuickActionLbl)
	{
		if (bHasPendingTradeInvite)
		{
			PartyQuickActionBtn->SetVisibility(ESlateVisibility::Visible);
			PartyQuickActionLbl->SetText(FText::FromString(TEXT(" [T] MO GIAO DICH ")));
		}
		else if (bHasPendingPartyInvite)
		{
			PartyQuickActionBtn->SetVisibility(ESlateVisibility::Visible);
			PartyQuickActionLbl->SetText(FText::FromString(TEXT(" [Y] VAO TO DOI ")));
		}
		else if (DisplayedParty.bInParty)
		{
			PartyQuickActionBtn->SetVisibility(ESlateVisibility::Visible);
			PartyQuickActionLbl->SetText(FText::FromString(TEXT(" ROI TO DOI ")));
		}
		else
		{
			PartyQuickActionBtn->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UIU_PlayerHUDWidget::HandlePartyActionBtnClicked()
{
	AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer());
	if (!IUPC)
	{
		return;
	}
	const AIU_PlayerState* PS = BoundPlayerState.Get();
	const FString MyCharID = PS ? PS->GetPersistedCharacterUUID() : TEXT("");
	if (DisplayedTradeInvite.bHasActiveTrade && DisplayedTradeInvite.State == TEXT("PENDING_INVITE") && DisplayedTradeInvite.RecipientCharID == MyCharID)
	{
		IUPC->Server_RequestTradeRespond(DisplayedTradeInvite.TradeID, true);
		return;
	}
	if (DisplayedParty.PendingInvites.Num() > 0)
	{
		IUPC->Server_RequestPartyRespond(DisplayedParty.PendingInvites[0].InviteID, true);
		return;
	}
	if (DisplayedParty.bInParty)
	{
		IUPC->Server_RequestPartyLeave();
	}
}

void UIU_PlayerHUDWidget::AppendChatMessage(const FIU_ChatMessageView& Message)
{
	const TCHAR* ChShort = (Message.Channel == TEXT("ZONE")) ? TEXT("TG")
		: (Message.Channel == TEXT("PARTY")) ? TEXT("TD")
		: (Message.Channel == TEXT("GUILD")) ? TEXT("BG")
		: (Message.Channel == TEXT("WHISPER")) ? TEXT("MAT")
		: (Message.Channel == TEXT("SYSTEM")) ? TEXT("HT")
		: TEXT("KV");

	const FString TagPart = Message.SenderGuildTag.IsEmpty() ? TEXT("") : FString::Printf(TEXT("[%s]"), *Message.SenderGuildTag);
	const FString Line = FString::Printf(TEXT("[%s] %s%s: %s"), ChShort, *TagPart, *Message.SenderName, *Message.Content);
	RecentChatLines.Add(Line);
	while (RecentChatLines.Num() > 4)
	{
		RecentChatLines.RemoveAt(0);
	}
	RebuildChatHistoryText();
}

void UIU_PlayerHUDWidget::RebuildChatHistoryText()
{
	if (ChatHistoryText)
	{
		ChatHistoryText->SetText(FText::FromString(FString::Join(RecentChatLines, TEXT("\n"))));
	}
}

void UIU_PlayerHUDWidget::HandleCycleChatChannelClicked()
{
	if (SelectedChatChannel == TEXT("LOCAL"))       { SelectedChatChannel = TEXT("ZONE");  if (ChatChannelLabel) { ChatChannelLabel->SetText(FText::FromString(TEXT(" [TG] "))); } }
	else if (SelectedChatChannel == TEXT("ZONE"))   { SelectedChatChannel = TEXT("PARTY"); if (ChatChannelLabel) { ChatChannelLabel->SetText(FText::FromString(TEXT(" [TD] "))); } }
	else if (SelectedChatChannel == TEXT("PARTY"))  { SelectedChatChannel = TEXT("GUILD"); if (ChatChannelLabel) { ChatChannelLabel->SetText(FText::FromString(TEXT(" [BG] "))); } }
	else                                            { SelectedChatChannel = TEXT("LOCAL"); if (ChatChannelLabel) { ChatChannelLabel->SetText(FText::FromString(TEXT(" [KV] "))); } }
}

void UIU_PlayerHUDWidget::HandleSendChatClicked()
{
	FocusOrSubmitChatBar();
}

void UIU_PlayerHUDWidget::HandleChatTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		const FString Trimmed = Text.ToString().TrimStartAndEnd();
		if (!Trimmed.IsEmpty())
		{
			if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
			{
				IUPC->Server_RequestSendChat(SelectedChatChannel, Trimmed, TEXT(""));
			}
			if (ChatInputBox)
			{
				ChatInputBox->SetText(FText::GetEmpty());
			}
		}
		if (APlayerController* PC = GetOwningPlayer())
		{
			FInputModeGameOnly GameInput;
			PC->SetInputMode(GameInput);
		}
	}
}

void UIU_PlayerHUDWidget::FocusOrSubmitChatBar()
{
	if (!ChatInputBox)
	{
		return;
	}
	const FString Trimmed = ChatInputBox->GetText().ToString().TrimStartAndEnd();
	if (!Trimmed.IsEmpty())
	{
		if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(GetOwningPlayer()))
		{
			IUPC->Server_RequestSendChat(SelectedChatChannel, Trimmed, TEXT(""));
		}
		ChatInputBox->SetText(FText::GetEmpty());
		if (APlayerController* PC = GetOwningPlayer())
		{
			FInputModeGameOnly GameInput;
			PC->SetInputMode(GameInput);
		}
		return;
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(ChatInputBox->TakeWidget());
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
	}
}

void UIU_PlayerHUDWidget::RefreshPotentialReadout()
{
	if (!PotentialValueText)
	{
		return;
	}
	const int64 PoolTN = BoundPlayerState.IsValid() ? BoundPlayerState->GetPotentialPoints() : 0;
	const int64 Coin = BoundPlayerState.IsValid() ? BoundPlayerState->GetStandardCoin() : 0;
	const float Atk = BoundAttributeComp.IsValid() ? BoundAttributeComp->GetAttackPower() : 100.0f;
	const float Def = BoundAttributeComp.IsValid() ? BoundAttributeComp->GetDefense() : 20.0f;
	PotentialValueText->SetText(FText::FromString(
		FString::Printf(TEXT("TN: %lld | SD: %.0f | GIAP: %.0f | VANG: %lld"), PoolTN, Atk, Def, Coin)));
}

void UIU_PlayerHUDWidget::HandleCombatStateChanged(EIU_CombatState OldState, EIU_CombatState NewState)
{
	DisplayedCombatState = NewState;
	if (NewState == EIU_CombatState::Idle)
	{
		DisplayedComboStep = 0;
	}
	else if (NewState == EIU_CombatState::Dodging)
	{
		SetActiveSkillSlot(3);
	}
	RefreshStatusText();
}

void UIU_PlayerHUDWidget::HandleComboStepExecuted(int32 ComboStep)
{
	DisplayedComboStep = ComboStep;
	SetActiveSkillSlot(1);
	RefreshStatusText();
}

void UIU_PlayerHUDWidget::HandleFlightStateChanged(bool bIsFlying)
{
	bDisplayedFlying = bIsFlying;
	if (bIsFlying)
	{
		SetActiveSkillSlot(6);
	}
	RefreshStatusText();
}

void UIU_PlayerHUDWidget::HandleMobilityStyleChanged(EIU_MobilityStepStyle NewStyle)
{
	DisplayedMobilityStyle = NewStyle;
	RefreshStatusText();
}

void UIU_PlayerHUDWidget::RefreshStatusText()
{
	if (!StatusReadoutText)
	{
		return;
	}

	const TCHAR* MoveLabel = bDisplayedFlying ? TEXT("FLIGHT 3D") : TEXT("GROUND");
	const TCHAR* StyleLabel = (DisplayedMobilityStyle == EIU_MobilityStepStyle::InstantBlink)
		? TEXT("ALIEN BLINK")
		: TEXT("EARTH DASH");

	FString CombatLabel = TEXT("READY");
	if (DisplayedCombatState == EIU_CombatState::Attacking && DisplayedComboStep > 0)
	{
		CombatLabel = FString::Printf(TEXT("COMBO HIT %d/3"), DisplayedComboStep);
	}
	else if (DisplayedCombatState == EIU_CombatState::Dodging)
	{
		const bool bCombatEvade = (BoundCombatComp.IsValid() && BoundCombatComp->IsLastDodgeCombatEvade());
		if (bCombatEvade)
		{
			CombatLabel = TEXT("MIRAGE EVADE (AFTERIMAGE)");
		}
		else
		{
			CombatLabel = (DisplayedMobilityStyle == EIU_MobilityStepStyle::InstantBlink)
				? TEXT("INSTANT BLINK")
				: TEXT("QUICK DASH");
		}
	}
	else if (DisplayedCombatState == EIU_CombatState::HitReaction)
	{
		CombatLabel = TEXT("STAGGERED");
	}
	else if (DisplayedCombatState == EIU_CombatState::Defeated)
	{
		CombatLabel = TEXT("DEFEATED");
	}

	StatusReadoutText->SetText(FText::FromString(FString::Printf(TEXT("[%s | %s]  %s"), MoveLabel, StyleLabel, *CombatLabel)));
}
