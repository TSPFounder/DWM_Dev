// Copyright Epic Games, Inc. All Rights Reserved.

#include "DwmQuitMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "DwmQuitMenu"

UDwmQuitMenuWidget::UDwmQuitMenuWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // The game is paused behind this, so the menu itself must keep ticking and
    // accepting clicks while paused.
    SetIsFocusable(true);
}

TSharedRef<SWidget> UDwmQuitMenuWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildLayout();
    }

    return Super::RebuildWidget();
}

void UDwmQuitMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (ResumeButton)
    {
        ResumeButton->OnClicked.AddUniqueDynamic(this, &UDwmQuitMenuWidget::HandleResumeClicked);
    }

    if (QuitButton)
    {
        QuitButton->OnClicked.AddUniqueDynamic(this, &UDwmQuitMenuWidget::HandleQuitClicked);
    }
}

void UDwmQuitMenuWidget::BuildLayout()
{
    // Same construction approach and palette as the dialogue panel, so this reads as
    // part of the same game rather than as engine default UI.
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
        UCanvasPanel::StaticClass(), TEXT("QuitRoot"));
    WidgetTree->RootWidget = Root;

    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("QuitPanel"));
    Panel->SetBrushColor(FLinearColor(0.015f, 0.025f, 0.045f, 0.94f));
    Panel->SetPadding(FMargin(36.0f, 28.0f));
    Root->AddChild(Panel);

    if (UCanvasPanelSlot* PanelSlot = Cast<UCanvasPanelSlot>(Panel->Slot))
    {
        // Centred, and sized to its content like the dialogue panel -- a fixed box is
        // only ever right for one string.
        PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
        PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
        PanelSlot->SetPosition(FVector2D(0.0f, 0.0f));
        PanelSlot->SetAutoSize(true);
    }

    UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("QuitColumn"));
    Panel->SetContent(Column);

    TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("QuitTitle"));
    TitleText->SetText(LOCTEXT("QuitTitle", "Leave Dream World Maker?"));
    TitleText->SetJustification(ETextJustify::Center);
    TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.72f, 0.20f, 1.0f)));
    TitleText->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 28));
    Column->AddChildToVerticalBox(TitleText);

    UTextBlock* Note = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("QuitNote"));
    // Say what is actually lost. Trades are written to the ledger and survive; the
    // conversation progress that gates the turbine lives in the GameInstance and does
    // not, so quitting mid-errand means starting the four trades over.
    Note->SetText(LOCTEXT("QuitNote",
        "Trades you have made are kept. Progress through Hank's errand is not -- "
        "leaving now means starting his four trades again."));
    Note->SetJustification(ETextJustify::Center);
    Note->SetAutoWrapText(true);
    Note->SetColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.84f, 0.88f, 1.0f)));
    Note->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 17));
    if (UVerticalBoxSlot* NoteSlot = Column->AddChildToVerticalBox(Note))
    {
        NoteSlot->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 22.0f));
    }

    UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("QuitButtons"));
    Column->AddChildToVerticalBox(Buttons);

    // Resume first and in the calmer colour: the safe choice should be the easy one.
    ResumeButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ResumeButton"));
    ResumeButton->SetBackgroundColor(FLinearColor(0.12f, 0.22f, 0.38f, 1.0f));
    if (UHorizontalBoxSlot* ResumeSlot = Buttons->AddChildToHorizontalBox(ResumeButton))
    {
        ResumeSlot->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
        ResumeSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    UTextBlock* ResumeLabel = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("ResumeLabel"));
    ResumeLabel->SetText(LOCTEXT("QuitResume", "Keep Playing"));
    ResumeLabel->SetJustification(ETextJustify::Center);
    ResumeLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    ResumeLabel->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 18));
    ResumeButton->SetContent(ResumeLabel);

    QuitButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("QuitButton"));
    QuitButton->SetBackgroundColor(FLinearColor(0.42f, 0.13f, 0.13f, 1.0f));
    if (UHorizontalBoxSlot* QuitSlot = Buttons->AddChildToHorizontalBox(QuitButton))
    {
        QuitSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    UTextBlock* QuitLabel = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("QuitLabel"));
    QuitLabel->SetText(LOCTEXT("QuitConfirm", "Quit to Desktop"));
    QuitLabel->SetJustification(ETextJustify::Center);
    QuitLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    QuitLabel->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 18));
    QuitButton->SetContent(QuitLabel);
}

void UDwmQuitMenuWidget::HandleResumeClicked()
{
    APlayerController* Controller = GetOwningPlayer();

    RemoveFromParent();

    if (Controller)
    {
        UGameplayStatics::SetGamePaused(this, false);
        Controller->SetInputMode(FInputModeGameOnly());
        Controller->SetShowMouseCursor(false);
    }
}

void UDwmQuitMenuWidget::HandleQuitClicked()
{
    // Unpause first: Quit with Quit Preference "Quit" tears the process down anyway,
    // but leaving a paused world behind on the way out has bitten other projects, and
    // it costs nothing to leave the world running as it closes.
    UGameplayStatics::SetGamePaused(this, false);

    UE_LOG(LogTemp, Log, TEXT("[DWM] Quit confirmed from the escape menu."));

    UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit,
        /*bIgnorePlatformRestrictions=*/false);
}

#undef LOCTEXT_NAMESPACE
