// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DWM_DevGameMode.generated.h"

UCLASS(minimalapi)
class ADWM_DevGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADWM_DevGameMode();

protected:
	virtual void BeginPlay() override;
};



