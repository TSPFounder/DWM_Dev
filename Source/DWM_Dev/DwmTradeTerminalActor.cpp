#include "DwmTradeTerminalActor.h"

#include "DWM_DevCharacter.h"
#include "DWM_DevPlayerController.h"
#include "DwmGameInstance.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr uint64 TradePromptMessageKey = 0xD0018AULL;
}

ADwmTradeTerminalActor::ADwmTradeTerminalActor()
{
    PrimaryActorTick.bCanEverTick = false;

    InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
    SetRootComponent(InteractionSphere);
    InteractionSphere->InitSphereRadius(175.0f);
    InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    InteractionSphere->SetCollisionObjectType(ECC_WorldDynamic);
    InteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
    InteractionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    InteractionSphere->SetGenerateOverlapEvents(true);
    InteractionSphere->SetRelativeLocation(FVector(0.0f, 0.0f, 70.0f));

    TerminalMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TerminalMesh"));
    TerminalMesh->SetupAttachment(InteractionSphere);
    TerminalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TerminalMesh->SetRelativeLocation(FVector::ZeroVector);
    TerminalMesh->SetRelativeScale3D(FVector(0.65f, 0.65f, 1.4f));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (CubeAsset.Succeeded())
    {
        TerminalMesh->SetStaticMesh(CubeAsset.Object);
    }

    TerminalLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("TerminalLabel"));
    TerminalLabel->SetupAttachment(InteractionSphere);
    TerminalLabel->SetRelativeLocation(FVector(0.0f, 0.0f, 145.0f));
    TerminalLabel->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
    TerminalLabel->SetHorizontalAlignment(EHTA_Center);
    TerminalLabel->SetWorldSize(28.0f);
    TerminalLabel->SetTextRenderColor(FColor(255, 210, 60));
    // NOTE: the label's actual text is set in BeginPlay, not here -- Day 20 fields
    // (BuyerCommunityId/SellerCommunityId/ResourceId/etc.) are UPROPERTY EditAnywhere values
    // that get applied to this instance AFTER the constructor runs (during actor spawn/load),
    // so reading them here in the constructor would still see the class defaults, not
    // whatever a placed instance was actually configured to. Same reasoning applies to the
    // on-screen overlap prompt below.
}

void ADwmTradeTerminalActor::BeginPlay()
{
    Super::BeginPlay();
    InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &ADwmTradeTerminalActor::OnInteractionSphereBeginOverlap);
    InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &ADwmTradeTerminalActor::OnInteractionSphereEndOverlap);

    TerminalLabel->SetText(FText::FromString(FString::Printf(TEXT("%s\nPress T"), *BuildTradeDescription())));
}

FString ADwmTradeTerminalActor::BuildTradeDescription() const
{
    if (!TradeDescription.IsEmpty())
    {
        return TradeDescription;
    }
    // Auto-generated fallback when no flavor text is configured for this instance.
    return FString::Printf(TEXT("%s buys %.0f %s from %s for %.0f St"),
        *BuyerCommunityId, Quantity, *ResourceId, *SellerCommunityId, Amount);
}

void ADwmTradeTerminalActor::SetVisualsHidden(bool bInHidden)
{
    if (TerminalMesh)
    {
        TerminalMesh->SetHiddenInGame(bInHidden);
    }
    if (TerminalLabel)
    {
        TerminalLabel->SetHiddenInGame(bInHidden);
    }
}

void ADwmTradeTerminalActor::OnInteractionSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
    bool bFromSweep, const FHitResult& SweepResult)
{
	if (ADWM_DevCharacter* Character = Cast<ADWM_DevCharacter>(OtherActor))
    {
        Character->SetActiveTradeTerminal(this);
		// The controller-level E binding can consume the key before the character's
		// binding. Keep the controller reference synchronized with the character so
		// either input path executes the same nearby terminal.
		if (ADWM_DevPlayerController* Controller =
			Cast<ADWM_DevPlayerController>(Character->GetController()))
		{
			Controller->SetActiveTradeTerminal(this);
		}
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(TradePromptMessageKey, -1.0f, FColor::Yellow,
                FString::Printf(TEXT("Press T: %s"), *BuildTradeDescription()));
		}
	}
	else if (APawn* Pawn = Cast<APawn>(OtherActor))
	{
		if (ADWM_DevPlayerController* Controller = Cast<ADWM_DevPlayerController>(Pawn->GetController()))
		{
			Controller->SetActiveTradeTerminal(this);
			if (GEngine && Pawn->IsLocallyControlled())
			{
				GEngine->AddOnScreenDebugMessage(TradePromptMessageKey, -1.0f, FColor::Yellow,
					FString::Printf(TEXT("Press T: %s"), *BuildTradeDescription()));
			}
		}
	}
}

void ADwmTradeTerminalActor::OnInteractionSphereEndOverlap(UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (ADWM_DevCharacter* Character = Cast<ADWM_DevCharacter>(OtherActor))
    {
        Character->ClearActiveTradeTerminal(this);
		if (ADWM_DevPlayerController* Controller =
			Cast<ADWM_DevPlayerController>(Character->GetController()))
		{
			Controller->ClearActiveTradeTerminal(this);
		}
        if (GEngine)
        {
            GEngine->RemoveOnScreenDebugMessage(TradePromptMessageKey);
		}
	}
	else if (APawn* Pawn = Cast<APawn>(OtherActor))
	{
		if (ADWM_DevPlayerController* Controller = Cast<ADWM_DevPlayerController>(Pawn->GetController()))
		{
			Controller->ClearActiveTradeTerminal(this);
			if (GEngine && Pawn->IsLocallyControlled())
			{
				GEngine->RemoveOnScreenDebugMessage(TradePromptMessageKey);
			}
		}
	}
}

void ADwmTradeTerminalActor::ExecuteTrade(APawn* InteractingPawn)
{
    if (UDwmGameInstance* GameInstance = GetGameInstance<UDwmGameInstance>())
    {
        GameInstance->ExecuteConfiguredTrade(BuyerCommunityId, SellerCommunityId, ResourceId, Amount, Quantity,
            BuildTradeDescription());
    }
}
