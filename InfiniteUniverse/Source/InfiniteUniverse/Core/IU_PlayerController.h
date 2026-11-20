// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Data/IU_GameplayTypes.h"
#include "Network/IU_BackendSubsystem.h"
#include "UI/IU_InventoryEquipmentWidget.h"
#include "IU_PlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class AIU_PlayerCharacter;
class UIU_LoginCharacterWidget;
class UIU_PotentialUpgradeWidget;
class UIU_InventoryEquipmentWidget;
class UIU_PlayerTradeWidget;

/**
 * Enhanced Input PlayerController decoupling hardware key bindings from character movement execution.
 * All InputActions and MappingContexts are remappable and editable via Blueprint or Data config.
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AIU_PlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/** Marks synthetic headless automation test controllers (without ULocalPlayer) as locally controlled in UE 5.8+. */
	void MarkAsLocalTestController() { SetAsLocalPlayerController(); }

	/**
	 * Populates an InputMappingContext in C++ using direct FEnhancedActionKeyMapping& references
	 * so axis swizzle/negate modifiers for W/A/S/D and Mouse2D are guaranteed persisted on the asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "IU|Input")
	static void ConfigureStandardPlayerMappings(
		UInputMappingContext* TargetIMC,
		UInputAction* InMove,
		UInputAction* InLook,
		UInputAction* InJump,
		UInputAction* InSprint,
		UInputAction* InZoom,
		UInputAction* InToggleRot,
		UInputAction* InDescend = nullptr,
		UInputAction* InFlightToggle = nullptr,
		UInputAction* InMeleeAttack = nullptr,
		UInputAction* InKiBlast = nullptr,
		UInputAction* InDodge = nullptr,
		UInputAction* InLockOn = nullptr,
		UInputAction* InSwitchTarget = nullptr,
		UInputAction* InToggleMobilityStyle = nullptr);

	// ========================================================================
	// Remappable Enhanced Input Assets
	// ========================================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> DescendAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> FlightToggleAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> CameraZoomAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> ToggleRotationModeAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> MeleeAttackAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> KiBlastAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> DodgeAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> LockOnAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> SwitchTargetAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IU|Input|Actions")
	TObjectPtr<UInputAction> ToggleMobilityStyleAction;

	// ========================================================================
	// Camera Pitch Clamp Limits & Genshin-Style Initial Overlook Angle
	// ========================================================================

	/** Gentle downward overlook pitch (-8.5 deg) on spawn so enemies ahead & character both stay clearly visible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "-35.0", ClampMax = "15.0"))
	float DefaultStartingCameraPitch = -8.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "-89.0", ClampMax = "0.0"))
	float MinCameraPitch = -75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|Camera", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxCameraPitch = 75.0f;

	/** Applies DefaultStartingCameraPitch while preserving current controller yaw. */
	UFUNCTION(BlueprintCallable, Category = "IU|Camera")
	void ApplyInitialGameplayCameraAngle();

	/** Requests Server Authority to toggle global PvP sparring mode (F4 shortcut in Development). */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Multiplayer")
	void Server_ToggleGlobalPvPSparring();

	/** Server-authoritative RPC: spends accumulated Potential Points to upgrade MaxHP (+100), MaxKi (+100), or AttackPower (+10). */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Potential")
	void Server_RequestAllocatePotential(EIU_PotentialStatType StatType);

	/** Shows or hides the NRO Potential & Stat Allocation UMG modal (Key P). */
	UFUNCTION(BlueprintCallable, Category = "IU|UI")
	void SetPotentialUpgradePanelVisible(bool bVisible, bool bUpdateMouseInputMode = true);

	UFUNCTION(BlueprintPure, Category = "IU|UI")
	bool IsPotentialUpgradePanelVisible() const;

	/** Shows or hides the NRO Character Profile modal (Key Tab / I / B / P). */
	UFUNCTION(BlueprintCallable, Category = "IU|UI")
	void SetInventoryPanelVisible(bool bVisible, bool bUpdateMouseInputMode = true);

	/** Opens the NRO Character Profile modal directly on a specific tab (e.g. HanhTrang or KyNang). */
	UFUNCTION(BlueprintCallable, Category = "IU|UI")
	void OpenCharacterProfileOnTab(EIU_CharacterProfileTab TargetTab, bool bUpdateMouseInputMode = true);

	UFUNCTION(BlueprintPure, Category = "IU|UI")
	bool IsInventoryPanelVisible() const;

	UFUNCTION(BlueprintPure, Category = "IU|UI")
	UIU_InventoryEquipmentWidget* GetActiveCharacterProfileWidget() const { return ActiveInventoryWidget; }

	/** Executes an outer Skill Hub slot (1..6) and highlights that slot on UIU_PlayerHUDWidget (NEVER spends Potential!). */
	UFUNCTION(BlueprintCallable, Category = "IU|Skills")
	void ExecuteHubSkillSlot(int32 SlotNumber);

	/** Triggered by clicking the bottom-right circular Dau Than Hub or pressing Key G. */
	UFUNCTION(BlueprintCallable, Category = "IU|Inventory")
	void TriggerQuickUseElixirFromHUD();

	/** Opens the Account/Character Login modal (Key F5 or Chuc Nang tab). */
	void OpenLoginCharacterModal();

	bool HasCachedInventorySnapshot() const { return bHasCachedInventorySnapshot; }
	const FIU_InventorySnapshot& GetCachedInventorySnapshot() const { return CachedInventorySnapshot; }

	bool HasCachedGuildSnapshot() const { return bHasCachedGuildSnapshot; }
	const FIU_GuildSnapshot& GetCachedGuildSnapshot() const { return CachedGuildSnapshot; }

	bool HasCachedPartySnapshot() const { return bHasCachedPartySnapshot; }
	const FIU_PartySnapshot& GetCachedPartySnapshot() const { return CachedPartySnapshot; }

	bool HasCachedTradeSnapshot() const { return bHasCachedTradeSnapshot; }
	const FIU_TradeSessionSnapshot& GetCachedTradeSnapshot() const { return CachedTradeSnapshot; }

	/** Shows or hides the NRO 2-Step P2P Direct Trade modal (Key T). */
	UFUNCTION(BlueprintCallable, Category = "IU|Trade")
	void SetPlayerTradePanelVisible(bool bVisible, bool bUpdateMouseInputMode = true);

	UFUNCTION(BlueprintPure, Category = "IU|Trade")
	bool IsPlayerTradePanelVisible() const;

	UFUNCTION(BlueprintPure, Category = "IU|Trade")
	UIU_PlayerTradeWidget* GetActivePlayerTradeWidget() const { return ActiveTradeWidget; }

	/** Server-enforced cooldown duration for Dau Than / consumables (default 3.0s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "IU|Inventory", meta = (ClampMin = "0.1", ClampMax = "60.0"))
	float ConsumableCooldownSeconds = 3.0f;

	/** Checks whether this controller can currently execute a consumable use on Server Authority. */
	bool CanUseConsumableOnServer(float& OutRemainingSeconds) const;

	// ========================================================================
	// Step 4 Inventory, Equipment, Consumable & Economy RPCs
	// ========================================================================

	UFUNCTION(Client, Reliable, Category = "IU|Inventory")
	void Client_ReceiveInventorySnapshot(const FIU_InventorySnapshot& Snapshot);

	UFUNCTION(Client, Reliable, Category = "IU|Inventory")
	void Client_NotifyLootDropped(int64 CoinsGranted, const TArray<FString>& DroppedItemLabels, bool bBagFullSkipped);

	UFUNCTION(Client, Reliable, Category = "IU|Inventory")
	void Client_NotifyInventoryFeedback(const FString& Message, bool bIsError);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Inventory")
	void Server_RequestRefreshInventory();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Inventory")
	void Server_RequestEquipItem(const FString& ItemInstanceID);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Inventory")
	void Server_RequestUnequipSlot(const FString& SlotID);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Inventory")
	void Server_RequestUseConsumable(const FString& OptionalItemInstanceID);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Inventory")
	void Server_RequestToggleItemLock(const FString& ItemInstanceID, bool bLocked);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Inventory")
	void Server_RequestSellItem(const FString& ItemInstanceID, int32 Quantity);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Inventory")
	void Server_RequestEnhanceItem(const FString& ItemInstanceID);

	// ========================================================================
	// Step 5 Party, Guild, Multi-Channel Chat, Tactical Ping & P2P Trade RPCs
	// ========================================================================

	UFUNCTION(Client, Reliable, Category = "IU|Party")
	void Client_ReceivePartySnapshot(const FIU_PartySnapshot& Snapshot);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Party")
	void Server_RequestRefreshParty();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Party")
	void Server_RequestPartyInvite(const FString& TargetCharUUID);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Party")
	void Server_RequestPartyRespond(const FString& InviteID, bool bAccept);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Party")
	void Server_RequestPartyLeave();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Party")
	void Server_RequestPartyKick(const FString& TargetCharUUID);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Party")
	void Server_RequestPartyPromote(const FString& TargetCharUUID);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Party")
	void Server_RequestPlaceTacticalPing(FVector WorldLocation);

	UFUNCTION(Client, Reliable, Category = "IU|Guild")
	void Client_ReceiveGuildSnapshot(const FIU_GuildSnapshot& Snapshot);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Guild")
	void Server_RequestRefreshGuild();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Guild")
	void Server_RequestCreateGuild(const FString& GuildName, const FString& GuildTag, const FString& MOTD);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Guild")
	void Server_RequestJoinGuild(const FString& GuildID);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Guild")
	void Server_RequestLeaveGuild();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Guild")
	void Server_RequestDonateGuildCoins(int64 AmountCoin);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Guild")
	void Server_RequestUpdateGuildMOTD(const FString& NewMOTD);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Guild")
	void Server_RequestKickGuildMember(const FString& TargetCharUUID);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Guild")
	void Server_RequestSetGuildMemberRole(const FString& TargetCharUUID, const FString& NewRole);

	UFUNCTION(Client, Reliable, Category = "IU|Social")
	void Client_ReceiveChatMessage(const FIU_ChatMessageView& Message);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Social")
	void Server_RequestSendChat(const FString& Channel, const FString& Content, const FString& TargetName);

	UFUNCTION(Client, Reliable, Category = "IU|Trade")
	void Client_ReceiveTradeSnapshot(const FIU_TradeSessionSnapshot& Snapshot);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Trade")
	void Server_RequestRefreshTrade();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Trade")
	void Server_RequestTradeWithCharacter(const FString& OptionalTargetCharUUID);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Trade")
	void Server_RequestTradeRespond(const FString& TradeID, bool bAccept);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Trade")
	void Server_RequestTradeUpdateOffer(int64 CoinOffer, const TArray<FString>& ItemInstanceIDs);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Trade")
	void Server_RequestTradeLock();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Trade")
	void Server_RequestTradeConfirm();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IU|Trade")
	void Server_RequestTradeCancel();

private:
	void ApplyAuthoritativeInventorySnapshotOnServer(const FIU_InventorySnapshot& Snapshot);
	void SyncAllWorldControllersPartyStateOnServer();
	void SyncPartnerTradeStateOnServer(const FString& CharIDA, const FString& CharIDB, bool bRefreshInventories = false);
	AIU_PlayerCharacter* FindClosestOtherPlayerCharacter(float MaxDistanceCm) const;
	AIU_PlayerCharacter* GetIUPlayerCharacter() const;

	void HandleMove(const FInputActionValue& Value);
	void HandleMoveCompleted(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleCameraZoom(const FInputActionValue& Value);
	void HandleJumpStarted(const FInputActionValue& Value);
	void HandleJumpCompleted(const FInputActionValue& Value);
	void HandleDescendStarted(const FInputActionValue& Value);
	void HandleDescendCompleted(const FInputActionValue& Value);
	void HandleFlightToggle(const FInputActionValue& Value);
	void HandleSprintStarted(const FInputActionValue& Value);
	void HandleSprintCompleted(const FInputActionValue& Value);
	void HandleToggleRotationMode(const FInputActionValue& Value);
	void HandleMeleeAttack(const FInputActionValue& Value);
	void HandleKiBlast(const FInputActionValue& Value);
	void HandleDodge(const FInputActionValue& Value);
	void HandleLockOn(const FInputActionValue& Value);
	void HandleSwitchTarget(const FInputActionValue& Value);
	void HandleToggleMobilityStyle(const FInputActionValue& Value);
	void HandleToggleDebugHUD();
	void HandleToggleGlobalPvP();
	void HandleToggleAuthPanel();
	void HandleToggleCharacterProfileTab();
	void HandleTogglePotentialPanel();
	void HandleToggleInventoryPanel();
	void HandleSkillHotbar1();
	void HandleSkillHotbar2();
	void HandleSkillHotbar3();
	void HandleSkillHotbar4();
	void HandleSkillHotbar5();
	void HandleSkillHotbar6();
	void HandleQuickUseElixir();
	void HandleChatEnterKey();
	void HandleQuickPartyKey();
	void HandleQuickTradeKey();

	UPROPERTY(Transient)
	TObjectPtr<UIU_LoginCharacterWidget> ActiveAuthWidget;

	UPROPERTY(Transient)
	TObjectPtr<UIU_PotentialUpgradeWidget> ActivePotentialWidget;

	UPROPERTY(Transient)
	TObjectPtr<UIU_InventoryEquipmentWidget> ActiveInventoryWidget;

	UPROPERTY(Transient)
	TObjectPtr<UIU_PlayerTradeWidget> ActiveTradeWidget;

	FIU_InventorySnapshot CachedInventorySnapshot;
	bool bHasCachedInventorySnapshot = false;
	FIU_GuildSnapshot CachedGuildSnapshot;
	bool bHasCachedGuildSnapshot = false;
	FIU_PartySnapshot CachedPartySnapshot;
	bool bHasCachedPartySnapshot = false;
	FIU_TradeSessionSnapshot CachedTradeSnapshot;
	bool bHasCachedTradeSnapshot = false;
	double LastConsumableUseServerTime = -100.0;
};
