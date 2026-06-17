// Copyright Epic Games, Inc. All Rights Reserved.

#include "DWM_DevGameMode.h"
#include "DWM_DevCharacter.h"
#include "UObject/ConstructorHelpers.h"

ADWM_DevGameMode::ADWM_DevGameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;

}
