// DwmPendulumActor.h
// A DWM world actor representing one rigid-body block.
// Loads a StaticMesh from the asset binding path, then on each tick
// advances through the precomputed SimSamples to drive rotation.
// For the Phase 3 tracer bullet this is a single pendulum arm (cylinder)
// swinging under small-angle approximation physics from Simscape.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DwmWorldPackageTypes.h"
#include "DwmPendulumActor.generated.h"

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
    // Runtime state (readable from Blueprint for debug HUD etc.)
    // ------------------------------------------------------------------

    UPROPERTY(BlueprintReadOnly, Category = "DWM")
    FString BlockId;

    UPROPERTY(BlueprintReadOnly, Category = "DWM")
    FString BlockName;

    /** Current sim-sample index (advances each tick). */
    UPROPERTY(BlueprintReadOnly, Category = "DWM")
    int32 CurrentSampleIndex = 0;

    /** Current pendulum angle in radians (from the active sim sample). */
    UPROPERTY(BlueprintReadOnly, Category = "DWM")
    float CurrentAngleRad = 0.0f;

private:
    UPROPERTY(VisibleAnywhere, Category = "DWM")
    UStaticMeshComponent* MeshComponent;

    TArray<FDwmSimSample> SimSamples;

    // Elapsed time used to index into the sim-sample array.
    float ElapsedTime = 0.0f;

    // Whether InitialiseFromPackage has been called successfully.
    bool bInitialised = false;

    // Load the StaticMesh at the given UE content path and apply it.
    void ApplyMesh(const FString& AssetPath);
};
