#include "DwmDialogueWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "DwmNpcActor.h"
#include "Styling/CoreStyle.h"

bool UDwmDialogueWidget::Initialize()
{
    if (!Super::Initialize())
    {
        return false;
    }

    if (!WidgetTree->RootWidget)
    {
        BuildFallbackLayout();
    }

    if (AdvanceButton)
    {
        AdvanceButton->OnClicked.AddUniqueDynamic(this, &UDwmDialogueWidget::RequestAdvance);
    }

    return true;
}

void UDwmDialogueWidget::BuildFallbackLayout()
{
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("DialogueRoot"));
    WidgetTree->RootWidget = Root;

    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DialoguePanel"));
    Panel->SetBrushColor(FLinearColor(0.015f, 0.025f, 0.045f, 0.94f));
    Panel->SetPadding(FMargin(28.0f, 20.0f));
    Root->AddChild(Panel);

    if (UCanvasPanelSlot* PanelSlot = Cast<UCanvasPanelSlot>(Panel->Slot))
    {
        PanelSlot->SetAnchors(FAnchors(0.5f, 1.0f));
        PanelSlot->SetAlignment(FVector2D(0.5f, 1.0f));
        PanelSlot->SetPosition(FVector2D(0.0f, -42.0f));

        // GROW TO FIT THE TEXT rather than a fixed 900x250 (issue #42).
        //
        // The height was the problem: the body wraps, so a long line ran past the
        // bottom of the background and sat on the scene. A fixed box can only ever be
        // right for one length of dialogue, and these lines run from one sentence to
        // four. Auto-size makes the panel follow its content, so the background covers
        // the text by construction rather than by a number that is usually big enough.
        //
        // The issue wondered about splitting the text into separate instances; that is
        // not needed -- the layout can simply stop being fixed.
        PanelSlot->SetAutoSize(true);
    }

    // The panel auto-sizes, so WIDTH has to be constrained or a long line would stretch
    // the background across the screen instead of wrapping. UTextBlock has no
    // SetWrapTextAt in 5.3 -- only SetAutoWrapText -- so the limit comes from a SizeBox
    // around the column. 844 is the old fixed 900 minus the panel's 28-a-side padding,
    // so the dialogue keeps exactly the width it always had.
    USizeBox* WidthLimit = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DialogueWidth"));
    WidthLimit->SetWidthOverride(844.0f);
    Panel->SetContent(WidthLimit);

    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DialogueColumn"));
    WidthLimit->SetContent(Column);

    SpeakerText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SpeakerText"));
    SpeakerText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.72f, 0.20f, 1.0f)));
    SpeakerText->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 28));
    SpeakerText->SetAutoWrapText(true);
    Column->AddChildToVerticalBox(SpeakerText);

    BodyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BodyText"));
    BodyText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    BodyText->SetAutoWrapText(true);
    BodyText->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 23));
    if (UVerticalBoxSlot* BodySlot = Column->AddChildToVerticalBox(BodyText))
    {
        BodySlot->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 14.0f));
    }

    AdvanceButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("AdvanceButton"));
    AdvanceButton->SetBackgroundColor(FLinearColor(0.12f, 0.22f, 0.38f, 1.0f));
    Column->AddChildToVerticalBox(AdvanceButton);

    AdvanceText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AdvanceText"));
    AdvanceText->SetJustification(ETextJustify::Center);
    AdvanceText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    AdvanceText->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 18));
    AdvanceButton->SetContent(AdvanceText);
}

void UDwmDialogueWidget::ShowLine(const FDwmDialogueLine& Line, bool bInHasNext)
{
    CurrentLine = Line;
    bHasNextLine = bInHasNext;

    if (SpeakerText)
    {
        SpeakerText->SetText(Line.Speaker);
    }

    if (BodyText)
    {
        BodyText->SetText(Line.Body);
    }

    if (AdvanceText)
    {
        AdvanceText->SetText(
            Line.AdvancePrompt.IsEmpty()
                ? (bHasNextLine ? FText::FromString(TEXT("Continue  [E]")) : FText::FromString(TEXT("Done  [E]")))
                : Line.AdvancePrompt);
    }

    OnLineChanged();
}

void UDwmDialogueWidget::RequestAdvance()
{
    // Routed back through the NPC rather than handled here: the NPC owns the state
    // machine (which line, which state, whether the opening has been delivered), and
    // keeping that in one place means the E-key path and the button path can't diverge.
    if (OwningNpc)
    {
        OwningNpc->AdvanceDialogue();
    }
}
