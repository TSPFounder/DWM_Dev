// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DWM_DevPlayerController.generated.h"

class UInputMappingContext;
class ADwmInteractiveDoor;
class ADwmNpcActor;
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

	/** Called by a DWM NPC when the possessed pawn enters its conversation range. */
	void SetActiveNpc(ADwmNpcActor* Npc);
	void ClearActiveNpc(const ADwmNpcActor* Npc);

private:
	void InteractWithTradeTerminal();
	/** T-key terminal interaction for alternate pawns -- mirrors the character's own
	    dedicated terminal key so a CharacterCustomizer pawn behaves the same (issue #20). */
	void InteractWithTerminalKey();
	void InteractWithDoor();

	TWeakObjectPtr<ADwmTradeTerminalActor> ActiveTradeTerminal;
	TWeakObjectPtr<ADwmInteractiveDoor> ActiveDoor;
	TWeakObjectPtr<ADwmNpcActor> ActiveNpc;
};
