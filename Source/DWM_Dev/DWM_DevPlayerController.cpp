// Copyright Epic Games, Inc. All Rights Reserved.


#include "DWM_DevPlayerController.h"
#include "DWM_DevCharacter.h"
#include "DwmInteractiveDoor.h"
#include "EngineUtils.h"
#include "DwmQuitMenuWidget.h"
#include "Kismet/GameplayStatics.h"
#include "DWM_DevCharacter.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "DwmNpcActor.h"
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
		// Non-consuming, for the same reason as the character's binding: this fires on
		// every F press whether or not a DWM door is in range, so consuming it would
		// block any world actor that listens for F.
		InputComponent->BindKey(EKeys::F, IE_Pressed, this,
			&ADWM_DevPlayerController::InteractWithDoor).bConsumeInput = false;
		InputComponent->BindKey(EKeys::T, IE_Pressed, this, &ADWM_DevPlayerController::InteractWithTerminalKey);

		// bExecuteWhenPaused, or the menu it opens could never be dismissed: the game
		// is paused behind it and a normal binding stops firing.
		FInputKeyBinding& QuitBinding = InputComponent->BindKey(
			EKeys::Escape, IE_Pressed, this, &ADWM_DevPlayerController::ToggleQuitMenu);
		QuitBinding.bExecuteWhenPaused = true;
		UE_LOG(LogTemp, Warning,
			TEXT("[DWM QUIT] Escape bound on '%s' (class '%s')."),
			*GetNameSafe(this), *GetNameSafe(GetClass()));
		InputComponent->bBlockInput = false;
	}
}

void ADWM_DevPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// One shot, one second in: late enough that every placed door has run BeginPlay,
	// early enough that the player cannot have reached one yet.
	{
		FTimerHandle DoorInputHandle;
		GetWorldTimerManager().SetTimer(DoorInputHandle, this,
			&ADWM_DevPlayerController::EnableInputOnPackDoors, 1.0f, false);
	}

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

void ADWM_DevPlayerController::SetActiveNpc(ADwmNpcActor* Npc)
{
	ActiveNpc = Npc;
}

void ADWM_DevPlayerController::ClearActiveNpc(const ADwmNpcActor* Npc)
{
	if (ActiveNpc.Get() == Npc)
	{
		ActiveNpc.Reset();
	}
}

void ADWM_DevPlayerController::InteractWithTerminalKey()
{
	// Route into the character's terminal-only handler when the pawn is ours, so both
	// paths share one implementation. Alternate pawns fall through to the controller's
	// own terminal reference below.
	if (ADWM_DevCharacter* DwmCharacter = Cast<ADWM_DevCharacter>(GetPawn()))
	{
		DwmCharacter->InteractWithTerminal();
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

	// CharacterCustomizer pawns are not ADWM_DevCharacter instances. Keep their E-key
	// dialogue route at controller level, ahead of the terminal fallback.
	if (ADwmNpcActor* Npc = ActiveNpc.Get())
	{
		if (Npc->IsDialogueOpen())
		{
			Npc->AdvanceDialogue();
		}
		else
		{
			Npc->BeginDialogue(GetPawn());
		}
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

void ADWM_DevPlayerController::EnableInputOnPackDoors()
{
	if (!GetWorld())
	{
		return;
	}

	int32 Repaired = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Candidate = *It;
		if (!Candidate || Candidate->IsA(ADwmInteractiveDoor::StaticClass()))
		{
			// Our own doors are driven by their overlap sphere, not by actor input.
			continue;
		}

		// Doors only. Repairing every input-binding Blueprint would also force input on
		// actors that deliberately enable it just while the player is in range, making
		// them respond to keys from across the level.
		if (!Candidate->GetClass()->GetName().Contains(TEXT("Door"))
			&& !Candidate->GetName().Contains(TEXT("Door")))
		{
			continue;
		}

		// AND ONLY BLUEPRINTS THAT ACTUALLY BIND INPUT.
		//
		// Matching "Door" by name swept up 903 actors in the City, nearly all of them
		// static ..._door_piece meshes that are part of a building facade and listen for
		// nothing. Pushing those onto the input stack is pure overhead. A door that wants
		// a key has an InputKey event, which UBlueprintGeneratedClass records as an
		// input delegate binding -- so ask the class whether it binds input at all.
		UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(Candidate->GetClass());

		UE_LOG(LogTemp, Warning,
			TEXT("[DWM DOOR] candidate '%s' class '%s': blueprint=%s, bindings=%d, ")
			TEXT("hasInput=%s."),
			*Candidate->GetName(), *GetNameSafe(Candidate->GetClass()),
			BlueprintClass ? TEXT("yes") : TEXT("no"),
			BlueprintClass ? BlueprintClass->DynamicBindingObjects.Num() : -1,
			Candidate->InputComponent ? TEXT("yes") : TEXT("no"));

		// A BLUEPRINT DOOR IS ENOUGH; DO NOT REQUIRE A VISIBLE INPUT BINDING.
		//
		// DynamicBindingObjects reports 2 for B_Door in the editor and the cooked build
		// repaired nothing, so that list is not dependable once cooked and requiring it
		// silently disabled the whole repair. Being a Blueprint class is the
		// discriminator that matters: the facade pieces to exclude are plain
		// StaticMeshActors, which this cast rejects. Reading DynamicBindingObjects
		// through that null cast is what crashed the packaged build on the Mountain's
		// SM_CupbardDoor2 -- the City had one candidate and it was a Blueprint, so PIE
		// never reached it.
		if (!BlueprintClass)
		{
			continue;
		}

		// An InputComponent already present means the door claimed input itself and
		// there is nothing to repair -- which is what happens in the editor.
		if (Candidate->InputComponent)
		{
			continue;
		}

		Candidate->EnableInput(this);
		++Repaired;
	}

	if (Repaired > 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[DWM DOOR] Enabled input on %d door(s) that did not claim it themselves."),
			Repaired);
	}
}

void ADWM_DevPlayerController::ToggleQuitMenu()
{
	UE_LOG(LogTemp, Warning, TEXT("[DWM QUIT] Escape pressed; menu %s."),
		QuitMenu ? TEXT("open") : TEXT("closed"));

	if (QuitMenu)
	{
		// Escape closes it too, so the key that opened it always gets you out.
		QuitMenu->RemoveFromParent();
		QuitMenu = nullptr;
		UGameplayStatics::SetGamePaused(this, false);
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
		return;
	}

	QuitMenu = CreateWidget<UDwmQuitMenuWidget>(this, UDwmQuitMenuWidget::StaticClass());
	if (!QuitMenu)
	{
		return;
	}

	QuitMenu->AddToViewport(1000);
	UGameplayStatics::SetGamePaused(this, true);

	// GameAndUI rather than UIOnly: the pause already stops the world, and UIOnly has
	// a habit of swallowing the Escape that should close this again.
	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(QuitMenu->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	SetShowMouseCursor(true);
}

void ADWM_DevPlayerController::InteractWithDoor()
{
	// TEMPORARY DIAGNOSTIC for the City door, which works in PIE by every route and
	// fails in the packaged build. Reasoning from the outside has produced three wrong
	// theories (pawn class, key consumption, cooking), so this reports what is actually
	// true at the moment F is pressed.
	//
	// The decisive fact is whether the pack's B_Door ever had EnableInput called on it:
	// EnableInput is what creates an actor's InputComponent and puts it on the
	// controller's input stack, so a null InputComponent means the door is not
	// listening and F could never have reached it.
	{
		const FVector From = GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector;
		UE_LOG(LogTemp, Warning, TEXT("[DWM DOOR] F pressed. Pawn '%s' (class '%s') at %s."),
			*GetNameSafe(GetPawn()), *GetNameSafe(GetPawn() ? GetPawn()->GetClass() : nullptr),
			*From.ToCompactString());

		int32 Reported = 0;
		for (TActorIterator<AActor> It(GetWorld()); It && Reported < 5; ++It)
		{
			AActor* Candidate = *It;
			// Match the ACTOR name as well as the class. The City door is named
			// Building33_GF3C10_B_Door0 with class B_Door_C -- either could be the one
			// carrying "Door", and missing the door entirely would waste the whole run.
			if (!Candidate
				|| (!Candidate->GetClass()->GetName().Contains(TEXT("Door"))
					&& !Candidate->GetName().Contains(TEXT("Door"))))
			{
				continue;
			}

			const float Distance = FVector::Dist(From, Candidate->GetActorLocation());
			if (Distance > 1000.0f)
			{
				continue;
			}

			UE_LOG(LogTemp, Warning,
				TEXT("[DWM DOOR]   '%s' (class '%s') at %.0f cm: InputComponent=%s, ")
				TEXT("input stack=%s, tick=%s."),
				*Candidate->GetName(), *GetNameSafe(Candidate->GetClass()), Distance,
				Candidate->InputComponent ? TEXT("YES") : TEXT("NO (EnableInput never ran)"),
				(Candidate->InputComponent && CurrentInputStack.Contains(Candidate->InputComponent))
					? TEXT("yes") : TEXT("no"),
				Candidate->IsActorTickEnabled() ? TEXT("on") : TEXT("off"));
			++Reported;
		}

		if (Reported == 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[DWM DOOR]   No actor with 'Door' in its class name within 10 m."));
		}
	}

	// ADWM_DevCharacter binds F itself and toggles the same door. While our binding
	// consumed the key this never mattered; once it stopped consuming, both handlers
	// ran, ToggleDoor fired twice, and the door opened and shut in one frame -- which
	// looks exactly like a door that does not work. This binding exists only so an
	// alternate pawn keeps door interaction, so stand down when the character is here.
	if (Cast<ADWM_DevCharacter>(GetPawn()))
	{
		return;
	}

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
