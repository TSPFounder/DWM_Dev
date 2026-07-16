// Day 18: lightweight runtime economy display backed by UDwmGameInstance's SQLite snapshot.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DwmEconomyHud.generated.h"

UCLASS()
class DWM_DEV_API ADwmEconomyHud : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
};
