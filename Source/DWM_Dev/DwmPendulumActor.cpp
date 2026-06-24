// DwmPendulumActor.cpp

#include "DwmPendulumActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ADwmPendulumActor::ADwmPendulumActor()
{
    PrimaryActorTick.bCanEverTick = true;

    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(
        TEXT("DwmMeshComponent"));
    RootComponent = MeshComponent;
}

void ADwmPendulumActor::BeginPlay()
{
    Super::BeginPlay();

    if (bInitialised)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[DWM] %s: BeginPlay — %d sim samples ready, starting playback."),
            *BlockName, SimSamples.Num());
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[DWM] DwmPendulumActor BeginPlay: not initialised yet."));
    }
}

void ADwmPendulumActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!bInitialised || SimSamples.Num() == 0)
        return;

    // Advance elapsed time, then find the closest sim sample.
    ElapsedTime += DeltaTime;

    // Find the sample whose Time is closest to ElapsedTime.
    // Samples are ordered by time; walk forward until we overshoot.
    while (CurrentSampleIndex < SimSamples.Num() - 1 &&
           SimSamples[CurrentSampleIndex + 1].Time <= ElapsedTime)
    {
        CurrentSampleIndex++;
    }

    // Loop: when we reach the last sample, restart.
    if (CurrentSampleIndex >= SimSamples.Num() - 1)
    {
        CurrentSampleIndex = 0;
        ElapsedTime        = 0.0f;
    }

    // Apply the pendulum angle as a rotation around the Y axis.
    // The sim stores angle in radians (positive = forward swing).
    // UE uses degrees; Y-axis rotation gives the swinging plane.
    const float AngleRad = SimSamples[CurrentSampleIndex].Position;
    CurrentAngleRad      = AngleRad;
    const float AngleDeg = FMath::RadiansToDegrees(AngleRad);

    // Pivot is at the top of the arm — offset the mesh so its top sits
    // at the actor origin, then rotate around that origin.
    SetActorRotation(FRotator(0.0f, 0.0f, AngleDeg));
}

void ADwmPendulumActor::InitialiseFromPackage(
    const FDwmBlock&            InBlock,
    const FDwmAssetBinding&     InBinding,
    const TArray<FDwmSimSample>& InSamples)
{
    BlockId   = InBlock.BlockId;
    BlockName = InBlock.Name;
    SimSamples = InSamples;

    UE_LOG(LogTemp, Log,
        TEXT("[DWM] %s: Initialising — %d sim samples, mesh='%s'"),
        *BlockName, SimSamples.Num(), *InBinding.AssetPath);

    ApplyMesh(InBinding.AssetPath);
    bInitialised = true;
}

void ADwmPendulumActor::ApplyMesh(const FString& AssetPath)
{
    // AssetPath is a UE content path e.g.
    // "/Engine/BasicShapes/Cylinder.Cylinder"
    // We load it as a UStaticMesh and assign it to MeshComponent.

    UStaticMesh* Mesh = Cast<UStaticMesh>(
        StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *AssetPath));

    if (Mesh)
    {
        MeshComponent->SetStaticMesh(Mesh);
        UE_LOG(LogTemp, Log,
            TEXT("[DWM] %s: Mesh loaded OK — %s"), *BlockName, *AssetPath);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[DWM] %s: Failed to load mesh at '%s'. "
                 "Actor will be invisible but physics will still run."),
            *BlockName, *AssetPath);
    }
}
