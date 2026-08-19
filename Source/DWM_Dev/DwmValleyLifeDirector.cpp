// DwmValleyLifeDirector.cpp

#include "DwmValleyLifeDirector.h"

#include "DwmNpcActor.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
FString GetActorIdentity(const AActor* Actor)
{
    if (!Actor)
    {
        return FString();
    }

    FString Identity = Actor->GetName() + TEXT(" ") + Actor->GetClass()->GetName();
#if WITH_EDITOR
    Identity += TEXT(" ") + Actor->GetActorLabel();
#endif
    return Identity;
}

bool LooksLikeWeapon(const FString& Identity)
{
    static const TCHAR* WeaponTokens[] =
    {
        TEXT("weapon"), TEXT("gun"), TEXT("rifle"), TEXT("pistol"),
        TEXT("firearm"), TEXT("shotgun"), TEXT("revolver")
    };

    for (const TCHAR* Token : WeaponTokens)
    {
        if (Identity.Contains(Token, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

FString GetComponentIdentity(const UActorComponent* Component)
{
    if (!Component)
    {
        return FString();
    }

    FString Identity = Component->GetName() + TEXT(" ") + Component->GetClass()->GetName();
    if (const USkeletalMeshComponent* SkeletalMesh = Cast<USkeletalMeshComponent>(Component))
    {
        if (const USkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset())
        {
            Identity += TEXT(" ") + Asset->GetPathName();
        }
    }
    else if (const UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Component))
    {
        if (const UStaticMesh* Asset = StaticMesh->GetStaticMesh())
        {
            Identity += TEXT(" ") + Asset->GetPathName();
        }
    }
    return Identity;
}

FString GetActorAndComponentIdentity(const AActor* Actor)
{
    FString Identity = GetActorIdentity(Actor);
    if (!Actor)
    {
        return Identity;
    }

    TArray<UActorComponent*> Components;
    Actor->GetComponents(Components);
    for (const UActorComponent* Component : Components)
    {
        Identity += TEXT(" ") + GetComponentIdentity(Component);
    }
    return Identity;
}
}

ADwmValleyLifeDirector::ADwmValleyLifeDirector()
{
    PrimaryActorTick.bCanEverTick = true;
    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);

    static ConstructorHelpers::FObjectFinder<UAnimSequence> MariaWalkFinder(
        TEXT("/Game/YI_NPC/Animation/Mocap_Mobility/IPC/"
             "MOB1_Walk_F_Loop_IPC.MOB1_Walk_F_Loop_IPC"));
    MariaWalkAnimation = MariaWalkFinder.Object;

    static ConstructorHelpers::FObjectFinder<UAnimSequence> MariaIdleFinder(
        TEXT("/Game/YI_NPC/Animation/Mocap_Mobility/IPC/"
             "MOB1_Stand_Relaxed_Idle_v2_IPC.MOB1_Stand_Relaxed_Idle_v2_IPC"));
    MariaRelaxedIdleAnimation = MariaIdleFinder.Object;

    // The office-desk pack clips use the standard UE4 mannequin hierarchy. Maria's mesh
    // uses the same hierarchy; CanPlayOnMaria performs the engine's compatibility check
    // before either clip is assigned and falls back to her native relaxed idle if an
    // asset update ever makes them incompatible.
    static ConstructorHelpers::FObjectFinder<UAnimSequence> StandToSitFinder(
        TEXT("/Game/Office_Desk/Animation/Root_Motion/"
             "Office_Desk_Stand_To_Sit.Office_Desk_Stand_To_Sit"));
    MariaStandToSitAnimation = StandToSitFinder.Object;

    static ConstructorHelpers::FObjectFinder<UAnimSequence> SitIdleFinder(
        TEXT("/Game/Office_Desk/Animation/Root_Motion/"
             "Office_Desk_Sit_Idle.Office_Desk_Sit_Idle"));
    MariaSitIdleAnimation = SitIdleFinder.Object;

    static ConstructorHelpers::FObjectFinder<UAnimSequence> ChickenIdle1Finder(
        TEXT("/Game/FarmAnimalsPack/Chicken/Animations/"
             "ANIM_Chicken_Idle1.ANIM_Chicken_Idle1"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> ChickenIdle2Finder(
        TEXT("/Game/FarmAnimalsPack/Chicken/Animations/"
             "ANIM_Chicken_Idle2.ANIM_Chicken_Idle2"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> ChickenIdleLayFinder(
        TEXT("/Game/FarmAnimalsPack/Chicken/Animations/"
             "ANIM_Chicken_IdleLay.ANIM_Chicken_IdleLay"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> ChickenWalkFinder(
        TEXT("/Game/FarmAnimalsPack/Chicken/Animations/"
             "ANIM_Chicken_Walk.ANIM_Chicken_Walk"));

    ChickenAnimations =
    {
        ChickenIdle1Finder.Object,
        ChickenIdle2Finder.Object,
        ChickenIdleLayFinder.Object,
        ChickenWalkFinder.Object
    };
    ChickenAnimations.Remove(nullptr);
}

void ADwmValleyLifeDirector::BeginPlay()
{
    Super::BeginPlay();

    RoutineStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() + MariaStartDelay : MariaStartDelay;
    DiscoverValleyActors();
    SuppressMariaWeapons();

    if (MariaActor && MariaMesh)
    {
        if (ACharacter* MariaCharacter = Cast<ACharacter>(MariaActor))
        {
            if (UCharacterMovementComponent* Movement = MariaCharacter->GetCharacterMovement())
            {
                Movement->StopMovementImmediately();
                Movement->DisableMovement();
            }
        }

        // The authored Blueprint remains the visual actor. Collision is disabled only
        // for this short, deterministic walk so porch trim or a chair collision hull
        // cannot turn the scene into a sliding/moonwalking failure.
        MariaActor->SetActorEnableCollision(false);

        if (MariaRelaxedIdleAnimation)
        {
            MariaMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
            MariaMesh->PlayAnimation(MariaRelaxedIdleAnimation, true);
        }

        FTransform ProxyTransform = MariaActor->GetActorTransform();
        DialogueProxy = GetWorld()->SpawnActorDeferred<ADwmNpcActor>(
            ADwmNpcActor::StaticClass(), ProxyTransform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (DialogueProxy)
        {
            DialogueProxy->ConfigureDialogueProxy(EDwmNpcProfile::Maria);
            DialogueProxy->FinishSpawning(ProxyTransform);
#if WITH_EDITOR
            DialogueProxy->SetActorLabel(TEXT("DWM_Maria_Dialogue"));
#endif
        }
    }

    if (!MariaActor || !MariaMesh)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[DWM Valley] BP_Morphpose_Maria was not found; Maria's porch routine is disabled."));
    }
    else if (!RockingChairActor)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[DWM Valley] No SM_RockingChair actor was found; Maria will remain at her placed location."));
    }

    NextChickenAnimationTimes.SetNum(ChickenMeshes.Num());
    const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
    for (int32 Index = 0; Index < ChickenMeshes.Num(); ++Index)
    {
        NextChickenAnimationTimes[Index] = Now + FMath::FRandRange(0.1f, 1.5f);
    }

    UE_LOG(LogTemp, Log,
        TEXT("[DWM Valley] Life director found Maria=%s, chair=%s, chickens=%d."),
        *GetNameSafe(MariaActor), *GetNameSafe(RockingChairActor), ChickenMeshes.Num());
}

void ADwmValleyLifeDirector::DiscoverValleyActors()
{
    float ClosestChairDistanceSquared = TNumericLimits<float>::Max();

    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor || Actor == this)
        {
            continue;
        }

        USkeletalMeshComponent* SkeletalMesh = Actor->FindComponentByClass<USkeletalMeshComponent>();
        const USkeletalMesh* SkeletalMeshAsset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
        const FString SkeletalMeshPath = SkeletalMeshAsset ? SkeletalMeshAsset->GetPathName() : FString();
        const FString Identity = GetActorIdentity(Actor);

        if (!MariaActor
            && (Identity.Contains(TEXT("BP_Morphpose_Maria"), ESearchCase::IgnoreCase)
                || SkeletalMeshPath.Contains(TEXT("SK_Maria_02"), ESearchCase::IgnoreCase)))
        {
            MariaActor = Actor;
            MariaMesh = SkeletalMesh;
            continue;
        }

        if (SkeletalMesh
            && SkeletalMeshPath.Contains(TEXT("/FarmAnimalsPack/Chicken/"), ESearchCase::IgnoreCase))
        {
            ChickenMeshes.AddUnique(SkeletalMesh);
        }
    }

    if (!MariaActor)
    {
        return;
    }

    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor || Actor == this)
        {
            continue;
        }

        UStaticMeshComponent* StaticMesh = Actor->FindComponentByClass<UStaticMeshComponent>();
        const UStaticMesh* StaticMeshAsset = StaticMesh ? StaticMesh->GetStaticMesh() : nullptr;
        if (!StaticMeshAsset
            || !StaticMeshAsset->GetPathName().Contains(TEXT("SM_RockingChair"), ESearchCase::IgnoreCase))
        {
            continue;
        }

        const float DistanceSquared = FVector::DistSquared2D(
            MariaActor->GetActorLocation(), Actor->GetActorLocation());
        if (DistanceSquared < ClosestChairDistanceSquared)
        {
            ClosestChairDistanceSquared = DistanceSquared;
            RockingChairActor = Actor;
        }
    }

    if (RockingChairActor)
    {
        const FVector ChairForward = RockingChairActor->GetActorForwardVector();
        MariaSeatLocation = RockingChairActor->GetActorLocation() - ChairForward * 8.0f;
        // BP_Morphpose_Maria's mesh is authored 90cm below its Character origin.
        MariaSeatLocation.Z += 90.0f;
        MariaApproachLocation = MariaSeatLocation + ChairForward * 95.0f;
        MariaSeatRotation = RockingChairActor->GetActorRotation();
        MariaSeatRotation.Pitch = 0.0f;
        MariaSeatRotation.Roll = 0.0f;
    }
}

void ADwmValleyLifeDirector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    TickMariaRoutine(DeltaSeconds);
    UpdateDialogueProxy();
    UpdateChickenAnimations();

    if (UWorld* World = GetWorld())
    {
        const float Now = World->GetTimeSeconds();
        if (Now >= NextWeaponSuppressionTime)
        {
            SuppressMariaWeapons();
            NextWeaponSuppressionTime = Now + 0.5f;
        }
    }
}

void ADwmValleyLifeDirector::StartMariaWalk()
{
    if (!MariaActor || !MariaMesh || !RockingChairActor)
    {
        MariaPhase = EDwmMariaRoutinePhase::Seated;
        return;
    }

    MariaPhase = EDwmMariaRoutinePhase::WalkingToPorch;
    if (MariaWalkAnimation)
    {
        MariaMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        MariaMesh->PlayAnimation(MariaWalkAnimation, true);
    }
}

void ADwmValleyLifeDirector::TickMariaRoutine(float DeltaSeconds)
{
    if (!MariaActor || !GetWorld())
    {
        return;
    }

    const float Now = GetWorld()->GetTimeSeconds();
    if (MariaPhase == EDwmMariaRoutinePhase::Waiting)
    {
        if (Now >= RoutineStartTime)
        {
            StartMariaWalk();
        }
        return;
    }

    if (MariaPhase == EDwmMariaRoutinePhase::WalkingToPorch)
    {
        const FVector Current = MariaActor->GetActorLocation();
        FVector ToApproach = MariaApproachLocation - Current;
        const float Remaining = ToApproach.Size2D();

        if (Remaining <= 12.0f)
        {
            SeatMaria();
            return;
        }

        ToApproach.Z = 0.0f;
        const FVector Direction = ToApproach.GetSafeNormal();
        const float StepDistance = FMath::Min(MariaWalkSpeed * DeltaSeconds, Remaining);
        FVector Next = Current + Direction * StepDistance;
        Next.Z = FMath::FInterpTo(Current.Z, MariaApproachLocation.Z, DeltaSeconds, 2.0f);
        MariaActor->SetActorLocation(Next, false, nullptr, ETeleportType::TeleportPhysics);

        const FRotator DesiredRotation(0.0f, Direction.Rotation().Yaw, 0.0f);
        MariaActor->SetActorRotation(FMath::RInterpConstantTo(
            MariaActor->GetActorRotation(), DesiredRotation, DeltaSeconds, MariaTurnSpeed));
        return;
    }

    if (MariaPhase == EDwmMariaRoutinePhase::SittingDown && Now >= SitTransitionEndTime)
    {
        MariaPhase = EDwmMariaRoutinePhase::Seated;
        if (CanPlayOnMaria(MariaSitIdleAnimation))
        {
            MariaMesh->PlayAnimation(MariaSitIdleAnimation, true);
        }
        else if (MariaRelaxedIdleAnimation)
        {
            MariaMesh->PlayAnimation(MariaRelaxedIdleAnimation, true);
        }
    }
}

void ADwmValleyLifeDirector::SeatMaria()
{
    MariaActor->SetActorLocation(MariaSeatLocation, false, nullptr, ETeleportType::TeleportPhysics);
    MariaActor->SetActorRotation(MariaSeatRotation, ETeleportType::TeleportPhysics);

    if (CanPlayOnMaria(MariaStandToSitAnimation))
    {
        MariaPhase = EDwmMariaRoutinePhase::SittingDown;
        MariaMesh->PlayAnimation(MariaStandToSitAnimation, false);
        SitTransitionEndTime = GetWorld()->GetTimeSeconds()
            + FMath::Max(0.1f, MariaStandToSitAnimation->GetPlayLength());
    }
    else
    {
        MariaPhase = EDwmMariaRoutinePhase::Seated;
        if (CanPlayOnMaria(MariaSitIdleAnimation))
        {
            MariaMesh->PlayAnimation(MariaSitIdleAnimation, true);
        }
        else if (MariaRelaxedIdleAnimation)
        {
            MariaMesh->PlayAnimation(MariaRelaxedIdleAnimation, true);
        }
        UE_LOG(LogTemp, Warning,
            TEXT("[DWM Valley] Maria's seated clips are not compatible with her current mesh; using native idle."));
    }
}

bool ADwmValleyLifeDirector::CanPlayOnMaria(const UAnimSequence* Animation) const
{
    if (!Animation || !Animation->GetSkeleton() || !MariaMesh || !MariaMesh->GetSkeletalMeshAsset())
    {
        return false;
    }
    return Animation->GetSkeleton()->IsCompatibleMesh(MariaMesh->GetSkeletalMeshAsset());
}

void ADwmValleyLifeDirector::SuppressMariaWeapons()
{
    if (!MariaActor)
    {
        return;
    }

    TArray<UActorComponent*> Components;
    MariaActor->GetComponents(Components);
    for (UActorComponent* Component : Components)
    {
        if (!Component)
        {
            continue;
        }

        // Some vendor Blueprints give the gun a generic component or child-actor name.
        // Include the assigned mesh path so those weapon visuals are still suppressed.
        const FString Identity = GetComponentIdentity(Component);
        if (LooksLikeWeapon(Identity))
        {
            Component->SetComponentTickEnabled(false);
            if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
            {
                Primitive->SetVisibility(false, true);
                Primitive->SetHiddenInGame(true, true);
                Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }
        }
    }

    TArray<AActor*> AttachedActors;
    MariaActor->GetAttachedActors(AttachedActors, true, true);
    for (AActor* Attached : AttachedActors)
    {
        if (Attached && LooksLikeWeapon(GetActorAndComponentIdentity(Attached)))
        {
            Attached->SetActorHiddenInGame(true);
            Attached->SetActorEnableCollision(false);
            Attached->SetActorTickEnabled(false);
        }
    }
}

void ADwmValleyLifeDirector::UpdateDialogueProxy()
{
    if (!DialogueProxy || !MariaActor)
    {
        return;
    }

    DialogueProxy->SetActorLocationAndRotation(
        MariaActor->GetActorLocation(), MariaActor->GetActorRotation(), false, nullptr,
        ETeleportType::TeleportPhysics);
}

void ADwmValleyLifeDirector::UpdateChickenAnimations()
{
    if (!GetWorld())
    {
        return;
    }

    const float Now = GetWorld()->GetTimeSeconds();
    for (int32 Index = 0; Index < ChickenMeshes.Num(); ++Index)
    {
        if (NextChickenAnimationTimes.IsValidIndex(Index)
            && Now >= NextChickenAnimationTimes[Index])
        {
            PlayRandomChickenAnimation(Index, Now);
        }
    }
}

void ADwmValleyLifeDirector::PlayRandomChickenAnimation(int32 ChickenIndex, float NowSeconds)
{
    if (!ChickenMeshes.IsValidIndex(ChickenIndex)
        || !NextChickenAnimationTimes.IsValidIndex(ChickenIndex)
        || ChickenAnimations.Num() == 0)
    {
        return;
    }

    USkeletalMeshComponent* ChickenMesh = ChickenMeshes[ChickenIndex];
    UAnimSequence* Animation = ChickenAnimations[FMath::RandRange(0, ChickenAnimations.Num() - 1)];
    if (!ChickenMesh || !Animation)
    {
        NextChickenAnimationTimes[ChickenIndex] = NowSeconds + 2.0f;
        return;
    }

    ChickenMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    ChickenMesh->PlayAnimation(Animation, false);

    if (AActor* ChickenActor = ChickenMesh->GetOwner())
    {
        FRotator Rotation = ChickenActor->GetActorRotation();
        Rotation.Yaw += FMath::FRandRange(-35.0f, 35.0f);
        ChickenActor->SetActorRotation(Rotation);
    }

    NextChickenAnimationTimes[ChickenIndex] = NowSeconds
        + FMath::Max(Animation->GetPlayLength(), FMath::FRandRange(2.5f, 6.5f));
}
