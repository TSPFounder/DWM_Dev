#include "DwmEconomyHud.h"

#include "DwmGameInstance.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

void ADwmEconomyHud::DrawHUD()
{
    Super::DrawHUD();

    if (!Canvas || !GEngine)
    {
        return;
    }

    const UDwmGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance<UDwmGameInstance>() : nullptr;
    if (!GameInstance)
    {
        return;
    }

    constexpr float Margin = 24.0f;
    constexpr float LineHeight = 24.0f;
    const float PanelHeight = 74.0f + (LineHeight * GameInstance->EconomyStates.Num());
    DrawRect(FLinearColor(0.02f, 0.04f, 0.08f, 0.75f), Margin - 12.0f, Margin - 12.0f, 480.0f, PanelHeight);

    DrawText(TEXT("DWM Economy"), FLinearColor(0.95f, 0.8f, 0.2f), Margin, Margin,
        GEngine->GetMediumFont(), 1.1f, false);

    float Y = Margin + 30.0f;
    for (const FDwmCommunityEconomyState& State : GameInstance->EconomyStates)
    {
        const FLinearColor StateColor = State.FailureState == TEXT("CascadingFailure")
            ? FLinearColor(1.0f, 0.25f, 0.2f)
            : FLinearColor::White;
        DrawText(FString::Printf(TEXT("%s: %.0f St | Vault $%.0f"),
            *State.CommunityName, State.StoneBalance, State.DollarVaultBalance),
            StateColor, Margin, Y, GEngine->GetSmallFont(), 1.0f, false);
        Y += LineHeight;
    }

    DrawText(GameInstance->LastEconomyStatusMessage, FLinearColor(0.55f, 0.9f, 1.0f),
        Margin, Y + 4.0f, GEngine->GetSmallFont(), 0.9f, false);
}
