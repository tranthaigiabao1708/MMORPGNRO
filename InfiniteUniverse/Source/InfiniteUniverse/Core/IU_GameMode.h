// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "IU_GameMode.generated.h"

/**
 * Default GameMode for Step 1 Core Gameplay Prototype.
 * Assigns AIU_PlayerCharacter and AIU_PlayerController as defaults, overridable via Blueprint.
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_GameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AIU_GameMode();
};
