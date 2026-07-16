// DwmEconomyWriter.cpp
// See DwmEconomyWriter.h for design notes.
//
// API calls here (Open/Close/GetLastError, Stmt.Create/Execute/SetBindingValueByName) are
// used exactly as CONFIRMED against this project's actual SQLiteDatabase.h/
// SQLitePreparedStatement.h during the Day 16 spike -- not re-guessed. The two things in this
// file NOT verified against project headers: FDateTime::ToIso8601() and FGuid::NewGuid()
// (both Core module, "Misc/DateTime.h" / "Misc/Guid.h" -- long-standing, extremely standard
// UE APIs, but flagging per this task's "don't guess, flag uncertainty" guardrail since they
// weren't in anything pasted and checked so far).

#include "DwmEconomyWriter.h"
#include "SQLiteDatabase.h"
#include "SQLitePreparedStatement.h"
#include "Misc/Guid.h"
#include "Misc/DateTime.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogDwmEconomyWriter, Log, All);

FDwmEconomyWriter::FWriteTradeResult FDwmEconomyWriter::WriteTrade(
    const FString& DbPath,
    const TArray<FString>& KnownCommunityIds,
    const TArray<FString>& KnownResourceIds,
    const FString& FromCommunityId,
    const FString& ToCommunityId,
    double Amount,
    const FString& ResourceId,
    const TOptional<double>& Quantity,
    const FString& Memo)
{
    FWriteTradeResult Result;

    // ------------------------------------------------------------------
    // Task 2: structural validation -- matches TradeSettlementService.SettleTrade's five
    // checks exactly (amount>0, self-trade, unknown from/to community, unknown resource).
    // Deliberately NOT exhaustive beyond that -- economy_schema.sql's own CHECK/FOREIGN KEY
    // constraints remain the backstop, same division of responsibility the C# side uses.
    // ------------------------------------------------------------------
    if (Amount <= 0.0)
    {
        Result.FailureReason = EWriteTradeFailureReason::NonPositiveAmount;
        Result.Message = FString::Printf(TEXT("Amount must be greater than 0 (was %.17g)."), Amount);
        UE_LOG(LogDwmEconomyWriter, Warning, TEXT("WriteTrade rejected: %s"), *Result.Message);
        return Result;
    }
    if (FromCommunityId == ToCommunityId)
    {
        Result.FailureReason = EWriteTradeFailureReason::SelfTrade;
        Result.Message = FString::Printf(TEXT("A community cannot trade with itself ('%s')."), *FromCommunityId);
        UE_LOG(LogDwmEconomyWriter, Warning, TEXT("WriteTrade rejected: %s"), *Result.Message);
        return Result;
    }
    if (!KnownCommunityIds.Contains(FromCommunityId))
    {
        Result.FailureReason = EWriteTradeFailureReason::UnknownFromCommunity;
        Result.Message = FString::Printf(TEXT("No community with id '%s' exists."), *FromCommunityId);
        UE_LOG(LogDwmEconomyWriter, Warning, TEXT("WriteTrade rejected: %s"), *Result.Message);
        return Result;
    }
    if (!KnownCommunityIds.Contains(ToCommunityId))
    {
        Result.FailureReason = EWriteTradeFailureReason::UnknownToCommunity;
        Result.Message = FString::Printf(TEXT("No community with id '%s' exists."), *ToCommunityId);
        UE_LOG(LogDwmEconomyWriter, Warning, TEXT("WriteTrade rejected: %s"), *Result.Message);
        return Result;
    }
    if (!ResourceId.IsEmpty() && !KnownResourceIds.Contains(ResourceId))
    {
        Result.FailureReason = EWriteTradeFailureReason::UnknownResource;
        Result.Message = FString::Printf(TEXT("No resource with id '%s' exists."), *ResourceId);
        UE_LOG(LogDwmEconomyWriter, Warning, TEXT("WriteTrade rejected: %s"), *Result.Message);
        return Result;
    }

    // ------------------------------------------------------------------
    // Task 1: open ReadWrite -- the one write path in this codebase; every reader elsewhere
    // stays ReadOnly and is untouched by this file.
    // ------------------------------------------------------------------
    FSQLiteDatabase Db;
    if (!Db.Open(*DbPath, ESQLiteDatabaseOpenMode::ReadWrite))
    {
        Result.FailureReason = EWriteTradeFailureReason::DatabaseOpenFailed;
        Result.Message = FString::Printf(TEXT("Failed to open '%s' ReadWrite: %s"), *DbPath, *Db.GetLastError());
        UE_LOG(LogDwmEconomyWriter, Error, TEXT("%s"), *Result.Message);
        return Result;
    }

    const FString TransactionId = FGuid::NewGuid().ToString();
    // Bound as TEXT in ISO-8601 form, matching exactly how the C# side already writes this
    // column (DateTimeOffset.UtcNow.ToString("o")). Binding a raw FDateTime value via
    // SetBindingValueByName's FDateTime overload instead was deliberately avoided -- that
    // risks a different on-disk representation than what C#-written rows already use in this
    // same TEXT column, which would make the column inconsistently formatted depending on
    // which side wrote a given row.
    const FString Timestamp = FDateTime::UtcNow().ToIso8601();

    FSQLitePreparedStatement Stmt;
    // Same Stmt.Create(Db, sql, flags) pattern DwmGameInstance.cpp already uses for its own
    // reads (not Db.PrepareStatement(...)) -- matching the established convention exactly,
    // per this task's mandatory-first-step instruction.
    if (!Stmt.Create(Db,
        TEXT("INSERT INTO StoneLedger (TransactionId, Timestamp, FromCommunityId, ToCommunityId, Amount, ResourceId, Quantity, Memo) ")
        TEXT("VALUES ($id, $ts, $from, $to, $amount, $resource, $qty, $memo);"),
        ESQLitePreparedStatementFlags::None))
    {
        Result.FailureReason = EWriteTradeFailureReason::DatabaseWriteFailed;
        Result.Message = FString::Printf(TEXT("Failed to prepare INSERT: %s"), *Db.GetLastError());
        UE_LOG(LogDwmEconomyWriter, Error, TEXT("%s"), *Result.Message);
        Db.Close();
        return Result;
    }

    bool bBindOk = true;
    bBindOk &= Stmt.SetBindingValueByName(TEXT("$id"), TransactionId);
    bBindOk &= Stmt.SetBindingValueByName(TEXT("$ts"), Timestamp);
    bBindOk &= Stmt.SetBindingValueByName(TEXT("$from"), FromCommunityId);
    bBindOk &= Stmt.SetBindingValueByName(TEXT("$to"), ToCommunityId);
    bBindOk &= Stmt.SetBindingValueByName(TEXT("$amount"), Amount);
    if (ResourceId.IsEmpty())
        bBindOk &= Stmt.SetBindingValueByName(TEXT("$resource"));
    else
        bBindOk &= Stmt.SetBindingValueByName(TEXT("$resource"), ResourceId);
    if (Quantity.IsSet())
        bBindOk &= Stmt.SetBindingValueByName(TEXT("$qty"), Quantity.GetValue());
    else
        bBindOk &= Stmt.SetBindingValueByName(TEXT("$qty"));
    if (Memo.IsEmpty())
        bBindOk &= Stmt.SetBindingValueByName(TEXT("$memo"));
    else
        bBindOk &= Stmt.SetBindingValueByName(TEXT("$memo"), Memo);

    if (!bBindOk)
    {
        Result.FailureReason = EWriteTradeFailureReason::DatabaseWriteFailed;
        Result.Message = FString::Printf(TEXT("Failed to bind INSERT parameters: %s"), *Db.GetLastError());
        UE_LOG(LogDwmEconomyWriter, Error, TEXT("%s"), *Result.Message);
        Stmt.Destroy();
        Db.Close();
        return Result;
    }

    if (!Stmt.Execute())
    {
        Result.FailureReason = EWriteTradeFailureReason::DatabaseWriteFailed;
        Result.Message = FString::Printf(TEXT("INSERT execution failed: %s"), *Db.GetLastError());
        UE_LOG(LogDwmEconomyWriter, Error, TEXT("%s"), *Result.Message);
        Stmt.Destroy();
        Db.Close();
        return Result;
    }

    Stmt.Destroy();
    if (!Db.Close())
    {
        // Not treated as a write failure -- the INSERT itself already committed (SQLite
        // commits happen as part of a successful Execute() of a non-transactional single
        // statement, not deferred to Close()). Logged as a warning since a Close() failure
        // is still worth knowing about even though the data is safe.
        UE_LOG(LogDwmEconomyWriter, Warning, TEXT("Close() after write reported an error: %s"), *Db.GetLastError());
    }

    Result.bSuccess = true;
    Result.TransactionId = TransactionId;
    UE_LOG(LogDwmEconomyWriter, Log, TEXT("WriteTrade succeeded: TransactionId=%s From=%s To=%s Amount=%.17g"),
        *TransactionId, *FromCommunityId, *ToCommunityId, Amount);
    return Result;
}
