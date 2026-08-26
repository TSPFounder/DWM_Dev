// Copyright Epic Games, Inc. All Rights Reserved.

#include "DWM_DevGameMode.h"
#include "DWM_DevCharacter.h"
#include "DwmEconomyHud.h"
#include "DwmNpcActor.h"
#include "DwmValleyLifeDirector.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
FString GetDwmBootstrapIdentity(const AActor* Actor)
{
	FString Identity = Actor ? Actor->GetName() : FString();
#if WITH_EDITOR
	if (Actor)
	{
		Identity += TEXT(" ");
		Identity += Actor->GetActorLabel();
	}
#endif
	return Identity;
}

struct FDwmDialogueProxySource
{
	EDwmNpcProfile Profile;
	const TCHAR* ActorToken;
	const TCHAR* ActorLabel;
	AActor* SourceActor = nullptr;
	ADwmNpcActor* ExistingNpc = nullptr;
};
const TCHAR* const* GetDwmCrowdIdleAnimationPaths(int32& OutCount)
{
	static const TCHAR* CandidatePaths[] =
	{
		TEXT("/Game/CitySampleCrowd/Character/Anims/Loco/MTN_Set/MTN_N_Idle.MTN_N_Idle"),
		TEXT("/Game/CitySampleCrowd/Character/Anims/Loco/MTN_Set/MTN_N_Idle_B.MTN_N_Idle_B"),
		TEXT("/Game/CitySampleCrowd/Character/Anims/Loco/MTN_Set/MTN_N_Idle_C.MTN_N_Idle_C"),
		TEXT("/Game/CitySampleCrowd/Character/Anims/Loco/MTO_Set/MTO_N_Idle.MTO_N_Idle"),
		TEXT("/Game/CitySampleCrowd/Character/Anims/Loco/MTU_Set/MTU_N_Idle.MTU_N_Idle"),
		TEXT("/Game/CitySampleCrowd/Character/Anims/Loco/FTN_Set/FTN_N_Idle_Base.FTN_N_Idle_Base"),
		TEXT("/Game/CitySampleCrowd/Character/Anims/Loco/FTO_Set/FTO_N_Idle_Base.FTO_N_Idle_Base"),
		TEXT("/Game/CitySampleCrowd/Character/Anims/Loco/FTU_Set/FTU_N_Idle_Base.FTU_N_Idle_Base"),
		TEXT("/Game/CharacterCustomizer/Components/SLS/Animations/Idle/Idle.Idle"),
		TEXT("/Game/CharacterCustomizer/Components/SLS/Animations/Idle/Idle_01.Idle_01"),
		TEXT("/Game/CharacterCustomizer/Components/SLS/Poses/Pose_Idle_01_Loop.Pose_Idle_01_Loop"),
		TEXT("/Game/CharacterCustomizer/Characters/ApparelPack_Example_Character/Idle.Idle")
	};

	OutCount = UE_ARRAY_COUNT(CandidatePaths);
	return CandidatePaths;
}
bool IsDwmAnimationCompatible(const UAnimSequence* Animation, const USkeletalMeshComponent* MeshComponent)
{
	const USkeletalMesh* Mesh = MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
	return Animation
		&& Mesh
		&& Animation->GetSkeleton()
		&& Mesh->GetSkeleton()
		&& Animation->GetSkeleton() == Mesh->GetSkeleton();
}

UAnimSequence* LoadDwmCompatibleCrowdIdleAnimation(const USkeletalMeshComponent* MeshComponent)
{
	int32 CandidateCount = 0;
	const TCHAR* const* CandidatePaths = GetDwmCrowdIdleAnimationPaths(CandidateCount);
	for (int32 Index = 0; Index < CandidateCount; ++Index)
	{
		if (UAnimSequence* Animation = LoadObject<UAnimSequence>(nullptr, CandidatePaths[Index]))
		{
			if (IsDwmAnimationCompatible(Animation, MeshComponent))
			{
				return Animation;
			}
		}
	}

	return nullptr;
}
void ApplyBasicIdleAnimationToSourceActor(AActor* SourceActor, const TCHAR* LogPrefix)
{
	if (!SourceActor)
	{
		return;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	SourceActor->GetComponents<USkeletalMeshComponent>(MeshComponents);

	bool bApplied = false;
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!MeshComponent || !MeshComponent->GetSkeletalMeshAsset())
		{
			continue;
		}

		// FOLLOWERS MUST NOT BE ANIMATED. A CharacterCustomizer character is a LEADER
		// mesh (the body) plus follower components -- hair, clothing, accessories --
		// that copy the leader's pose each frame instead of evaluating their own
		// animation. Starting an independent idle on a follower leaves two things
		// writing the same bones every frame: the pose copy and the follower's own
		// animation instance. That fight is the rapid vibration reported in issue #19,
		// worst on whichever bones carry the most followers (gloves and sleeves, hence
		// "the hands"). Removing the Post Process Anim Blueprint did not help because
		// the Post Process layer was never the second writer -- this loop was.
		//
		// DwmGameInstance.cpp already reads LeaderPoseComponent when it captures these
		// characters and re-establishes it with SetLeaderPoseComponent on the clone;
		// this is the same relationship, respected rather than rebuilt.
		if (MeshComponent->LeaderPoseComponent.IsValid())
		{
			continue;
		}

		UAnimSequence* IdleAnimation = LoadDwmCompatibleCrowdIdleAnimation(MeshComponent);
		if (!IdleAnimation)
		{
			continue;
		}

		MeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		MeshComponent->SetAnimation(IdleAnimation);
		MeshComponent->Play(true);
		bApplied = true;
	}

	if (!bApplied)
	{
		// Two different failures share this branch, so name which one happened:
		// every mesh being a follower means the leader was not found (a rigging
		// problem), while a leader with no match is an animation-compatibility
		// problem. They need different fixes.
		int32 LeaderCount = 0;
		for (const USkeletalMeshComponent* MeshComponent : MeshComponents)
		{
			if (MeshComponent && MeshComponent->GetSkeletalMeshAsset()
				&& !MeshComponent->LeaderPoseComponent.IsValid())
			{
				++LeaderCount;
			}
		}

		if (LeaderCount == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[%s] Found placed visual '%s', but every skeletal mesh on it is a follower (no leader pose component). Nothing was animated."), LogPrefix, *GetNameSafe(SourceActor));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[%s] Found placed visual '%s', but no compatible City Sample or CharacterCustomizer idle animation matched its skeletal mesh."), LogPrefix, *GetNameSafe(SourceActor));
		}
	}
}
template <int32 NumSources>
void BootstrapDialogueProxyNpcs(
	UWorld* World,
	FDwmDialogueProxySource (&Sources)[NumSources],
	const TCHAR* LogPrefix)
{
	if (!World)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		if (ADwmNpcActor* ExistingNpc = Cast<ADwmNpcActor>(Actor))
		{
			for (FDwmDialogueProxySource& Source : Sources)
			{
				if (ExistingNpc->GetNpcProfile() == Source.Profile)
				{
					Source.ExistingNpc = ExistingNpc;
				}
			}
			continue;
		}

		if (!Actor->FindComponentByClass<USkeletalMeshComponent>())
		{
			continue;
		}

		const FString Identity = GetDwmBootstrapIdentity(Actor);
		for (FDwmDialogueProxySource& Source : Sources)
		{
			if (!Source.SourceActor && Identity.Contains(Source.ActorToken, ESearchCase::IgnoreCase))
			{
				Source.SourceActor = Actor;
				break;
			}
		}
	}

	for (FDwmDialogueProxySource& Source : Sources)
	{
		if (Source.SourceActor
			&& (Source.Profile == EDwmNpcProfile::Kai || Source.Profile == EDwmNpcProfile::DeShawn))
		{
			ApplyBasicIdleAnimationToSourceActor(Source.SourceActor, LogPrefix);
		}

		if (Source.SourceActor && Source.ExistingNpc)
		{
			Source.ExistingNpc->SetActorTransform(Source.SourceActor->GetActorTransform());
			Source.ExistingNpc->SetActorEnableCollision(true);
			continue;
		}

		if (Source.SourceActor && !Source.ExistingNpc)
		{
			const FTransform SourceTransform = Source.SourceActor->GetActorTransform();

			ADwmNpcActor* NewNpc = World->SpawnActorDeferred<ADwmNpcActor>(
				ADwmNpcActor::StaticClass(),
				SourceTransform,
				nullptr,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (NewNpc)
			{
				NewNpc->ConfigureDialogueProxy(Source.Profile);
				NewNpc->FinishSpawning(SourceTransform);
#if WITH_EDITOR
				NewNpc->SetActorLabel(Source.ActorLabel);
#endif
				UE_LOG(LogTemp, Log,
					TEXT("[%s] Created dialogue proxy %s for placed visual '%s'."),
					LogPrefix,
					Source.ActorLabel,
					*GetNameSafe(Source.SourceActor));
			}
		}

		if (!Source.SourceActor)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[%s] Could not find a placed visual actor containing '%s' for %s."),
				LogPrefix,
				Source.ActorToken,
				Source.ActorLabel);
		}
	}
}
}

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
	if (!World)
	{
		return;
	}

	const FString MapName = World->GetMapName();
	if (MapName.Contains(TEXT("DWM_Valley"), ESearchCase::IgnoreCase))
	{
		ADwmValleyLifeDirector* ExistingDirector = nullptr;
		for (TActorIterator<ADwmValleyLifeDirector> It(World); It; ++It)
		{
			ExistingDirector = *It;
			break;
		}

		if (!ExistingDirector)
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = TEXT("DWM_ValleyLifeDirector");
			SpawnParameters.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			World->SpawnActor<ADwmValleyLifeDirector>(
				ADwmValleyLifeDirector::StaticClass(), FTransform::Identity, SpawnParameters);
		}
		return;
	}

	if (MapName.Contains(TEXT("DWM_Hillside"), ESearchCase::IgnoreCase))
	{
		struct FHillsideNpcSource
		{
			EDwmNpcProfile Profile;
			const TCHAR* MeshToken;
			const TCHAR* ActorLabel;
			AActor* SourceActor = nullptr;
			USkeletalMeshComponent* SourceMesh = nullptr;
			ADwmNpcActor* ExistingNpc = nullptr;
		};

		// The three scanned people already placed by the level designer are retained as the
		// visual sources for Sophia, Owen, and Nathan.
		FHillsideNpcSource Sources[] =
		{
			{ EDwmNpcProfile::Sophia, TEXT("rp_sophia_rigged_003_ue4"), TEXT("DWM_Sophia") },
			{ EDwmNpcProfile::Owen, TEXT("rp_manuel_rigged_001_ue4"), TEXT("DWM_Owen") },
			{ EDwmNpcProfile::Nathan, TEXT("rp_nathan_rigged_003_ue4"), TEXT("DWM_Nathan") }
		};

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			if (ADwmNpcActor* ExistingNpc = Cast<ADwmNpcActor>(Actor))
			{
				for (FHillsideNpcSource& Source : Sources)
				{
					if (ExistingNpc->GetNpcProfile() == Source.Profile)
					{
						Source.ExistingNpc = ExistingNpc;
					}
				}
				continue;
			}

			USkeletalMeshComponent* MeshComponent = Actor->FindComponentByClass<USkeletalMeshComponent>();
			const USkeletalMesh* MeshAsset = MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
			if (!MeshAsset)
			{
				continue;
			}

			const FString MeshPath = MeshAsset->GetPathName();
			for (FHillsideNpcSource& Source : Sources)
			{
				if (!Source.SourceActor && MeshPath.Contains(Source.MeshToken, ESearchCase::IgnoreCase))
				{
					Source.SourceActor = Actor;
					Source.SourceMesh = MeshComponent;
					break;
				}
			}
		}

		for (FHillsideNpcSource& Source : Sources)
		{
			if (!Source.ExistingNpc && Source.SourceActor && Source.SourceMesh)
			{
				const FTransform SourceTransform = Source.SourceMesh->GetComponentTransform();
				ADwmNpcActor* NewNpc = World->SpawnActorDeferred<ADwmNpcActor>(
					ADwmNpcActor::StaticClass(), SourceTransform, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (NewNpc)
				{
					NewNpc->ConfigureProfileFromSource(Source.Profile, Source.SourceMesh);
					NewNpc->FinishSpawning(SourceTransform);
#if WITH_EDITOR
					NewNpc->SetActorLabel(Source.ActorLabel);
#endif
					Source.ExistingNpc = NewNpc;
					UE_LOG(LogTemp, Log,
						TEXT("[DWM Hillside] Created %s from placed visual '%s'."),
						Source.ActorLabel, *GetNameSafe(Source.SourceActor));
				}
			}

			if (Source.ExistingNpc && Source.SourceActor)
			{
				Source.SourceActor->SetActorHiddenInGame(true);
				Source.SourceActor->SetActorEnableCollision(false);
			}

			if (!Source.ExistingNpc)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[DWM Hillside] Could not find the placed mesh for %s (%s)."),
					Source.ActorLabel, Source.MeshToken);
			}
		}
		return;
	}

	if (MapName.Contains(TEXT("DWM_City"), ESearchCase::IgnoreCase))
	{
		FDwmDialogueProxySource Sources[] =
		{
			{ EDwmNpcProfile::Mike, TEXT("Mike"), TEXT("DWM_Mike") },
			{ EDwmNpcProfile::Kai, TEXT("Kai"), TEXT("DWM_Kai") }
		};
		BootstrapDialogueProxyNpcs(World, Sources, TEXT("DWM City"));
		return;
	}

	if (MapName.Contains(TEXT("DWM_Suburbs"), ESearchCase::IgnoreCase)
		|| MapName.Contains(TEXT("DWM_Suburb"), ESearchCase::IgnoreCase))
	{
		FDwmDialogueProxySource Sources[] =
		{
			{ EDwmNpcProfile::DeShawn, TEXT("DeShawn"), TEXT("DWM_DeShawn") }
		};
		BootstrapDialogueProxyNpcs(World, Sources, TEXT("DWM Suburbs"));
		return;
	}

	if (!MapName.Contains(TEXT("DWM_Mountain"), ESearchCase::IgnoreCase))
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

