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

    UFUNCTION(BlueprintCallable, Category = "DWM|Economy")
    bool ExecuteMountainBuysGrainFromValley();

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
