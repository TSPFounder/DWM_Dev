// DwmPendulumActor.h
// A DWM world actor representing one rigid-body block.
// Loads a StaticMesh from the asset binding path, then on each tick
// advances through the precomputed SimSamples to drive rotation.
// For the Phase 3 tracer bullet this is a single pendulum arm (cylinder)
// swinging under small-angle approximation physics from Simscape.
//
// NOT PENDULUM-SPECIFIC despite the name, and deliberately kept that way.
// Any block whose motion is one angle over time drives through this actor --
// the turbine's `block_rotor` is the second such block and reuses it as-is.
// What varies between mechanisms is which axis the angle turns about
// (SpinAxis), whether playback starts immediately or waits for a story beat
// (bStartPaused), and what happens when the samples run out (EndBehaviour).
// All three are configuration rather than a second copy of this class.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DwmWorldPackageTypes.h"
#include "DwmPendulumActor.generated.h"

/**
 * Which local axis the sim angle turns the mesh about.
 *
 * Roll is the pendulum's swinging plane and remains the default so the tracer
 * bullet is unaffected. A rotor's correct axis depends on how its mesh was
 * authored, so this is set per mechanism rather than assumed.
 */
UENUM(BlueprintType)
enum class EDwmSpinAxis : uint8
{
    Roll  UMETA(DisplayName = "Roll (pendulum swing plane)"),
    Pitch UMETA(DisplayName = "Pitch"),
    Yaw   UMETA(DisplayName = "Yaw")
};

/**
 * What happens when playback reaches the last sample.
 *
 * A PENDULUM AND A ROTOR WANT DIFFERENT ANSWERS HERE, which is the whole
 * reason this is a choice:
 *
 *   Loop              Restart, carrying the travelled angle across the seam.
 *                     Correct for periodic motion; the tracer bullet default.
 *
 *   HoldFinalVelocity Stay on the last sample and keep turning at the rate
 *                     that sample recorded. Correct for a run that ramps from
 *                     rest to a steady speed -- looping such a run would drop
 *                     the rotor back to rest and ramp it up again, so a
 *                     turbine that was meant to end up running would instead
 *                     cyclically slow down and speed up forever.
 *
 *   Freeze            Stop on the last sample and hold that pose.
 */
UENUM(BlueprintType)
enum class EDwmPlaybackEnd : uint8
{
    Loop              UMETA(DisplayName = "Loop (periodic motion)"),
    HoldFinalVelocity UMETA(DisplayName = "Hold final velocity (keep turning)"),
    Freeze            UMETA(DisplayName = "Freeze on last sample")
};

UCLASS()
class DWM_DEV_API ADwmPendulumActor : public AActor
{
    GENERATED_BODY()

public:
    ADwmPendulumActor();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    // ------------------------------------------------------------------
    // Initialise from world-package data (called by DwmGameInstance
    // immediately after spawning).
    // ------------------------------------------------------------------

    /** Populate this actor from package data. Must be called before play. */
    UFUNCTION(BlueprintCallable, Category = "DWM")
    void InitialiseFromPackage(const FDwmBlock&         InBlock,
                               const FDwmAssetBinding&  InBinding,
                               const TArray<FDwmSimSample>& InSamples);

    // ------------------------------------------------------------------
    // Playback control
    //
    // The Act 1 turbine is stationary and the Act 3 turbine runs; those are
    // the same actor with playback held and then released, NOT two states of
    // a mechanism. Deliberately NOT inferred from trade state or from any
    // other game state -- the caller decides when the beat lands, the same
    // way ADwmNpcActor::UnlockFarewell is an explicit external call.
    // ------------------------------------------------------------------

    /** Begin (or restart) playback from the first sample.
        CallInEditor: shows as a button in the Details panel, including while selected
        during PIE/Simulate, so the parked -> startup -> generating sequence can be
        triggered by hand without an input binding or console command while iterating
        on the model or on SpinAxis. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "DWM|Playback")
    void StartPlayback();

    /** Hold at the current sample. The mesh keeps its present orientation.
        Also CallInEditor -- see StartPlayback. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "DWM|Playback")
    void PausePlayback();

    UFUNCTION(BlueprintPure, Category = "DWM|Playback")
    bool IsPlaying() const { return bPlaying; }

    // ------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------

    /** Local axis the sim angle rotates the mesh about.
        Class default is Roll, correct for the pendulum tracer's swinging cylinder.
        block_rotor is set to Pitch instead -- see ADwmGameInstance::SpawnWorldActors,
        which sets it PER-BLOCK on spawn rather than here, precisely so this default
        stays right for the pendulum while the turbine gets its own confirmed axis.
        Confirmed 2026-08-18 against the placed WindTurbine_C Blueprint's own working
        rotor animation, not guessed: its MakeRotator feeding the Rotor component's
        AddLocalRotation has Y (Pitch) wired to speed, X and Z left at literal 0. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM")
    EDwmSpinAxis SpinAxis = EDwmSpinAxis::Roll;

    /**
     * When true the actor holds still after initialising and moves only once
     * StartPlayback() is called. Default false so the tracer bullet still
     * starts swinging the moment it spawns.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Playback")
    bool bStartPaused = false;

    /** What to do when the samples run out. See EDwmPlaybackEnd. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Playback")
    EDwmPlaybackEnd EndBehaviour = EDwmPlaybackEnd::Loop;

    // ------------------------------------------------------------------
    // Runtime state (readable from Blueprint for debug HUD etc.)
    // ------------------------------------------------------------------

    UPROPERTY(BlueprintReadOnly, Category = "DWM")
    FString BlockId;

    UPROPERTY(BlueprintReadOnly, Category = "DWM")
    FString BlockName;

    /** Current sim-sample index (advances each tick). */
    UPROPERTY(BlueprintReadOnly, Category = "DWM")
    int32 CurrentSampleIndex = 0;

    /**
     * Angle in radians as read from the active sim sample -- the raw model
     * output, NOT the wrapped value handed to the FRotator. Kept raw so a debug
     * HUD reports what the simulation says rather than what the mesh is doing.
     */
    UPROPERTY(BlueprintReadOnly, Category = "DWM")
    float CurrentAngleRad = 0.0f;

private:
    UPROPERTY(VisibleAnywhere, Category = "DWM")
    UStaticMeshComponent* MeshComponent;

    TArray<FDwmSimSample> SimSamples;

    // Elapsed time used to index into the sim-sample array.
    float ElapsedTime = 0.0f;

    // Angle accumulated by every completed playback loop so far. See Tick().
    float LoopAngleOffsetRad = 0.0f;

    // Angle accumulated past the last sample under HoldFinalVelocity.
    float FreeSpinAngleRad = 0.0f;

    // Rotation this actor was placed or spawned with. The sim angle is applied
    // relative to it, so a mesh standing at some yaw on a hillside keeps it.
    FRotator BaseRotation = FRotator::ZeroRotator;

    // Whether playback is currently advancing.
    bool bPlaying = false;

    // Whether InitialiseFromPackage has been called successfully.
    bool bInitialised = false;

    // Load the StaticMesh at the given UE content path and apply it.
    void ApplyMesh(const FString& AssetPath);

    // Reset every playback accumulator to the start of the run.
    void ResetPlaybackState();
};
