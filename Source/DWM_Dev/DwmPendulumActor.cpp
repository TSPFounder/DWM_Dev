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

    // Capture the placed/spawned orientation before any sim angle is applied.
    // Spawned tracer actors use FRotator::ZeroRotator, so this is identity for
    // the pendulum; an actor placed in a level keeps whatever it was built with.
    //
    // This is also what a paused actor sits at: with bStartPaused set, Tick
    // never touches the rotation, so the mesh holds exactly the pose it was
    // placed in until StartPlayback() is called. A turbine standing still in
    // Act 1 is therefore the ABSENCE of playback, not a zero-motion sim run.
    BaseRotation = GetActorRotation();

    if (bInitialised)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[DWM] %s: BeginPlay — %d sim samples ready, %s."),
            *BlockName, SimSamples.Num(),
            bPlaying ? TEXT("starting playback") : TEXT("holding until StartPlayback()"));
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

    if (!bInitialised || SimSamples.Num() == 0 || !bPlaying)
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

    if (CurrentSampleIndex >= SimSamples.Num() - 1)
    {
        switch (EndBehaviour)
        {
        case EDwmPlaybackEnd::HoldFinalVelocity:
            // Keep turning at the rate the run ended on. Velocity is rad/s in
            // the same convention as Position, so this is the model's own
            // steady-state speed continued -- not a made-up constant.
            FreeSpinAngleRad += SimSamples.Last().Velocity * DeltaTime;
            break;

        case EDwmPlaybackEnd::Freeze:
            bPlaying = false;
            break;

        case EDwmPlaybackEnd::Loop:
        default:
            // CARRY THE TRAVELLED ANGLE ACROSS THE SEAM rather than snapping
            // back to the first sample's value. A pendulum ends its run near
            // where it started, so the accumulated offset is ~0 and the tracer
            // bullet behaves exactly as before. An unwrapped, monotonically
            // increasing azimuth would otherwise visibly rewind once per loop.
            LoopAngleOffsetRad +=
                SimSamples.Last().Position - SimSamples[0].Position;
            CurrentSampleIndex = 0;
            ElapsedTime        = 0.0f;
            break;
        }
    }

    // The sim stores angle in radians (positive = forward swing); UE uses degrees.
    const float SampleAngleRad = SimSamples[CurrentSampleIndex].Position;
    CurrentAngleRad            = SampleAngleRad;

    // Wrap into one revolution before handing it to FRotator. This is an identity
    // for a pendulum, whose angle never approaches 360 degrees, and necessary for
    // an unwrapped rotor azimuth, which reaches thousands of degrees over a run.
    const float AngleDeg = FMath::Fmod(
        FMath::RadiansToDegrees(
            SampleAngleRad + LoopAngleOffsetRad + FreeSpinAngleRad),
        360.0f);

    FRotator Spin = FRotator::ZeroRotator;
    switch (SpinAxis)
    {
    case EDwmSpinAxis::Pitch:
        Spin.Pitch = AngleDeg;
        break;
    case EDwmSpinAxis::Yaw:
        Spin.Yaw = AngleDeg;
        break;
    case EDwmSpinAxis::Roll:
    default:
        // Pivot is at the top of the arm — offset the mesh so its top sits
        // at the actor origin, then rotate around that origin.
        Spin.Roll = AngleDeg;
        break;
    }

    // Quaternion composition, NOT FRotator addition. Base * Spin turns the mesh
    // about its OWN axis after the base orientation is applied; adding Euler
    // angles componentwise gives a different (and wrong) result as soon as
    // BaseRotation is non-zero. Identical to the old behaviour while base is
    // identity, which is every case the tracer bullet exercises.
    SetActorRotation(FQuat(BaseRotation) * FQuat(Spin));
}

void ADwmPendulumActor::StartPlayback()
{
    ResetPlaybackState();
    bPlaying = true;

    UE_LOG(LogTemp, Log,
        TEXT("[DWM] %s: StartPlayback — %d samples, end behaviour %s."),
        *BlockName, SimSamples.Num(),
        *UEnum::GetValueAsString(EndBehaviour));
}

void ADwmPendulumActor::PausePlayback()
{
    bPlaying = false;

    UE_LOG(LogTemp, Log,
        TEXT("[DWM] %s: PausePlayback — holding at sample %d."),
        *BlockName, CurrentSampleIndex);
}

void ADwmPendulumActor::ResetPlaybackState()
{
    CurrentSampleIndex = 0;
    ElapsedTime        = 0.0f;
    LoopAngleOffsetRad = 0.0f;
    FreeSpinAngleRad   = 0.0f;
}

void ADwmPendulumActor::InitialiseFromPackage(
    const FDwmBlock&            InBlock,
    const FDwmAssetBinding&     InBinding,
    const TArray<FDwmSimSample>& InSamples)
{
    BlockId   = InBlock.BlockId;
    BlockName = InBlock.Name;
    SimSamples = InSamples;

    // Fresh data means a fresh playback run.
    ResetPlaybackState();
    bPlaying = !bStartPaused;

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
