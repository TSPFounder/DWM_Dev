// DwmGameInstance.h
// GameInstance that handles dwmworld:// deep links and spawns DWM world actors.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "DwmWorldPackageTypes.h"
#include "DwmGameInstance.generated.h"

UCLASS()
class DWM_DEV_API UDwmGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    virtual void Init() override;
    virtual void OnStart() override;

    UFUNCTION(BlueprintCallable, Category = "DWM")
    void HandleDwmUrl(const FString& Url);

    UPROPERTY(BlueprintReadOnly, Category = "DWM")
    FString PendingWorldId;

private:
    bool TryGetLaunchUrl(FString& OutUrl) const;
    void LoadDwmWorld(const FString& WorldId);
    void SpawnWorldActors();

    // Loaded package data — stored here then spawned in OnStart()
    // once the world is ready.
    TArray<FDwmBlock>                    PendingBlocks;
    TMap<FString, FDwmAssetBinding>      PendingBindings;
    TMap<FString, TArray<FDwmSimSample>> PendingSamples;
    bool bHasPendingSpawn = false;
};