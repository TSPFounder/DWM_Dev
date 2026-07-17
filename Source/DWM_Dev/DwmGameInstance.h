// DwmGameInstance.h
// GameInstance that handles dwmworld:// deep links and spawns DWM world actors.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "DwmWorldPackageTypes.h"
#include "DwmGameInstance.generated.h"

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

    UPROPERTY(BlueprintReadOnly, Category = "DWM")
    FString PendingWorldId;

    UPROPERTY(BlueprintReadOnly, Category = "DWM|Economy")
    TArray<FDwmCommunityEconomyState> EconomyStates;

    UPROPERTY(BlueprintReadOnly, Category = "DWM|Economy")
    FString LastEconomyStatusMessage;

private:
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
};
