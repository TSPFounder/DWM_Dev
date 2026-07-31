// DwmDialogueWidget.h
// C++ base for the static interaction panel. It creates a usable native fallback layout;
// a Blueprint subclass can replace that layout for art direction without changing code.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DwmDialogueTypes.h"
#include "DwmDialogueWidget.generated.h"

class ADwmNpcActor;
class UButton;
class UTextBlock;

UCLASS()
class DWM_DEV_API UDwmDialogueWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual bool Initialize() override;

    /** Called by the NPC each time a new line should appear.
        bHasNext is true when advancing shows more dialogue, false when advancing closes
        the panel -- use it to swap the button label between "Continue" and "Done". */
    void ShowLine(const FDwmDialogueLine& Line, bool bHasNext);

    void SetOwningNpc(ADwmNpcActor* Npc) { OwningNpc = Npc; }

    /** Bind the panel's advance button to this. Also reachable by pressing E, which
        routes through ADWM_DevCharacter::Interact -- both paths land here. */
    UFUNCTION(BlueprintCallable, Category = "DWM|Dialogue")
    void RequestAdvance();

    /** The current line's speaker/body/prompt, for binding directly in UMG. */
    UPROPERTY(BlueprintReadOnly, Category = "DWM|Dialogue")
    FDwmDialogueLine CurrentLine;

    /** False when advancing will close the panel rather than show more text. */
    UPROPERTY(BlueprintReadOnly, Category = "DWM|Dialogue")
    bool bHasNextLine = false;

protected:
    /** Implement in the Blueprint subclass: push CurrentLine into the panel's text
        widgets. Fires after CurrentLine/bHasNextLine are already updated. */
    UFUNCTION(BlueprintImplementableEvent, Category = "DWM|Dialogue")
    void OnLineChanged();

private:
    void BuildFallbackLayout();

    UPROPERTY()
    ADwmNpcActor* OwningNpc = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> SpeakerText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> BodyText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> AdvanceText;

    UPROPERTY(Transient)
    TObjectPtr<UButton> AdvanceButton;
};
