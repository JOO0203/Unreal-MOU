// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TeamProject_MOUGameMode.generated.h"

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class ATeamProject_MOUGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	ATeamProject_MOUGameMode();
};



