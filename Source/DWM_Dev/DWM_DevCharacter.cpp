// Copyright Epic Games, Inc. All Rights Reserved.

#include "DWM_DevCharacter.h"
#include "DWM_DevProjectile.h"
#include "DwmTradeTerminalActor.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
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
	PlayerInputComponent->BindKey(EKeys::E, IE_Pressed, this, &ADWM_DevCharacter::Interact);

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

void ADWM_DevCharacter::Interact()
{
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
			TEXT("No trade terminal in range."));
	}
}
