// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "DWM_DevCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class ADwmTradeTerminalActor;
class ADwmInteractiveDoor;
class ADwmNpcActor;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

UCLASS(config=Game)
class ADWM_DevCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Pawn mesh: 1st person view (arms; seen only by self) */
	UPROPERTY(VisibleDefaultsOnly, Category=Mesh)
	USkeletalMeshComponent* Mesh1P;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

	/** MappingContext */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputAction* MoveAction;
	
public:
	ADWM_DevCharacter();

protected:
	virtual void BeginPlay();

public:
		
	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* LookAction;

	/** Bool for AnimBP to switch to another animation set */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	bool bHasRifle;

	/** Setter to set the bool */
	UFUNCTION(BlueprintCallable, Category = Weapon)
	void SetHasRifle(bool bNewHasRifle);

	/** Getter for the bool */
	UFUNCTION(BlueprintCallable, Category = Weapon)
	bool GetHasRifle();

	/** Called by a nearby Day 18 trade terminal while the player is in interaction range. */
	void SetActiveTradeTerminal(ADwmTradeTerminalActor* TradeTerminal);
	void ClearActiveTradeTerminal(ADwmTradeTerminalActor* TradeTerminal);

	/** Called by an interactive door while the player is in its interaction range. */
	void SetActiveDoor(ADwmInteractiveDoor* Door);
	void ClearActiveDoor(ADwmInteractiveDoor* Door);

	/** Called by a nearby NPC while the player is in conversation range. */
	void SetActiveNpc(ADwmNpcActor* Npc);
	void ClearActiveNpc(ADwmNpcActor* Npc);

	/**
	 * Runs the primary E-key interaction. The player controller calls this instead of
	 * handling E as terminal-only, so NPC dialogue and terminals share one deliberate
	 * priority order without competing input bindings.
	 */
	void HandlePrimaryInteraction();

protected:
	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

	/** Door interaction bound to F, kept separate from the terminal's E interaction. */
	void InteractWithDoor();

protected:
	// APawn interface
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;
	// End of APawn interface

	TWeakObjectPtr<ADwmTradeTerminalActor> ActiveTradeTerminal;
	TWeakObjectPtr<ADwmInteractiveDoor> ActiveDoor;
	TWeakObjectPtr<ADwmNpcActor> ActiveNpc;

public:
	/** Returns Mesh1P subobject **/
	USkeletalMeshComponent* GetMesh1P() const { return Mesh1P; }
	/** Returns FirstPersonCameraComponent subobject **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

};

