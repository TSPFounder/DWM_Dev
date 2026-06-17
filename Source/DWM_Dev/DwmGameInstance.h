// DwmGameInstance.h
// GameInstance that handles dwmworld:// deep links passed on the command line.
// When DWM_Dev is launched via a dwmworld://launch?id=... URL (registered as a
// Windows URI scheme), the full URL arrives as a command-line argument. This
// class extracts it, parses the world ID, and routes into world loading.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "DwmGameInstance.generated.h"

UCLASS()
class DWM_DEV_API UDwmGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    virtual void Init() override;

    /** Parse a dwmworld:// URL and act on it. Public so it can be called
        from tests or a runtime handler if the app is already running. */
    UFUNCTION(BlueprintCallable, Category = "DWM")
    void HandleDwmUrl(const FString& Url);

    /** The world ID extracted from the launch URL, if any. */
    UPROPERTY(BlueprintReadOnly, Category = "DWM")
    FString PendingWorldId;

private:
    /** Pull the dwmworld:// token out of the raw command line, if present. */
    bool TryGetLaunchUrl(FString& OutUrl) const;

    /** Load the world identified by WorldId (stub for Phase 3). */
    void LoadDwmWorld(const FString& WorldId);
};
