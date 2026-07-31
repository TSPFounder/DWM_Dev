// DwmDialogueTypes.h
// Shared data types for the MVP's static interaction-panel dialogue.
//
// Per DWM_MVP_Dialogue.md's own format note, this is deliberately NOT a branching
// dialogue tree: each state is a linear sequence of lines the player advances through
// with a single generic prompt. A full branching system (Narrative Tales) is Post-MVP
// (Phase D3) and is not what this builds.

#pragma once

#include "CoreMinimal.h"
#include "DwmDialogueTypes.generated.h"

/**
 * The six dialogue states from DWM_MVP_Dialogue.md's Mountain/Hank section.
 * These map 1:1 onto the doc's own headings -- if the doc gains a state, add it here;
 * don't overload an existing one.
 */
UENUM(BlueprintType)
enum class EDwmDialogueState : uint8
{
    /** First contact. "That turbine came with the land when we settled here..." */
    Approach                UMETA(DisplayName = "Approach"),

    /** After the player prompt "What exactly do we need?" -- the four-item shopping list. */
    QuestDetails            UMETA(DisplayName = "Quest Details"),

    /** Return visit, before all required trades are complete. */
    ReturnInProgress        UMETA(DisplayName = "Return (In Progress)"),

    /** Return visit, after all required trades are complete. */
    ReturnAllTradesComplete UMETA(DisplayName = "Return (All Trades Complete)"),

    /** After the turbine spins -- the Act 3 payoff line. Gated by an explicit external
        trigger (see ADwmNpcActor::UnlockFarewell), never by trade state alone, because
        the turbine beat is a separate Week 7 mechanism. */
    Farewell                UMETA(DisplayName = "Farewell"),

    /** Optional flavor on repeat visits. Cycles rather than repeating line 1 forever. */
    Ambient                 UMETA(DisplayName = "Ambient")
};

/**
 * One panel's worth of text. Speaker is carried per-line rather than per-NPC so a future
 * multi-NPC stop (Hillside's Reya/Owen/Lena, City's Mike/Kai) can reuse this struct
 * unchanged -- see DWM_MVP_Dialogue.md open items 3 and 5.
 */
USTRUCT(BlueprintType)
struct FDwmDialogueLine
{
    GENERATED_BODY()

    /** Display name shown above the body text, e.g. "Hank". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Dialogue")
    FText Speaker;

    /** The line itself. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Dialogue", meta = (MultiLine = "true"))
    FText Body;

    /** Text on the advance button. Leave empty for the default ("Continue").
        The doc's one real player prompt -- "What exactly do we need?" -- is expressed
        here, on the Approach line that it follows. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Dialogue")
    FText AdvancePrompt;
};

/**
 * The ordered lines for a single state. Wrapped in a struct (rather than using
 * TMap<EDwmDialogueState, TArray<...>> directly) because nested containers don't
 * edit cleanly in the Details panel.
 */
USTRUCT(BlueprintType)
struct FDwmDialogueSequence
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Dialogue")
    TArray<FDwmDialogueLine> Lines;
};
