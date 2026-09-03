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
/** Put every sheep in the level on the grazing loop.

    Staggered, because six sheep chewing in lockstep looks worse than six sheep doing
    nothing -- the same reason the Valley director staggers its chickens. The offset is
    derived from the actor's own position so it is stable across runs rather than
    re-randomised on every load. */
void SetMountainSheepGrazing(UWorld* World)
{
	if (!World)
	{
		return;
	}

	UAnimSequence* Graze = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/FarmAnimalsPack/Sheep/Animations/ANIM_Sheep_IdleGraze.ANIM_Sheep_IdleGraze"));
	if (!Graze)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DWM Sheep] ANIM_Sheep_IdleGraze failed to load; the flock will stay in ")
			TEXT("its reference pose. Is /Game/FarmAnimalsPack/Sheep cooked?"));
		return;
	}

	int32 Grazing = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		USkeletalMeshComponent* Mesh = Actor->FindComponentByClass<USkeletalMeshComponent>();
		const USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
		if (!Asset)
		{
			continue;
		}

		if (!Asset->GetPathName().Contains(TEXT("SK_Sheep"), ESearchCase::IgnoreCase))
		{
			// Every other skeletal actor in the Mountain, so a sheep wearing a mesh this
			// filter does not recognise still shows up in the log.
			UE_LOG(LogTemp, Verbose, TEXT("[DWM Sheep]   not a sheep: '%s' mesh '%s'."),
				*Actor->GetName(), *Asset->GetName());
			continue;
		}

		// Report the state BEFORE changing it. Two sheep were reported set to graze and
		// both still looked dead, so what the clip is competing with matters more than
		// whether the clip was applied.
		UE_LOG(LogTemp, Warning,
			TEXT("[DWM Sheep] '%s' at %s: physics=%s, visible=%s, hidden=%s, ")
			TEXT("animMode=%d, currentAnim='%s'."),
			*Actor->GetName(), *Actor->GetActorLocation().ToCompactString(),
			Mesh->IsSimulatingPhysics() ? TEXT("SIMULATING") : TEXT("off"),
			Mesh->IsVisible() ? TEXT("yes") : TEXT("no"),
			Actor->IsHidden() ? TEXT("yes") : TEXT("no"),
			(int32)Mesh->GetAnimationMode(),
			*GetNameSafe(Mesh->AnimationData.AnimToPlay));

		// A simulating skeletal mesh ignores its animation and settles into a heap,
		// which is exactly what a dead sheep looks like.
		if (Mesh->IsSimulatingPhysics())
		{
			Mesh->SetSimulatePhysics(false);
		}

		Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Mesh->SetAnimation(Graze);
		Mesh->Play(true);

		const FVector Where = Actor->GetActorLocation();
		const float Offset = FMath::Fmod(FMath::Abs(Where.X + Where.Y) / 97.0f, 1.0f);
		Mesh->SetPosition(Offset * Graze->GetPlayLength(), false);

		++Grazing;
	}

	if (Grazing > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[DWM Sheep] %d sheep set to graze."), Grazing);
	}
}

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

	/** Set once a TAGGED actor has claimed this source. Nothing outranks a tag: it is
	    the only identifier here that survives cooking intact. Labels are editor-only
	    and object names keep whatever the actor was created as, which is why DeShawn
	    -- labelled 'DeShawn' but named something else entirely -- was found in PIE and
	    not in the packaged build. */
	bool bTagMatch = false;

	/** Set once an actor labelled EXACTLY with ActorToken has claimed this source, so
	    a later loose name match cannot displace it. */
	bool bExactLabelMatch = false;
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

		// A TAG WINS BEFORE ANY OTHER TEST, including the skeletal-mesh gate below.
		// Tagging an actor says "this is the one"; the component check only guesses at
		// what such an actor looks like, and it is the guess that rejected DeShawn.
		{
			bool bClaimed = false;
			for (FDwmDialogueProxySource& Source : Sources)
			{
				if (Actor->ActorHasTag(FName(Source.ActorLabel)))
				{
					Source.SourceActor = Actor;
					Source.bTagMatch = true;
					bClaimed = true;
					UE_LOG(LogTemp, Log, TEXT("[%s] '%s' is tagged '%s'; using it."),
						LogPrefix, *GetNameSafe(Actor), Source.ActorLabel);
					break;
				}
			}
			if (bClaimed)
			{
				continue;
			}
		}

		// EVERY actor this loop rejects, and why. The Suburbs bootstrap failed to find
		// BP_DeShawn in a cooked build even though that name is in the level, so the
		// reason it was skipped is not something to guess at a second time.
		if (!Actor->FindComponentByClass<USkeletalMeshComponent>())
		{
			const FString RejectName = Actor->GetName();
			for (const FDwmDialogueProxySource& Source : Sources)
			{
				if (RejectName.Contains(Source.ActorToken, ESearchCase::IgnoreCase))
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[%s]   SKIPPED '%s' (class '%s'): it matches '%s' but has no ")
						TEXT("SkeletalMeshComponent."),
						LogPrefix, *RejectName, *GetNameSafe(Actor->GetClass()),
						Source.ActorToken);
				}
			}
			continue;
		}

		UE_LOG(LogTemp, Warning, TEXT("[%s]   candidate '%s' (class '%s')."),
			LogPrefix, *Actor->GetName(), *GetNameSafe(Actor->GetClass()));

		const FString Identity = GetDwmBootstrapIdentity(Actor);

		// An EXACT label match beats an incidental one buried in the object name.
		//
		// GetName() keeps the name the actor was created with, so a Blueprint class
		// BP_Kai2 still answers to "Kai" long after its label has been changed to
		// something else. Relabelling the old actor therefore does NOT retire it, and
		// since iteration order is arbitrary the stale one can win over a purpose-placed
		// replacement. Deliberately labelling an actor is the clearest statement of
		// intent available, so it takes precedence.
#if WITH_EDITOR
		const FString Label = Actor->GetActorLabel();
#else
		const FString Label = Actor->GetName();
#endif

		for (FDwmDialogueProxySource& Source : Sources)
		{
			if (Source.bTagMatch)
			{
				continue;
			}

			if (Label.Equals(Source.ActorToken, ESearchCase::IgnoreCase))
			{
				if (Source.SourceActor && Source.SourceActor != Actor)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[%s] '%s' is labelled '%s' exactly; using it instead of '%s'."),
						LogPrefix, *GetNameSafe(Actor), Source.ActorToken,
						*GetNameSafe(Source.SourceActor));
				}
				Source.SourceActor = Actor;
				Source.bExactLabelMatch = true;
				break;
			}

			if (!Source.SourceActor && !Source.bExactLabelMatch && !Source.bTagMatch
				&& Identity.Contains(Source.ActorToken, ESearchCase::IgnoreCase))
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
			// The proxy is invisible; the placed Blueprint is what the player sees.
			// Without this link, seating the proxy seats nobody -- which is why Mike
			// stayed standing in the City while the log said he was seated.
			Source.ExistingNpc->SetVisualSourceActor(Source.SourceActor);
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

				// BEFORE FinishSpawning, which is what runs BeginPlay. The seated setup
				// happens there, so a link established afterwards arrives too late and the
				// proxy seats itself instead of the Blueprint the player can see.
				NewNpc->SetVisualSourceActor(Source.SourceActor);
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
				TEXT("[%s] Could not find a placed visual actor containing '%s' for %s. ")
				TEXT("Tag the placed actor '%s' in the editor (Actor -> Tags); its label ")
				TEXT("is not visible in a cooked build."),
				LogPrefix,
				Source.ActorToken,
				Source.ActorLabel,
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

	// The flock reads as a field of dead sheep without this.
	//
	// They are plain SkeletalMeshActors placed with SK_Sheep and no animation asset at
	// all -- nothing in the level references any ANIM_Sheep clip -- so they render in
	// the skeleton's reference pose, which on a quadruped is a splayed, collapsed shape.
	// Grazing is what they are meant to be doing, in the opening scene and again at the
	// end, so it is set here rather than left to be re-applied by hand on each sheep.
	SetMountainSheepGrazing(World);

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

