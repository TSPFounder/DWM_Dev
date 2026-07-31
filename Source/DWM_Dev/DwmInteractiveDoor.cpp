#include "DwmInteractiveDoor.h"

#include "DWM_DevCharacter.h"
#include "DWM_DevPlayerController.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr uint64 DoorPromptMessageKeyBase = 0xD0020000ULL;
}

ADwmInteractiveDoor::ADwmInteractiveDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	DoorName = FText::FromString(TEXT("Door"));

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(SceneRoot);
	Hinge->SetMobility(EComponentMobility::Movable);

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(Hinge);
	DoorMesh->SetMobility(EComponentMobility::Movable);
	DoorMesh->SetCollisionProfileName(TEXT("BlockAll"));

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(SceneRoot);
	InteractionSphere->InitSphereRadius(InteractionRadius);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractionSphere->SetGenerateOverlapEvents(true);
	InteractionSphere->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
}

void ADwmInteractiveDoor::BeginPlay()
{
	Super::BeginPlay();

	InteractionSphere->SetSphereRadius(InteractionRadius);
	ClosedHingeRotation = Hinge->GetRelativeRotation();
	TargetHingeRotation = ClosedHingeRotation;

	// Some TownShops doors are assembled from a door panel plus separate glass, decals, and
	// hardware. Keeping their existing world transforms avoids hand-aligning each piece; only
	// their parent changes, so they follow the same hinge while the door opens.
	TSet<AActor*> VisualActorsToAttach;
	for (AActor* VisualActor : AttachedVisualActors)
	{
		VisualActorsToAttach.Add(VisualActor);
	}

	if (!VisualAttachmentTag.IsNone())
	{
		TArray<AActor*> TaggedActors;
		UGameplayStatics::GetAllActorsWithTag(this, VisualAttachmentTag, TaggedActors);
		VisualActorsToAttach.Append(TaggedActors);
	}

	for (AActor* VisualActor : VisualActorsToAttach)
	{
		if (IsValid(VisualActor) && VisualActor != this)
		{
			// TownShops ships these separate glass/decal actors as Static. Unreal refuses
			// to attach a static component to our movable hinge, so promote the visual's
			// root before attaching it. This is runtime-only and leaves the source asset
			// unchanged in the editor.
			if (USceneComponent* VisualRoot = VisualActor->GetRootComponent())
			{
				VisualRoot->SetMobility(EComponentMobility::Movable);
			}
			VisualActor->AttachToComponent(Hinge, FAttachmentTransformRules::KeepWorldTransform);
		}
	}

	InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &ADwmInteractiveDoor::OnInteractionSphereBeginOverlap);
	InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &ADwmInteractiveDoor::OnInteractionSphereEndOverlap);
}

void ADwmInteractiveDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const FRotator NewRotation = FMath::RInterpConstantTo(
		Hinge->GetRelativeRotation(), TargetHingeRotation, DeltaSeconds, OpenSpeedDegreesPerSecond);
	Hinge->SetRelativeRotation(NewRotation);

	if (NewRotation.Equals(TargetHingeRotation, 0.05f))
	{
		Hinge->SetRelativeRotation(TargetHingeRotation);
		SetActorTickEnabled(false);
	}
}

void ADwmInteractiveDoor::ToggleDoor(APawn* InteractingPawn)
{
	if (bLocked)
	{
		if (GEngine && InteractingPawn && InteractingPawn->IsLocallyControlled())
		{
			GEngine->AddOnScreenDebugMessage(GetPromptMessageKey(), 2.0f, FColor::Red,
				FString::Printf(TEXT("%s is locked."), *DoorName.ToString()));
		}
		return;
	}

	bIsOpen = !bIsOpen;
	TargetHingeRotation = ClosedHingeRotation;
	if (bIsOpen)
	{
		TargetHingeRotation.Yaw += OpenYawDegrees;
	}

	SetActorTickEnabled(true);

	if (GEngine && InteractingPawn && InteractingPawn->IsLocallyControlled())
	{
		GEngine->AddOnScreenDebugMessage(GetPromptMessageKey(), -1.0f, FColor::Cyan, BuildPrompt());
	}
}

void ADwmInteractiveDoor::OnInteractionSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (ADWM_DevCharacter* Character = Cast<ADWM_DevCharacter>(OtherActor))
	{
		Character->SetActiveDoor(this);
		// F is intentionally also bound on DWM_DevPlayerController so alternate
		// pawns can use doors. The controller input component has priority and can
		// consume F before the character receives it, so keep both references in
		// sync for the normal first-person character as well.
		if (ADWM_DevPlayerController* Controller =
			Cast<ADWM_DevPlayerController>(Character->GetController()))
		{
			Controller->SetActiveDoor(this);
		}
		if (GEngine && Character->IsLocallyControlled())
		{
			GEngine->AddOnScreenDebugMessage(GetPromptMessageKey(), -1.0f, FColor::Cyan, BuildPrompt());
		}
	}
	else if (APawn* Pawn = Cast<APawn>(OtherActor))
	{
		if (ADWM_DevPlayerController* Controller = Cast<ADWM_DevPlayerController>(Pawn->GetController()))
		{
			Controller->SetActiveDoor(this);
			if (GEngine && Pawn->IsLocallyControlled())
			{
				GEngine->AddOnScreenDebugMessage(GetPromptMessageKey(), -1.0f, FColor::Cyan, BuildPrompt());
			}
		}
		else if (Pawn->IsLocallyControlled())
		{
			// The CharacterCustomizer pack normally uses the stock PlayerController.  Give the
			// door a small, scoped F binding in that case so interaction is not tied to DWM's
			// controller subclass. The binding is removed again on overlap end.
			if (APlayerController* FallbackController = Cast<APlayerController>(Pawn->GetController()))
			{
				FallbackInteractingPawn = Pawn;
				FallbackInputController = FallbackController;
				EnableInput(FallbackController);
				if (InputComponent && !bFallbackInputBound)
				{
					InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ADwmInteractiveDoor::ToggleDoorFromFallbackInput);
					bFallbackInputBound = true;
				}

				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(GetPromptMessageKey(), -1.0f, FColor::Cyan, BuildPrompt());
				}
			}
		}
	}
}

void ADwmInteractiveDoor::OnInteractionSphereEndOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (ADWM_DevCharacter* Character = Cast<ADWM_DevCharacter>(OtherActor))
	{
		Character->ClearActiveDoor(this);
		if (ADWM_DevPlayerController* Controller =
			Cast<ADWM_DevPlayerController>(Character->GetController()))
		{
			Controller->ClearActiveDoor(this);
		}
		if (GEngine && Character->IsLocallyControlled())
		{
			GEngine->RemoveOnScreenDebugMessage(GetPromptMessageKey());
		}
	}
	else if (APawn* Pawn = Cast<APawn>(OtherActor))
	{
		if (ADWM_DevPlayerController* Controller = Cast<ADWM_DevPlayerController>(Pawn->GetController()))
		{
			Controller->ClearActiveDoor(this);
			if (GEngine && Pawn->IsLocallyControlled())
			{
				GEngine->RemoveOnScreenDebugMessage(GetPromptMessageKey());
			}
		}
		else if (FallbackInteractingPawn.Get() == Pawn)
		{
			if (APlayerController* FallbackController = FallbackInputController.Get())
			{
				DisableInput(FallbackController);
			}
			FallbackInteractingPawn.Reset();
			FallbackInputController.Reset();
			bFallbackInputBound = false;
			if (GEngine && Pawn->IsLocallyControlled())
			{
				GEngine->RemoveOnScreenDebugMessage(GetPromptMessageKey());
			}
		}
	}
}

void ADwmInteractiveDoor::ToggleDoorFromFallbackInput()
{
	if (APawn* Pawn = FallbackInteractingPawn.Get())
	{
		ToggleDoor(Pawn);
	}
}

FString ADwmInteractiveDoor::BuildPrompt() const
{
	if (bLocked)
	{
		return FString::Printf(TEXT("%s is locked."), *DoorName.ToString());
	}

	return FString::Printf(TEXT("Press F to %s %s"), bIsOpen ? TEXT("close") : TEXT("open"), *DoorName.ToString());
}

uint64 ADwmInteractiveDoor::GetPromptMessageKey() const
{
	return DoorPromptMessageKeyBase + static_cast<uint64>(GetUniqueID());
}
