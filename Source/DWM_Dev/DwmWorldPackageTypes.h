// DwmWorldPackageTypes.h
// USTRUCTs that define the DWM world-package schema.
//
// These structs are the single source of truth for the world-package
// database layout. USQLite generates its SQL schema from these via property
// reflection, so the DWMStudio-side exporter must write a SQLite file whose
// tables and columns match these property names exactly.
//
// Convention (from USQLite's serializer):
//   - Primitive properties (FString, int32, float, bool) -> string/number columns
//   - Struct properties                                  -> JSON text columns
//   - Only properties tagged UPROPERTY(SaveGame) are serialized
//
// For the Phase 3 tracer bullet (a single pendulum) these three tables are
// enough: the blocks in the world, their numeric parameters, and the asset
// meshes bound to each block for visual representation.

#pragma once

#include "CoreMinimal.h"
#include "DwmWorldPackageTypes.generated.h"

/**
 * A single block in a DWM world — one physical/logical element such as the
 * pendulum arm or bob. Maps to a SysML block on the C# side.
 */
USTRUCT(BlueprintType)
struct FDwmBlock
{
    GENERATED_BODY()

    /** Stable unique id (GUID string) — matches DWM.db BlockId. */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    FString BlockId;

    /** Human-readable name, e.g. "PendulumArm". */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    FString Name;

    /** Block classification, e.g. "RigidBody", "Joint". */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    FString BlockType;

    FDwmBlock() = default;
};

/**
 * A numeric parameter belonging to a block — arm length, bob mass, gravity,
 * initial angle, etc. These are the values Simscape consumes and that drive
 * the geometry and physics.
 */
USTRUCT(BlueprintType)
struct FDwmParameter
{
    GENERATED_BODY()

    /** Owning block id (foreign key to FDwmBlock.BlockId). */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    FString BlockId;

    /** Parameter name, e.g. "armLength". */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    FString Name;

    /** Numeric value in SI base units. */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    float Value = 0.0f;

    /** Unit label, e.g. "m", "kg", "deg". */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    FString Unit;

    FDwmParameter() = default;
};

/**
 * Binds a block to a UE asset for visual/physical representation.
 * Multiple bindings per block are allowed (one Visual mesh, one Collision
 * mesh, a Material, etc.). Assets are decorative — physics comes from the
 * simulation results, not from these meshes.
 */
USTRUCT(BlueprintType)
struct FDwmAssetBinding
{
    GENERATED_BODY()

    /** Owning block id (foreign key to FDwmBlock.BlockId). */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    FString BlockId;

    /** UE content path, e.g. "/Game/Collections/Sci/SM_Rod.SM_Rod". */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    FString AssetPath;

    /** Asset class, e.g. "StaticMesh", "Material", "NiagaraSystem". */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    FString AssetType;

    /** Role in the block: "Visual", "Collision", "Material", "Effect". */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    FString Role;

    FDwmAssetBinding() = default;
};

/**
 * One row of simulation output — the state of a block at a single timestep.
 * The pendulum's angle over time lives here; UE reads these rows to drive
 * the actor's rotation each frame. Written by the MATLAB/Simscape stage
 * (via Database Toolbox) and carried into the package by the export bridge.
 */
USTRUCT(BlueprintType)
struct FDwmSimSample
{
    GENERATED_BODY()

    /** Owning block id (foreign key to FDwmBlock.BlockId). */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    FString BlockId;

    /** Simulation time in seconds. */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    float Time = 0.0f;

    /** Generalized position (e.g. pendulum angle in radians). */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    float Position = 0.0f;

    /** Generalized velocity (e.g. angular velocity). */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    float Velocity = 0.0f;

    FDwmSimSample() = default;
};

/**
 * Top-level world metadata — one row describing the world the package holds.
 */
USTRUCT(BlueprintType)
struct FDwmWorldInfo
{
    GENERATED_BODY()

    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    FString WorldId;

    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    FString Name;

    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    FString Description;

    /** Schema version — lets the loader handle older packages. */
    UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DWM")
    int32 SchemaVersion = 1;

    FDwmWorldInfo() = default;
};
