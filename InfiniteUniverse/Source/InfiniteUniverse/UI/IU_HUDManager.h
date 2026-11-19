// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_HUDManager.generated.h"

class UIU_PlayerHUDWidget;
class UIU_DamageComponent;
class UIU_CombatComponent;
class UIU_AttributeComponent;

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
 * Prototype Action RPG HUD Manager (Milestones 3–7 + Step 1 Gatekeeper Telemetry & Notifications).
 * - Spawns event-driven UIU_PlayerHUDWidget (HP/Ki/State)
 * - Renders Genshin-style reactive center crosshair & 3D Lock-On diamond brackets
 * - Renders world-space overhead Enemy HP bars & "[ ! RMB EVADE ]" melee windup telegraphs
 * - Renders floating 3D->2D damage numbers, "PERFECT EVADE!", and explicit Enemy/Player Death Notifications
 * - Renders toggleable (F3) Live Gameplay Debug Telemetry Overlay (FPS, Frame ms, RAM MB, Speeds, Attributes, AI Count)
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|UI|Debug")
	bool bShowDebugTelemetry = true;

	UFUNCTION(BlueprintCallable, Category = "IU|UI|Debug")
	void ToggleDebugTelemetry() { bShowDebugTelemetry = !bShowDebugTelemetry; }

	UFUNCTION(BlueprintCallable, Category = "IU|UI")
	void SetShowcaseStepBanner(const FString& InBannerText);

	UFUNCTION(BlueprintPure, Category = "IU|UI")
	UIU_PlayerHUDWidget* GetPlayerHUDWidget() const { return ActiveHUDWidget; }

	UFUNCTION(BlueprintPure, Category = "IU|UI")
	int32 GetDefeatedEnemyNotificationCount() const { return DefeatedEnemyCount; }

	UFUNCTION(BlueprintPure, Category = "IU|UI")
	int32 GetPlayerDefeatedNotificationCount() const { return PlayerDefeatedCount; }

private:
	void RefreshCombatEventBindings(APawn* OwningPawn);

	UFUNCTION()
	void HandleAnyActorDamaged(const FIU_DamageSpec& DamageSpec, float RemainingHP);

	UFUNCTION()
	void HandleAnyActorHealthDepleted(AActor* DepletedActor);

	UFUNCTION()
	void HandlePlayerCombatStateChanged(EIU_CombatState PreviousState, EIU_CombatState NewState);

	UPROPERTY(Transient)
	TObjectPtr<UIU_PlayerHUDWidget> ActiveHUDWidget;

	TWeakObjectPtr<APawn> BoundHUDWidgetPawn;
	TSet<TWeakObjectPtr<UIU_DamageComponent>> ObservedDamageComponents;
	TSet<TWeakObjectPtr<UIU_AttributeComponent>> ObservedAttributeComponents;
	TWeakObjectPtr<UIU_CombatComponent> ObservedPlayerCombat;
	TArray<FIU_FloatingCombatText> FloatingTexts;
	double NextBindingScanTime = 0.0;

	FString ActiveCenterToastText;
	FLinearColor ActiveCenterToastColor = FLinearColor::White;
	double CenterToastExpireTime = 0.0;
	FString ShowcaseStepBannerText;

	int32 DefeatedEnemyCount = 0;
	int32 PlayerDefeatedCount = 0;
	float SmoothedFrameDeltaSeconds = 1.0f / 60.0f;
};
