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

	/** Escape opens a confirmation panel with Keep Playing / Quit to Desktop.
	    A packaged build otherwise has no way out but Alt+F4, and Escape does nothing
	    by default in a shipping game. Confirmed rather than immediate, because the
	    key is easy to hit by accident and Hank's errand progress does not survive
	    the process. */
	void ToggleQuitMenu();

	UPROPERTY(Transient)
	class UDwmQuitMenuWidget* QuitMenu = nullptr;

	/** Give asset-pack doors the input they failed to claim for themselves.

	    Doors like AmericanCityPacks B_Door call Enable Input (Get Player Controller 0)
	    on BeginPlay. In a packaged build that controller can still be null at that
	    moment, so EnableInput quietly does nothing and the door never listens for its
	    key. PIE hides this because the world is already running when BeginPlay fires.
	    Measured: identical door and pawn reported InputComponent=YES in PIE and NO
	    packaged. Running once after BeginPlay, when this controller definitely
	    exists, repairs it without touching the vendor Blueprint. */
	void EnableInputOnPackDoors();

	TWeakObjectPtr<ADwmTradeTerminalActor> ActiveTradeTerminal;
	TWeakObjectPtr<ADwmInteractiveDoor> ActiveDoor;
	TWeakObjectPtr<ADwmNpcActor> ActiveNpc;
};
