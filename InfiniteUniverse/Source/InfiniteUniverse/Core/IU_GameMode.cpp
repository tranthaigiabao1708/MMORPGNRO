// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_GameMode.h"
#include "Core/IU_GameState.h"
#include "Core/IU_PlayerState.h"
#include "Core/IU_PlayerController.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "UI/IU_HUDManager.h"
#include "EngineUtils.h"
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

void AIU_GameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	UE_LOG(LogInfiniteUniverse, Log, TEXT("[GameMode] Player connected: %s (Total Players: %d)"),
		*GetNameSafe(NewPlayer), GetNumPlayers());
}

void AIU_GameMode::Logout(AController* Exiting)
{
	APawn* ExitingPawn = Exiting ? Exiting->GetPawn() : nullptr;
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
	}
}
