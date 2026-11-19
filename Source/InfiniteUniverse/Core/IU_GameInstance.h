// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "IU_GameInstance.generated.h"

/**
 * Prototype GameInstance for global non-actor configuration and lifecycle hooks.
 */
UCLASS()
class INFINITEUNIVERSE_API UIU_GameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
};
