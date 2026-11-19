// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_GameMode.h"
#include "Core/IU_PlayerController.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "UI/IU_HUDManager.h"

AIU_GameMode::AIU_GameMode()
{
	DefaultPawnClass = AIU_PlayerCharacter::StaticClass();
	PlayerControllerClass = AIU_PlayerController::StaticClass();
	HUDClass = AIU_HUDManager::StaticClass();
}
