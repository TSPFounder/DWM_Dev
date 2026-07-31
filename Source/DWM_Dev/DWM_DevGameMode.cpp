// Copyright Epic Games, Inc. All Rights Reserved.

#include "DWM_DevGameMode.h"
#include "DWM_DevCharacter.h"
#include "DwmEconomyHud.h"
#include "DwmNpcActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

ADWM_DevGameMode::ADWM_DevGameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;
	HUDClass = ADwmEconomyHud::StaticClass();

}

void ADWM_DevGameMode::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World || !World->GetMapName().Contains(TEXT("DWM_Mountain"), ESearchCase::IgnoreCase))
	{
		return;
	}

	ADwmNpcActor* HankNpc = nullptr;
	AActor* LegacyHankActor = nullptr;
	AActor* TurbineActor = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		if (ADwmNpcActor* ExistingNpc = Cast<ADwmNpcActor>(Actor))
		{
			HankNpc = ExistingNpc;
			continue;
		}

		FString ActorIdentity = Actor->GetName();
#if WITH_EDITOR
		ActorIdentity += TEXT(" ") + Actor->GetActorLabel();
#endif
		if (!TurbineActor && ActorIdentity.Contains(TEXT("Turbine"), ESearchCase::IgnoreCase))
		{
			TurbineActor = Actor;
		}

		if (!LegacyHankActor)
		{
			if (USkeletalMeshComponent* MeshComponent = Actor->FindComponentByClass<USkeletalMeshComponent>())
			{
				if (const USkeletalMesh* MeshAsset = MeshComponent->GetSkeletalMeshAsset())
				{
					const FString MeshPath = MeshAsset->GetPathName();
					if (MeshPath.Contains(TEXT("/Hank/"), ESearchCase::IgnoreCase)
						|| MeshPath.Contains(TEXT("SK_Hank_02"), ESearchCase::IgnoreCase))
					{
						LegacyHankActor = Actor;
					}
				}
			}
		}
	}

	if (!HankNpc && LegacyHankActor)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = TEXT("DWM_Hank");
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		HankNpc = World->SpawnActor<ADwmNpcActor>(
			ADwmNpcActor::StaticClass(), LegacyHankActor->GetActorTransform(), SpawnParameters);

		if (HankNpc)
		{
#if WITH_EDITOR
			HankNpc->SetActorLabel(TEXT("DWM_Hank"));
#endif
			LegacyHankActor->SetActorHiddenInGame(true);
			LegacyHankActor->SetActorEnableCollision(false);
			UE_LOG(LogTemp, Log,
				TEXT("[DWM Hank] Replaced legacy Hank set-dressing '%s' with animated/dialogue Hank."),
				*GetNameSafe(LegacyHankActor));
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[DWM Hank] Failed to spawn the animated/dialogue Hank actor."));
		}
	}
	else if (!HankNpc && !LegacyHankActor)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DWM Hank] No legacy SK_Hank_02 actor was found in DWM_Mountain."));
	}

	if (HankNpc)
	{
		HankNpc->TurbineActor = TurbineActor;
		if (TurbineActor)
		{
			FVector TowardTurbine = TurbineActor->GetActorLocation() - HankNpc->GetActorLocation();
			TowardTurbine.Z = 0.0f;
			if (!TowardTurbine.IsNearlyZero())
			{
				HankNpc->SetActorRotation(TowardTurbine.Rotation());
			}
		}
	}
}
