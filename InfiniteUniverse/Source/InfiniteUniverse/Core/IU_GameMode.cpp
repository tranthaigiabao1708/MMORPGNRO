// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_GameMode.h"
#include "Core/IU_GameState.h"
#include "Core/IU_PlayerState.h"
#include "Core/IU_PlayerController.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Attributes/IU_AttributeComponent.h"
#include "Network/IU_BackendSubsystem.h"
#include "UI/IU_HUDManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameSession.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "CoreGlobals.h"
#include "InfiniteUniverse.h"

AIU_GameMode::AIU_GameMode()
{
	DefaultPawnClass = AIU_PlayerCharacter::StaticClass();
	PlayerControllerClass = AIU_PlayerController::StaticClass();
	GameStateClass = AIU_GameState::StaticClass();
	PlayerStateClass = AIU_PlayerState::StaticClass();
	HUDClass = AIU_HUDManager::StaticClass();
}

void AIU_GameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	if (!GameStateClass || GameStateClass == AGameStateBase::StaticClass())
	{
		GameStateClass = AIU_GameState::StaticClass();
	}
	if (!PlayerStateClass || PlayerStateClass == APlayerState::StaticClass())
	{
		PlayerStateClass = AIU_PlayerState::StaticClass();
	}
}

void AIU_GameMode::StartPlay()
{
	Super::StartPlay();

	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (World && Backend && Backend->IsBackendBridgeEnabled())
	{
		World->GetTimerManager().SetTimer(
			LeaseRenewTimerHandle,
			this,
			&AIU_GameMode::HandleLeaseRenewTick,
			12.0f,
			true);

		World->GetTimerManager().SetTimer(
			PeriodicSaveTimerHandle,
			this,
			&AIU_GameMode::HandlePeriodicSaveTick,
			25.0f,
			true);

		UE_LOG(LogInfiniteUniverse, Log,
			TEXT("[GameMode] Step 3 Lease Renewal (12s) & Periodic Checkpoint Save (25s) timers active."));
	}
}

void AIU_GameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LeaseRenewTimerHandle);
		World->GetTimerManager().ClearTimer(PeriodicSaveTimerHandle);

		// Flush final state on orderly Dedicated Server shutdown for any remaining valid leased players
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UIU_BackendSubsystem* Backend = GI->GetSubsystem<UIU_BackendSubsystem>())
			{
				if (Backend->IsBackendBridgeEnabled())
				{
					for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
					{
						APlayerController* PC = It->Get();
						if (!PC) { continue; }
						if (AIU_PlayerState* PS = PC->GetPlayerState<AIU_PlayerState>())
						{
							if (PS->HasValidBackendLease())
							{
								FIU_CharacterSavePayload ShutdownSave;
								const bool bBuilt = BuildCharacterSavePayload(PC, TEXT("LOGOUT"), ShutdownSave);
								Backend->ReleaseCharacterLease(
									PS->GetPersistedCharacterUUID(),
									PS->GetActiveFencingEpoch(),
									bBuilt ? &ShutdownSave : nullptr);
								PS->MarkLeaseRevoked();
							}
						}
					}
				}
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AIU_GameMode::PreLogin(
	const FString& Options,
	const FString& Address,
	const FUniqueNetIdRepl& UniqueId,
	FString& ErrorMessage)
{
	UGameInstance* GI = GetGameInstance();
	const UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	const bool bRequireTicket = bForceRequireTicketForTest || (Backend && Backend->ShouldRequireBackendTicket());

	if (bRequireTicket)
	{
		const FString Ticket = UGameplayStatics::ParseOption(Options, TEXT("Ticket")).TrimStartAndEnd();
		if (Ticket.IsEmpty())
		{
			ErrorMessage = TEXT("ERR_MISSING_WORLD_TICKET");
			UE_LOG(LogInfiniteUniverse, Warning,
				TEXT("[Security] PreLogin REJECTED connection from '%s': missing required one-time ?Ticket= parameter."),
				*Address);
			return;
		}
	}

	if (GameSession && GetWorld() && GetWorld()->GetAuthGameMode() == this)
	{
		Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	}
}

APlayerController* AIU_GameMode::Login(
	UPlayer* NewPlayer,
	ENetRole InRemoteRole,
	const FString& Portal,
	const FString& Options,
	const FUniqueNetIdRepl& UniqueId,
	FString& ErrorMessage)
{
	APlayerController* NewPC = Super::Login(NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage);
	if (NewPC && ErrorMessage.IsEmpty())
	{
		const FString Ticket = UGameplayStatics::ParseOption(Options, TEXT("Ticket")).TrimStartAndEnd();
		if (!Ticket.IsEmpty())
		{
			PendingTicketsByController.Add(NewPC, Ticket);
		}
	}
	return NewPC;
}

void AIU_GameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	UE_LOG(LogInfiniteUniverse, Log, TEXT("[GameMode] Player connected: %s (Total Players: %d)"),
		*GetNameSafe(NewPlayer), GetNumPlayers());

	if (!NewPlayer)
	{
		return;
	}

	FString ConnectTicket;
	if (FString* FoundTicket = PendingTicketsByController.Find(NewPlayer))
	{
		ConnectTicket = *FoundTicket;
		PendingTicketsByController.Remove(NewPlayer);
	}

	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!Backend || (!Backend->IsBackendBridgeEnabled() && ConnectTicket.IsEmpty()))
	{
		return;
	}

	if (ConnectTicket.IsEmpty())
	{
		if (Backend->ShouldRequireBackendTicket())
		{
			KickPlayerControllerWithReason(NewPlayer, TEXT("Missing world connect ticket"));
		}
		return;
	}

	Backend->SetBackendBridgeEnabled(true);
	TWeakObjectPtr<AIU_GameMode> WeakGM(this);
	TWeakObjectPtr<APlayerController> WeakPC(NewPlayer);

	Backend->ClaimCharacterLease(ConnectTicket,
		[WeakGM, WeakPC, ConnectTicket](bool bSuccess, const FIU_BackendCharacterSummary& LoadedState, const FString& ErrorMsg)
		{
			AIU_GameMode* Self = WeakGM.Get();
			APlayerController* PC = WeakPC.Get();
			if (!Self || !IsValid(PC))
			{
				return;
			}

			if (!bSuccess)
			{
				UE_LOG(LogInfiniteUniverse, Error,
					TEXT("[Security] ClaimCharacterLease FAILED for '%s' (Ticket '%s'): %s -> Kicking client!"),
					*GetNameSafe(PC), *ConnectTicket.Left(16), *ErrorMsg);
				if (AIU_PlayerState* PS = PC->GetPlayerState<AIU_PlayerState>())
				{
					PS->MarkLeaseRevoked();
				}
				Self->KickPlayerControllerWithReason(PC, TEXT("Invalid or expired world connect ticket"));
				return;
			}

			if (AIU_PlayerState* PS = PC->GetPlayerState<AIU_PlayerState>())
			{
				PS->ApplyLoadedBackendSummary(LoadedState);
			}

			if (AIU_PlayerCharacter* PChar = Cast<AIU_PlayerCharacter>(PC->GetPawn()))
			{
				if (UIU_AttributeComponent* Attr = PChar->GetAttributeComponent())
				{
					Attr->ApplyPersistedVitals(
						LoadedState.MaxHP,
						LoadedState.HP,
						LoadedState.MaxEnergy,
						LoadedState.Energy,
						LoadedState.AttackPowerMultiplier,
						LoadedState.AttackPower,
						LoadedState.Defense);
				}

				if (LoadedState.bHasSavedPosition)
				{
					PChar->TeleportToPersistedTransform(
						LoadedState.SavedPosition,
						FRotator(0.0f, LoadedState.SavedYaw, 0.0f));
				}
				else
				{
					// First-ever world join for this character: persist its safe non-overlapping spawn slot immediately!
					FIU_CharacterSavePayload InitialSave;
					if (Self->BuildCharacterSavePayload(PC, TEXT("PERIODIC_60S"), InitialSave))
					{
						if (UGameInstance* InnerGI = Self->GetGameInstance())
						{
							if (UIU_BackendSubsystem* InnerBackend = InnerGI->GetSubsystem<UIU_BackendSubsystem>())
							{
								InnerBackend->SaveCharacterCheckpoint(InitialSave);
							}
						}
					}
				}
			}

			if (AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(PC))
			{
				IUPC->Server_RequestRefreshInventory();
			}

			UE_LOG(LogInfiniteUniverse, Log,
				TEXT("[Persistence] Restored Character '%s' (ID=%s, Epoch=#%lld, Lv=%d, EXP=%lld/%lld, HP=%.0f/%.0f, Pos=(%.1f, %.1f, %.1f), HasSavedPos=%d)"),
				*LoadedState.CharacterName,
				*LoadedState.CharacterID,
				LoadedState.FencingEpoch,
				LoadedState.Level,
				LoadedState.CurrentExp,
				LoadedState.NextLevelExp,
				LoadedState.HP,
				LoadedState.MaxHP,
				LoadedState.SavedPosition.X,
				LoadedState.SavedPosition.Y,
				LoadedState.SavedPosition.Z,
				LoadedState.bHasSavedPosition ? 1 : 0);
		});
}

void AIU_GameMode::Logout(AController* Exiting)
{
	APawn* ExitingPawn = Exiting ? Exiting->GetPawn() : nullptr;

	// Flush final LOGOUT snapshot & release lease BEFORE Pawn is detached/destroyed (ONLY if lease was NOT revoked!)
	if (Exiting)
	{
		if (AIU_PlayerState* PS = Exiting->GetPlayerState<AIU_PlayerState>())
		{
			if (PS->HasValidBackendLease())
			{
				if (UGameInstance* GI = GetGameInstance())
				{
					if (UIU_BackendSubsystem* Backend = GI->GetSubsystem<UIU_BackendSubsystem>())
					{
						FIU_CharacterSavePayload FinalSave;
						const bool bBuilt = BuildCharacterSavePayload(Exiting, TEXT("LOGOUT"), FinalSave);
						Backend->ReleaseCharacterLease(
							PS->GetPersistedCharacterUUID(),
							PS->GetActiveFencingEpoch(),
							bBuilt ? &FinalSave : nullptr);
						PS->MarkLeaseRevoked();
					}
				}
			}
		}
	}

	if (APlayerController* ExitingPC = Cast<APlayerController>(Exiting))
	{
		PendingTicketsByController.Remove(ExitingPC);
	}

	Super::Logout(Exiting);

	if (IsValid(ExitingPawn) && !ExitingPawn->IsActorBeingDestroyed())
	{
		ExitingPawn->Destroy();
	}

	int32 RemainingPlayerPawns = 0;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AIU_PlayerCharacter> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->IsActorBeingDestroyed())
			{
				++RemainingPlayerPawns;
			}
		}
	}

	UE_LOG(LogInfiniteUniverse, Log, TEXT("[GameMode] Player disconnected: %s (Active Player Pawns Remaining: %d)"),
		*GetNameSafe(Exiting), RemainingPlayerPawns);
}

bool AIU_GameMode::BuildCharacterSavePayload(
	AController* Controller,
	const FString& SnapshotReason,
	FIU_CharacterSavePayload& OutPayload) const
{
	if (!Controller)
	{
		return false;
	}
	AIU_PlayerState* PS = Controller->GetPlayerState<AIU_PlayerState>();
	if (!PS || !PS->HasValidBackendLease())
	{
		return false;
	}

	OutPayload.CharacterID = PS->GetPersistedCharacterUUID();
	OutPayload.FencingEpoch = PS->GetActiveFencingEpoch();
	OutPayload.SnapshotReason = SnapshotReason;
	OutPayload.EnemyKills = PS->GetEnemyKills();
	OutPayload.PvPKills = PS->GetPvPKills();
	OutPayload.Deaths = PS->GetDeaths();

	if (const APawn* Pawn = Controller->GetPawn())
	{
		OutPayload.Position = Pawn->GetActorLocation();
		OutPayload.Yaw = Pawn->GetActorRotation().Yaw;
		if (const UIU_AttributeComponent* Attr = Pawn->FindComponentByClass<UIU_AttributeComponent>())
		{
			OutPayload.bIsAlive = !Attr->IsDead();
			OutPayload.HP = Attr->GetHP();
			OutPayload.Energy = Attr->GetKi();
		}
		PS->CachePawnWorldSnapshot(OutPayload.Position, OutPayload.Yaw, OutPayload.HP, OutPayload.Energy, OutPayload.bIsAlive);
		return true;
	}

	// During UDP disconnect, APlayerController::Destroyed() unpossesses Pawn right before calling GameMode::Logout!
	return PS->GetCachedPawnWorldSnapshot(
		OutPayload.Position,
		OutPayload.Yaw,
		OutPayload.HP,
		OutPayload.Energy,
		OutPayload.bIsAlive);
}

void AIU_GameMode::HandleLeaseRenewTick()
{
	UWorld* World = GetWorld();
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!World || !Backend || !Backend->IsBackendBridgeEnabled())
	{
		return;
	}

	TArray<FIU_LeaseRenewalEntry> ActiveLeases;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* PC = It->Get())
		{
			if (const AIU_PlayerState* PS = PC->GetPlayerState<AIU_PlayerState>())
			{
				if (PS->HasValidBackendLease())
				{
					FIU_LeaseRenewalEntry Entry;
					Entry.CharacterID = PS->GetPersistedCharacterUUID();
					Entry.FencingEpoch = PS->GetActiveFencingEpoch();
					ActiveLeases.Add(Entry);
				}
			}
		}
	}

	if (ActiveLeases.Num() == 0)
	{
		return;
	}

	TWeakObjectPtr<AIU_GameMode> WeakGM(this);
	Backend->RenewActiveLeases(ActiveLeases,
		[WeakGM](bool bSuccess, const TArray<FString>& RevokedCharIDs)
		{
			AIU_GameMode* Self = WeakGM.Get();
			if (!Self || !bSuccess || RevokedCharIDs.Num() == 0)
			{
				return;
			}
			UWorld* InnerWorld = Self->GetWorld();
			if (!InnerWorld)
			{
				return;
			}

			TArray<APlayerController*> ToKick;
			for (FConstPlayerControllerIterator It = InnerWorld->GetPlayerControllerIterator(); It; ++It)
			{
				APlayerController* PC = It->Get();
				if (!IsValid(PC)) { continue; }
				if (AIU_PlayerState* PS = PC->GetPlayerState<AIU_PlayerState>())
				{
					if (RevokedCharIDs.Contains(PS->GetPersistedCharacterUUID()))
					{
						UE_LOG(LogInfiniteUniverse, Warning,
							TEXT("[LeaseFence] Lease REVOKED for Character '%s' (%s) — Duplicate login / epoch superseded! Evicting stale session WITHOUT saving."),
							*PS->GetPersistedCharacterName(), *PS->GetPersistedCharacterUUID());
						PS->MarkLeaseRevoked();
						ToKick.Add(PC);
					}
				}
			}

			for (APlayerController* StalePC : ToKick)
			{
				Self->KickPlayerControllerWithReason(StalePC, TEXT("Account logged in from another location (Lease Superseded)"));
			}
		});
}

void AIU_GameMode::HandlePeriodicSaveTick()
{
	UWorld* World = GetWorld();
	UGameInstance* GI = GetGameInstance();
	UIU_BackendSubsystem* Backend = GI ? GI->GetSubsystem<UIU_BackendSubsystem>() : nullptr;
	if (!World || !Backend || !Backend->IsBackendBridgeEnabled())
	{
		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		FIU_CharacterSavePayload Payload;
		if (BuildCharacterSavePayload(PC, TEXT("PERIODIC_60S"), Payload))
		{
			TWeakObjectPtr<APlayerController> WeakPC(PC);
			TWeakObjectPtr<AIU_GameMode> WeakGM(this);
			Backend->SaveCharacterCheckpoint(Payload,
				[WeakGM, WeakPC](bool bSaved, const FString& ErrMsg)
				{
					if (!bSaved && ErrMsg.Contains(TEXT("409")))
					{
						if (AIU_GameMode* Self = WeakGM.Get())
						{
							if (APlayerController* InnerPC = WeakPC.Get())
							{
								if (AIU_PlayerState* PS = InnerPC->GetPlayerState<AIU_PlayerState>())
								{
									PS->MarkLeaseRevoked();
								}
								Self->KickPlayerControllerWithReason(InnerPC, TEXT("Character lease superseded (409 Fence)"));
							}
						}
					}
				});
		}
	}
}

void AIU_GameMode::KickPlayerControllerWithReason(APlayerController* TargetPC, const FString& Reason)
{
	if (!IsValid(TargetPC))
	{
		return;
	}
	if (GameSession)
	{
		GameSession->KickPlayer(TargetPC, FText::FromString(Reason));
	}
	else
	{
		TargetPC->ClientReturnToMainMenuWithTextReason(FText::FromString(Reason));
		TargetPC->Destroy();
	}
}

FTransform AIU_GameMode::AllocateMultiplayerSpawnTransform(AController* NewPlayer)
{
	AActor* StartSpot = FindPlayerStart(NewPlayer);
	FTransform BaseTM = StartSpot
		? StartSpot->GetActorTransform()
		: FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, 110.0f));

	const int32 SlotIdx = NextSpawnSlotIndex++;
	if (SlotIdx > 0)
	{
		// Distribute additional multiplayer clients cleanly on a 180 cm ring so capsules never collide on spawn
		const float AngleRad = FMath::DegreesToRadians(static_cast<float>((SlotIdx - 1) * 72 + 35));
		const float Radius = 180.0f + (45.0f * (SlotIdx / 5));
		const FVector Offset(FMath::Cos(AngleRad) * Radius, FMath::Sin(AngleRad) * Radius, 8.0f);
		BaseTM.AddToTranslation(Offset);
	}

	return BaseTM;
}

void AIU_GameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer || NewPlayer->IsPendingKillPending())
	{
		return;
	}

	const FTransform BaseTM = AllocateMultiplayerSpawnTransform(NewPlayer);
	RestartPlayerAtTransform(NewPlayer, BaseTM);

	if (AIU_PlayerCharacter* PChar = Cast<AIU_PlayerCharacter>(NewPlayer->GetPawn()))
	{
		PChar->UpdateSavedSpawnTransform(BaseTM);

		// In interactive gameplay (non-AutomationTest), initialize NRO starting baseline: 200 HP, 200 Ki, 100 Sức đánh, 20 Giáp!
		if (!GIsAutomationTesting)
		{
			if (UIU_AttributeComponent* Attr = PChar->GetAttributeComponent())
			{
				Attr->ApplyPersistedVitals(200.0f, 200.0f, 200.0f, 200.0f, 1.0f, 100.0f, 20.0f);
			}
			if (AIU_PlayerState* PS = NewPlayer->GetPlayerState<AIU_PlayerState>())
			{
				if (!PS->HasValidBackendLease())
				{
					PS->ApplyAuthoritativeEconomySummary(0, 3, 4, 0);
				}
			}
		}
	}
}
