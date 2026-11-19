// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "IU_HUDManager.generated.h"

class UIU_PlayerHUDWidget;

/**
 * Prototype HUD Manager (Milestone 3: P3-09).
 * Spawns the event-driven UIU_PlayerHUDWidget (HP/Ki bars) and renders crisp 3D Lock-On
 * target brackets + target HP bar on Canvas.
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_HUDManager : public AHUD
{
	GENERATED_BODY()

public:
	AIU_HUDManager();

	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|UI")
	TSubclassOf<UIU_PlayerHUDWidget> PlayerHUDWidgetClass;

	UFUNCTION(BlueprintPure, Category = "IU|UI")
	UIU_PlayerHUDWidget* GetPlayerHUDWidget() const { return ActiveHUDWidget; }

private:
	UPROPERTY(Transient)
	TObjectPtr<UIU_PlayerHUDWidget> ActiveHUDWidget;
};
