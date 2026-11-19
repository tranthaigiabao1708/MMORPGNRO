// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_GameMode.h"
#include "Core/IU_PlayerController.h"
#include "Characters/Player/IU_PlayerCharacter.h"

AIU_GameMode::AIU_GameMode()
{
	DefaultPawnClass = AIU_PlayerCharacter::StaticClass();
	PlayerControllerClass = AIU_PlayerController::StaticClass();
}
