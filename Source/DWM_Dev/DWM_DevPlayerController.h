// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DWM_DevPlayerController.generated.h"

class UInputMappingContext;
class ADwmInteractiveDoor;
class ADwmTradeTerminalActor;

/**
 *
 */
UCLASS()
class DWM_DEV_API ADWM_DevPlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:

	/** Input Mapping Context to be used for player input */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputMappingContext* InputMappingContext;

	virtual void SetupInputComponent() override;

	virtual void BeginPlay() override;

public:
	/** Called by a DWM trade terminal when the possessed pawn enters its range. */
	void SetActiveTradeTerminal(ADwmTradeTerminalActor* TradeTerminal);
	void ClearActiveTradeTerminal(const ADwmTradeTerminalActor* TradeTerminal);

	/** Called by a DWM door when the possessed pawn enters its range. */
	void SetActiveDoor(ADwmInteractiveDoor* Door);
	void ClearActiveDoor(const ADwmInteractiveDoor* Door);

private:
	void InteractWithTradeTerminal();
	void InteractWithDoor();

	TWeakObjectPtr<ADwmTradeTerminalActor> ActiveTradeTerminal;
	TWeakObjectPtr<ADwmInteractiveDoor> ActiveDoor;
};
