// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_HUDManager.generated.h"

class UIU_PlayerHUDWidget;
class UIU_DamageComponent;
class UIU_CombatComponent;

struct FIU_FloatingCombatText
{
	FVector WorldLocation = FVector::ZeroVector;
	FString Label;
	FLinearColor Color = FLinearColor::White;
	double SpawnWorldTime = 0.0;
	float Duration = 0.85f;
	float Scale = 1.25f;
};

/**
 * Prototype Action RPG HUD Manager (Milestones 3–7 + Playable Polish).
 * - Spawns event-driven UIU_PlayerHUDWidget (HP/Ki/State)
 * - Renders Genshin-style reactive center crosshair & 3D Lock-On diamond brackets
 * - Renders world-space overhead Enemy HP bars & "[ ! RMB EVADE ]" melee windup telegraphs
 * - Renders floating 3D->2D damage numbers, "PERFECT EVADE!" popups & compact controls legend
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|UI")
	bool bShowControlsLegend = true;

	UFUNCTION(BlueprintPure, Category = "IU|UI")
	UIU_PlayerHUDWidget* GetPlayerHUDWidget() const { return ActiveHUDWidget; }

private:
	void RefreshCombatEventBindings(APawn* OwningPawn);

	UFUNCTION()
	void HandleAnyActorDamaged(const FIU_DamageSpec& DamageSpec, float RemainingHP);

	UFUNCTION()
	void HandlePlayerCombatStateChanged(EIU_CombatState PreviousState, EIU_CombatState NewState);

	UPROPERTY(Transient)
	TObjectPtr<UIU_PlayerHUDWidget> ActiveHUDWidget;

	TSet<TWeakObjectPtr<UIU_DamageComponent>> ObservedDamageComponents;
	TWeakObjectPtr<UIU_CombatComponent> ObservedPlayerCombat;
	TArray<FIU_FloatingCombatText> FloatingTexts;
	double NextBindingScanTime = 0.0;
};
