// DwmEconomyWriter.h
// Day 17: writes a single Stone-ledger trade directly into the economy world-package .db,
// in C++, matching DwmGameInstance.cpp's established FSQLiteDatabase conventions exactly
// (Stmt.Create(Db, sql, flags) rather than Db.PrepareStatement(...), by-name binding via
// TEXT("$col"), checked results, GetLastError() on failure) -- opened ReadWrite for this one
// operation only, the existing reader path elsewhere stays ReadOnly and untouched.
//
// Per SCOPE.md's Day 27 decision: this deliberately DUPLICATES TradeSettlementService's five
// structural checks in C++ rather than routing through DWM.Shared/C# -- DWMStudio is an
// authoring tool, it does not run on a player's machine once the game is packaged, so there
// is no live C# service to call at runtime. Not a gap; an intentional architecture choice.
//
// KnownCommunityIds/KnownResourceIds are passed in by the CALLER rather than pulled from some
// in-memory economy cache -- no such cache exists yet anywhere in this codebase (LoadDwmWorld
// only caches pendulum/mechanism data: Blocks/Bindings/Samples, nothing about
// Communities/Resources). Inventing an economy-data cache subsystem is out of this task's
// scope; Day 27's real trade panel will already have this data loaded for its own UI (to
// populate seller/resource dropdowns) and can pass it straight through here.

#pragma once

#include "CoreMinimal.h"

class FDwmEconomyWriter
{
public:
    enum class EWriteTradeFailureReason : uint8
    {
        None,
        NonPositiveAmount,
        SelfTrade,
        UnknownFromCommunity,
        UnknownToCommunity,
        UnknownResource,
        DatabaseOpenFailed,
        DatabaseWriteFailed,
    };

    struct FWriteTradeResult
    {
        bool bSuccess = false;
        EWriteTradeFailureReason FailureReason = EWriteTradeFailureReason::None;
        FString Message;
        FString TransactionId;
    };

    /**
     * Validates and writes a single StoneLedger row. Opens DbPath ReadWrite for the duration
     * of this call only, then closes it -- does not hold the handle open across calls.
     *
     * @param ResourceId  Empty FString = NULL (a Stone-only trade with no attached resource).
     * @param Quantity    Unset TOptional = NULL.
     * @param Memo        Empty FString = NULL.
     */
    static FWriteTradeResult WriteTrade(
        const FString& DbPath,
        const TArray<FString>& KnownCommunityIds,
        const TArray<FString>& KnownResourceIds,
        const FString& FromCommunityId,
        const FString& ToCommunityId,
        double Amount,
        const FString& ResourceId,
        const TOptional<double>& Quantity,
        const FString& Memo);
};
