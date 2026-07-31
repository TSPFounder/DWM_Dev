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

UCLASS()
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

    UPROPERTY(BlueprintReadOnly, Category = "DWM")
    FString PendingWorldId;

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
    bool bHasPendingSpawn = false;
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
