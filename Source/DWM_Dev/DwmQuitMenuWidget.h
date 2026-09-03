// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DwmQuitMenuWidget.generated.h"

class UButton;
class UTextBlock;

/** Escape menu: a confirmation panel with Resume and Quit.
 *
 *  Built entirely in C++ like UDwmDialogueWidget, so it needs no Blueprint subclass
 *  and cannot be lost by a content re-save. Quitting is confirmed rather than
 *  immediate, because Escape is easy to hit by accident and there is no save to
 *  come back to -- a session's trades live only in the ledger, and the narrative
 *  progress not at all.
 */
UCLASS()
class DWM_DEV_API UDwmQuitMenuWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UDwmQuitMenuWidget(const FObjectInitializer& ObjectInitializer);

    virtual void NativeConstruct() override;

    /** The tree must exist BEFORE Slate builds from it.

        Constructing the layout in NativeConstruct is too late -- that runs after the
        Slate widget has already been made from an empty tree, so the panel is created
        and never drawn: the game pauses, the cursor appears, and the screen shows
        nothing. UDwmDialogueWidget survives the same code only because it is created
        from a Blueprint class whose tree is already populated. */
    virtual TSharedRef<SWidget> RebuildWidget() override;

protected:
    UPROPERTY(Transient)
    UButton* ResumeButton = nullptr;

    UPROPERTY(Transient)
    UButton* QuitButton = nullptr;

    UPROPERTY(Transient)
    UTextBlock* TitleText = nullptr;

private:
    void BuildLayout();

    UFUNCTION()
    void HandleResumeClicked();

    UFUNCTION()
    void HandleQuitClicked();
};
