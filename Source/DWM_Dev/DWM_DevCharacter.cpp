// Copyright Epic Games, Inc. All Rights Reserved.

#include "DWM_DevCharacter.h"
#include "DWM_DevProjectile.h"
#include "DwmInteractiveDoor.h"
#include "DwmNpcActor.h"
#include "DwmTradeTerminalActor.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// ADWM_DevCharacter

ADWM_DevCharacter::ADWM_DevCharacter()
{
	// Character doesnt have a rifle at start
	bHasRifle = false;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// Create a CameraComponent
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-10.f, 0.f, 60.f)); // Position the camera
	FirstPersonCameraComponent->bUsePawnControlRotation = true;

	// Create a mesh component that will be used when being viewed from a '1st person' view (when controlling this pawn)
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(FirstPersonCameraComponent);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	//Mesh1P->SetRelativeRotation(FRotator(0.9f, -19.19f, 5.2f));
	Mesh1P->SetRelativeLocation(FVector(-30.f, 0.f, -150.f));

}

void ADWM_DevCharacter::BeginPlay()
{
	// Call the base class
	Super::BeginPlay();

	// Add Input Mapping Context
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}

}

//////////////////////////////////////////////////////////////////////////// Input

void ADWM_DevCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// EnhancedInputComponent deliberately deletes its legacy BindKey overload. Binding through
	// the base input component keeps this temporary Day 18 interaction independent of a new
	// Input Action asset, while the existing movement actions stay on Enhanced Input below.
	PlayerInputComponent->BindKey(EKeys::E, IE_Pressed, this, &ADWM_DevCharacter::HandlePrimaryInteraction);
	PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &ADWM_DevCharacter::InteractWithDoor);
	// T for Trade. See InteractWithTerminal's declaration for why terminals no longer
	// share E with dialogue (issue #20).
	PlayerInputComponent->BindKey(EKeys::T, IE_Pressed, this, &ADWM_DevCharacter::InteractWithTerminal);

	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ADWM_DevCharacter::Move);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADWM_DevCharacter::Look);

	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}


void ADWM_DevCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add movement
		AddMovementInput(GetActorForwardVector(), MovementVector.Y);
		AddMovementInput(GetActorRightVector(), MovementVector.X);
	}
}

void ADWM_DevCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void ADWM_DevCharacter::SetHasRifle(bool bNewHasRifle)
{
	bHasRifle = bNewHasRifle;
}

bool ADWM_DevCharacter::GetHasRifle()
{
	return bHasRifle;
}

void ADWM_DevCharacter::SetActiveTradeTerminal(ADwmTradeTerminalActor* TradeTerminal)
{
	ActiveTradeTerminal = TradeTerminal;
}

void ADWM_DevCharacter::ClearActiveTradeTerminal(ADwmTradeTerminalActor* TradeTerminal)
{
	if (ActiveTradeTerminal.Get() == TradeTerminal)
	{
		ActiveTradeTerminal.Reset();
	}
}

void ADWM_DevCharacter::SetActiveDoor(ADwmInteractiveDoor* Door)
{
	ActiveDoor = Door;
}

void ADWM_DevCharacter::ClearActiveDoor(ADwmInteractiveDoor* Door)
{
	if (ActiveDoor.Get() == Door)
	{
		ActiveDoor.Reset();
	}
}

void ADWM_DevCharacter::SetActiveNpc(ADwmNpcActor* Npc)
{
	ActiveNpc = Npc;
}

void ADWM_DevCharacter::ClearActiveNpc(ADwmNpcActor* Npc)
{
	if (ActiveNpc.Get() == Npc)
	{
		ActiveNpc.Reset();
	}
}

void ADWM_DevCharacter::HandlePrimaryInteraction()
{
	if (ADwmNpcActor* Npc = ActiveNpc.Get())
	{
		if (Npc->IsDialogueOpen())
		{
			Npc->AdvanceDialogue();
			return;
		}
	}

	// The controller owns the top-level E binding and some imported player setups do
	// not reliably generate the NPC sphere overlap. Resolve a nearby NPC directly so
	// dialogue remains usable even when that overlap handoff is missed. Three and a
	// half metres is intentionally only a little larger than Hank's visible prompt
	// radius and is too short to select him accidentally from elsewhere in Mountain.
	// ALWAYS re-resolve, not only when nothing is set.
	//
	// SetActiveNpc is last-writer-wins, so with two NPCs in range -- two people on one
	// couch -- whichever fired its overlap LAST owned the prompt regardless of who the
	// player walked up to. Approaching Sophia and getting Nathan looks exactly like the
	// two of them having swapped dialogue, which is how it was reported.
	{
		constexpr float NpcInteractionFallbackRadius = 350.0f;
		const float MaxDistanceSquared = FMath::Square(NpcInteractionFallbackRadius);

		// Whoever the player is LOOKING AT, not whoever is marginally closer. Two people
		// sitting side by side are at nearly equal distance, so distance alone cannot tell
		// them apart -- but the camera is aimed squarely at one of them.
		const FVector ViewForward = GetControlRotation().Vector();
		ADwmNpcActor* BestNpc = nullptr;
		float BestAlignment = -2.0f;
		float BestDistanceSquared = MaxDistanceSquared;

		for (TActorIterator<ADwmNpcActor> It(GetWorld()); It; ++It)
		{
			ADwmNpcActor* Candidate = *It;
			if (!Candidate)
			{
				continue;
			}

			FVector ToCandidate = Candidate->GetActorLocation() - GetActorLocation();
			const float DistanceSquared = ToCandidate.SizeSquared();
			if (DistanceSquared > MaxDistanceSquared)
			{
				continue;
			}

			ToCandidate.Z = 0.0f;
			const float Alignment = ToCandidate.IsNearlyZero()
				? 1.0f
				: FVector::DotProduct(ToCandidate.GetSafeNormal(), ViewForward);

			// Distance only breaks ties between two the player is aimed at equally.
			if (Alignment > BestAlignment + KINDA_SMALL_NUMBER
				|| (FMath::IsNearlyEqual(Alignment, BestAlignment) && DistanceSquared < BestDistanceSquared))
			{
				BestAlignment = Alignment;
				BestDistanceSquared = DistanceSquared;
				BestNpc = Candidate;
			}
		}

		if (BestNpc)
		{
			if (BestNpc != ActiveNpc.Get())
			{
				UE_LOG(LogTemplateCharacter, Log,
					TEXT("[DWM Interaction] Talking to '%s' at %.0f cm (aim %.2f), not the last NPC to overlap."),
					*GetNameSafe(BestNpc), FMath::Sqrt(BestDistanceSquared), BestAlignment);
			}
			ActiveNpc = BestNpc;
		}
	}

	// A nearby NPC wins over a terminal when both ranges overlap. This keeps E
	// predictable beside Hank while preserving terminal interaction everywhere else.
	if (ADwmNpcActor* Npc = ActiveNpc.Get())
	{
		Npc->BeginDialogue(this);
		return;
	}

	if (ADwmTradeTerminalActor* TradeTerminal = ActiveTradeTerminal.Get())
	{
		// Day 20: renamed from ExecuteDemoTrade -- this terminal may now be configured with
		// any storyline trade, not just the original Mountain/Valley demo, so "Demo" no
		// longer describes what it does.
		TradeTerminal->ExecuteTrade(this);
		return;
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(0xD0018EULL, 2.0f, FColor::Yellow,
			TEXT("Nothing to interact with."));
	}
}

void ADWM_DevCharacter::InteractWithTerminal()
{
	// Terminal-only on purpose: no NPC check here, because the whole point of the
	// separate key is that a terminal beside an NPC stays reachable.
	if (ADwmTradeTerminalActor* TradeTerminal = ActiveTradeTerminal.Get())
	{
		TradeTerminal->ExecuteTrade(this);
		return;
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(0xD0020002ULL, 2.0f, FColor::Yellow,
			TEXT("No trade terminal in range."));
	}
}

void ADWM_DevCharacter::InteractWithDoor()
{
	if (ADwmInteractiveDoor* Door = ActiveDoor.Get())
	{
		Door->ToggleDoor(this);
		return;
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(0xD0020001ULL, 2.0f, FColor::Yellow,
			TEXT("No door in range."));
	}
}
