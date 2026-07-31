// Copyright Epic Games, Inc. All Rights Reserved.


#include "DWM_DevPlayerController.h"
#include "DWM_DevCharacter.h"
#include "DwmInteractiveDoor.h"
#include "DwmTradeTerminalActor.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"

namespace
{
	constexpr uint64 TerminalUnavailableMessageKey = 0xD0018AULL;
	constexpr uint64 DoorUnavailableMessageKey = 0xD0020000ULL;
}

void ADWM_DevPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		// These controller-level bindings let an alternate player pawn (such as the
		// CharacterCustomizer pawn) retain DWM's terminal and door interactions.
		InputComponent->BindKey(EKeys::E, IE_Pressed, this, &ADWM_DevPlayerController::InteractWithTradeTerminal);
		InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ADWM_DevPlayerController::InteractWithDoor);
	}
}

void ADWM_DevPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// get the enhanced input subsystem
	if (InputMappingContext)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(InputMappingContext, 0);
		}
	}
}

void ADWM_DevPlayerController::SetActiveTradeTerminal(ADwmTradeTerminalActor* TradeTerminal)
{
	ActiveTradeTerminal = TradeTerminal;
}

void ADWM_DevPlayerController::ClearActiveTradeTerminal(const ADwmTradeTerminalActor* TradeTerminal)
{
	if (ActiveTradeTerminal.Get() == TradeTerminal)
	{
		ActiveTradeTerminal.Reset();
	}
}

void ADWM_DevPlayerController::SetActiveDoor(ADwmInteractiveDoor* Door)
{
	ActiveDoor = Door;
}

void ADWM_DevPlayerController::ClearActiveDoor(const ADwmInteractiveDoor* Door)
{
	if (ActiveDoor.Get() == Door)
	{
		ActiveDoor.Reset();
	}
}

void ADWM_DevPlayerController::InteractWithTradeTerminal()
{
	// The controller is above the pawn in Unreal's input stack and consumes E. Route
	// the press into the native DWM character so its single priority order can handle
	// open dialogue, a nearby terminal, or a nearby NPC. Alternate pawns still retain
	// the controller-level terminal fallback below.
	if (ADWM_DevCharacter* DwmCharacter = Cast<ADWM_DevCharacter>(GetPawn()))
	{
		DwmCharacter->HandlePrimaryInteraction();
		return;
	}

	if (ADwmTradeTerminalActor* TradeTerminal = ActiveTradeTerminal.Get())
	{
		TradeTerminal->ExecuteTrade(GetPawn());
		return;
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(TerminalUnavailableMessageKey, 1.5f, FColor::Yellow,
			TEXT("No trade terminal in range."));
	}
}

void ADWM_DevPlayerController::InteractWithDoor()
{
	if (ADwmInteractiveDoor* Door = ActiveDoor.Get())
	{
		Door->ToggleDoor(GetPawn());
		return;
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(DoorUnavailableMessageKey, 1.5f, FColor::Cyan,
			TEXT("No door in range."));
	}
}
