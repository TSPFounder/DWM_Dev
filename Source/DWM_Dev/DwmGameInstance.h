// DwmGameInstance.h
// GameInstance that handles dwmworld:// deep links and spawns DWM world actors.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "DwmWorldPackageTypes.h"
#include "DwmGameInstance.generated.h"

class UButton;
class UAnimInstance;
class UMaterialInterface;
class USaveGame;
class USkeletalMesh;
class UStaticMesh;
class UUserWidget;
class AActor;
class UScriptStruct;
class ADwmPendulumActor;

/** A world-independent copy of one visible mesh in the character the user confirmed.
    CharacterCustomizer assembles its avatar from several runtime mesh components. Keeping
    those final component assets is more reliable for transition imagery than replaying the
    marketplace Blueprint's save/load graph inside Labgames' unrelated transition GameMode. */
USTRUCT()
struct FDwmCapturedCharacterMesh
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    TObjectPtr<USkeletalMesh> SkeletalMesh;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> StaticMesh;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInterface>> Materials;

    UPROPERTY(Transient)
    TObjectPtr<UClass> AnimClass;

    UPROPERTY(Transient)
    FTransform RelativeTransform = FTransform::Identity;

    UPROPERTY(Transient)
    TMap<FName, float> MorphTargets;

    /** LOD 0 material visibility from the confirmed live character. CharacterCustomizer
        hides body material sections beneath apparel to prevent skin clipping. */
    UPROPERTY(Transient)
    TArray<uint8> MaterialSectionVisibilityLOD0;

    /** Bones hidden by CharacterCustomizer for the selected outfit. */
    UPROPERTY(Transient)
    TArray<FName> HiddenBones;

    UPROPERTY(Transient)
    int32 LeaderMeshIndex = INDEX_NONE;

    /** Hair uses an AnimBP that copies the pose from its attached skeletal parent instead
        of Unreal's LeaderPose relationship. */
    UPROPERTY(Transient)
    int32 AttachmentMeshIndex = INDEX_NONE;

    UPROPERTY(Transient)
    FName SourceComponentName;
};

USTRUCT(BlueprintType)
struct FDwmCommunityEconomyState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "DWM|Economy")
    FString CommunityId;

    UPROPERTY(BlueprintReadOnly, Category = "DWM|Economy")
    FString CommunityName;

    UPROPERTY(BlueprintReadOnly, Category = "DWM|Economy")
    double StoneBalance = 0.0;

    UPROPERTY(BlueprintReadOnly, Category = "DWM|Economy")
    double DollarVaultBalance = 0.0;

    UPROPERTY(BlueprintReadOnly, Category = "DWM|Economy")
    double DollarVaultThreshold = 0.0;

    UPROPERTY(BlueprintReadOnly, Category = "DWM|Economy")
    FString FailureState;
};

UCLASS(config=Game)
class DWM_DEV_API UDwmGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    virtual void Init() override;
    virtual void OnStart() override;
    virtual void Shutdown() override;

    UFUNCTION(BlueprintCallable, Category = "DWM")
    void HandleDwmUrl(const FString& Url);

    UFUNCTION(BlueprintCallable, Category = "DWM|Economy")
    bool RefreshEconomyState();

    /** Day 18's original fixed demo trade -- kept as-is (reuse, don't rebuild) as a thin
        wrapper over the Day 20 generalized path below, with the same 20-Stone-for-20-Grain
        parameters it always had. */
    UFUNCTION(BlueprintCallable, Category = "DWM|Economy")
    bool ExecuteMountainBuysGrainFromValley();

    /** Day 20: validates and writes ANY configured trade via FDwmEconomyWriter::WriteTrade
        (the proven Day 17 write path, called here with different parameters, not
        reimplemented), then refreshes EconomyStates and confirms the buyer/seller deltas came
        out exactly right -- same before/after verification ExecuteMountainBuysGrainFromValley
        already did, just generalized to work for any community/resource/amount instead of one
        hardcoded pair.
        BuyerCommunityId pays Stone and receives the resource -- passed directly as
        WriteTrade's FromCommunityId (the payer). SellerCommunityId receives Stone and
        provides the resource -- passed directly as WriteTrade's ToCommunityId (the
        receiver). No crossing/inversion between this function's parameter names and
        WriteTrade's own. */
    UFUNCTION(BlueprintCallable, Category = "DWM|Economy")
    bool ExecuteConfiguredTrade(const FString& BuyerCommunityId, const FString& SellerCommunityId,
        const FString& ResourceId, double Amount, double Quantity, const FString& Memo);

    /** Returns the distinct communities that have completed trades with the buyer. */
    bool GetCompletedTradePartners(const FString& BuyerCommunityId, TArray<FString>& OutPartners) const;

    /** Hank's conversation progress lives in the GameInstance so OpenLevel transitions do
        not make him repeat his first-meeting dialogue during the same play session. */
    bool HasDeliveredHankOpening() const { return bHankOpeningDelivered; }
    void MarkHankOpeningDelivered() { bHankOpeningDelivered = true; }
    bool IsHankFarewellUnlocked() const { return bHankFarewellUnlocked; }
    void SetHankFarewellUnlocked(bool bUnlocked = true) { bHankFarewellUnlocked = bUnlocked; }
    int32 GetHankReturnVisitCount() const { return HankReturnVisitCount; }
    void IncrementHankReturnVisitCount() { ++HankReturnVisitCount; }
    int32 GetHankAmbientCursor() const { return HankAmbientCursor; }
    void SetHankAmbientCursor(int32 InCursor) { HankAmbientCursor = FMath::Max(0, InCursor); }

    /** The Act 3 payoff: releases the turbine rotor from Parked so it plays the real
        parked -> startup -> generating sequence exported from the R2011a model, rather
        than starting the moment the level loads. Called from ADwmNpcActor::AdvanceDialogue
        when Hank's ReturnAllTradesComplete sequence finishes -- see that call site for why
        it is paired there with UnlockFarewell rather than left for either to infer the
        other from trade state.
        Returns false (and changes nothing) if block_rotor was never spawned -- e.g. the
        pendulum world is loaded instead of the turbine one -- or no longer exists. Safe
        to call more than once: a rotor already playing is left alone. */
    UFUNCTION(BlueprintCallable, Category = "DWM")
    bool StartTurbineActThreePayoff();

    /** Persists across an OpenLevel the way bHankFarewellUnlocked does (see BeginPlay's
        restore block in DwmNpcActor.cpp) so a player who triggers Act 3, leaves Mountain,
        and comes back does not find the rotor freshly parked under a Farewell line that
        already says it happened. SpawnWorldActors checks this before holding block_rotor
        paused; it does not fast-forward the resumed run to a steady-state pose, so a
        revisit replays the parked/startup shape once more rather than resuming mid-run --
        an accepted simplification, not an attempt at seamless resume. */
    bool IsTurbineRotorStarted() const { return bTurbineRotorStarted; }

    UPROPERTY(BlueprintReadOnly, Category = "DWM")
    FString PendingWorldId;

    /** Which world-package Init() loads when there is no dwmworld:// launch URL -- i.e.
        every ordinary PIE session. Config so a target world (e.g. "turbine" while testing
        the supervisor startup sequence) can be switched from DefaultGame.ini:

            [/Script/DWM_Dev.DwmGameInstance]
            DebugPIEWorldId=turbine

        WITHOUT recompiling to change it back and forth. Defaults to "pendulum", which is
        the literal Init() always used before this property existed -- an unedited ini
        reproduces exactly the old hardcoded behaviour. */
    UPROPERTY(config, EditDefaultsOnly, BlueprintReadOnly, Category = "DWM|Debug")
    FString DebugPIEWorldId = TEXT("pendulum");

    UPROPERTY(BlueprintReadOnly, Category = "DWM|Economy")
    TArray<FDwmCommunityEconomyState> EconomyStates;

    UPROPERTY(BlueprintReadOnly, Category = "DWM|Economy")
    FString LastEconomyStatusMessage;

private:
    /** Marketplace demo levels need to run their own GameMode, input, and UI without DWM's
        runtime showcase actors being injected into their world. */
    bool ShouldRunDwmRuntimeSystems() const;
    /** Performs GameMode-dependent world setup after the GameMode and controller exist. */
    void InitializeWorldRuntime();
    /** Runs the same setup after OpenLevel, since UGameInstance::OnStart only runs once. */
    void HandlePostLoadMap(UWorld* LoadedWorld);
    /** Restores mouse/UI input after the marketplace Customizer Pawn has initialized.
        This only runs for maps whose active GameMode comes from CharacterCustomizer. */
    void EnableCharacterCustomizerInput();
    /** Receives the marketplace widget's Confirm click without modifying that asset. */
    UFUNCTION()
    void HandleCharacterCreatorConfirmed();
    /** Lets the marketplace Confirm handler finish saving before loading Mountain. */
    void TravelFromCharacterCreatorToMountain();
    /** Captures the live customized appearance after the marketplace Confirm handler runs. */
    void CaptureCharacterCreatorSelection();
    /** Reloads the marketplace SaveGame after its Confirm handler has written preset/apparel/hair. */
    bool RefreshCapturedCharacterSelectionFromSave();
    /** Captures the final assembled meshes that are visibly attached to the live avatar. */
    void CaptureVisibleCharacterMeshes(AActor* LiveCharacterActor, UObject* CustomizerContext);
    /** Resolves apparel and hair row names through the confirmed preset's data tables. */
    void CaptureSelectedCharacterAssets(
        const void* SavedData,
        const UScriptStruct* SavedDataStruct,
        UObject* FallbackPreset = nullptr);
    /** Replaces Labgames' abstract rotating mesh with the captured player character. */
    void SetupTransitionCharacterDisplay();
    /** Copies the captured live player appearance onto a spawned customizable character. */
    bool ApplySavedCharacterAppearance(UObject* CharacterObject);
    /** True only for Labgames' intermediate three-dimensional transition map. */
    bool IsLevelTransitionWorld() const;
    /** Places the gameplay pawn at the arrival marker for the map it travelled from. */
    void ApplyRouteSpecificTransitionArrival();
    /** Returns the stable short map name without PIE's UEDPIE prefix. */
    static FName GetStableMapName(const UWorld* World);
    /** True for the five playable DWM community maps. */
    static bool IsCommunityMap(FName MapName);
    /** The map a world package's blocks belong in, or NAME_None for a package that is
        not tied to one. See the definition for why "pendulum" is deliberately absent. */
    static FName GetHostMapName(const FString& WorldId);
    bool TryGetLaunchUrl(FString& OutUrl) const;
    void LoadDwmWorld(const FString& WorldId);
    void SpawnWorldActors();
    void SpawnDemoTradeTerminal();
    FString GetEconomyPackagePath() const;
    void SetEconomyStatus(const FString& Message, const FColor& Color);

    // Loaded package data — stored here then spawned in OnStart()
    // once the world is ready.
    TArray<FDwmBlock>                    PendingBlocks;
    TMap<FString, FDwmAssetBinding>      PendingBindings;
    TMap<FString, TArray<FDwmSimSample>> PendingSamples;
    /** A package has been read and its blocks are waiting for their host map. NOT
        cleared by a spawn: it stays set for the life of the loaded package so that
        leaving the host map and returning spawns the blocks again. SpawnedIntoWorld,
        not this flag, is what stops a second spawn inside one loaded world. */
    bool bHasPendingSpawn = false;
    /** The world SpawnWorldActors last spawned into. Weak so a torn-down level is
        never mistaken for the current one -- a stale pointer reads as null, which
        correctly means "this world has not been spawned into yet". */
    TWeakObjectPtr<UWorld> SpawnedIntoWorld;

    // Actors SpawnWorldActors actually created, keyed by BlockId. Weak because the level
    // (and therefore these actors) can be torn down by an OpenLevel the GameInstance
    // survives; a raw or strong pointer here would either dangle or needlessly keep a
    // destroyed actor alive. Populated once per LoadDwmWorld/SpawnWorldActors cycle.
    TMap<FString, TWeakObjectPtr<ADwmPendulumActor>> SpawnedBlockActors;
    bool bTurbineRotorStarted = false;
    bool bDemoTradeTerminalSpawned = false;
    int32 DemoTradeTerminalSpawnAttempts = 0;
    int32 CharacterCustomizerInputAttempts = 0;
    FTimerHandle CharacterCustomizerInputTimer;
    FTimerHandle CharacterCreatorTransitionTimer;
    FTimerHandle TransitionCharacterDisplayTimer;
    FTimerHandle TransitionArrivalTimer;
    TWeakObjectPtr<UButton> CharacterCreatorConfirmButton;
    TWeakObjectPtr<AActor> TransitionCharacterDisplay;
    TWeakObjectPtr<UWorld> LastInitializedWorld;
    FDelegateHandle PostLoadMapHandle;
    bool bCharacterCreatorTransitionQueued = false;
    /** True when Confirm's live Local Data was sufficient to resolve the exact outfit.
        In that case the delayed SaveGame refresh must not replace it with an older record. */
    bool bLiveAppearanceAssetsCaptured = false;
    int32 TransitionCharacterDisplayAttempts = 0;
    int32 TransitionArrivalAttempts = 0;

    /** Session-only Hank conversation state. The GameInstance survives community-map loads. */
    UPROPERTY(Transient)
    bool bHankOpeningDelivered = false;

    UPROPERTY(Transient)
    bool bHankFarewellUnlocked = false;

    UPROPERTY(Transient)
    int32 HankReturnVisitCount = 0;

    UPROPERTY(Transient)
    int32 HankAmbientCursor = 0;

    /** GameInstance survives the pack's intermediate transition map, so these names let
        each destination select its route-specific arrival point instead of the asset's
        random spawn from a pooled list. */
    FName LastCommunityMapName;
    FName PendingArrivalSourceMapName;

    /** A transient one-entry copy of the live CharacterCustomizer data.  Keeping it as a
        reflected UObject preserves the data table/material references stored inside the
        marketplace's Blueprint struct while OpenLevel replaces the current world. */
    UPROPERTY(Transient)
    TObjectPtr<USaveGame> SelectedCharacterSnapshot;

    /** Exact visible body/apparel component snapshot used by the 3D transition display. */
    UPROPERTY(Transient)
    TArray<FDwmCapturedCharacterMesh> CapturedCharacterMeshes;
};
