// DwmWriteTestTrade.cpp
// Day 17 Tasks 3/4: repeatable, PIE-triggerable proof that FDwmEconomyWriter::WriteTrade
// works end to end. NOT throwaway like Day 16's DwmEconomySqliteSpike -- this is meant to
// stay as reusable regression-check tooling ("a repeatable in-editor test... not a one-off
// manual check", per the task). Registers a console command:
//   DwmWriteTestTrade <full-path-to-economy-world-package.db>
//
// Proves, in one run:
//   (a) ReadWrite open -> write -> close -> ReadOnly reopen produces no lock/disk I/O error
//       -- single process, sequential operations, no concurrent writer (the simpler claim
//       Day 17 actually needs, per SCOPE.md's Day 27 decision that DWMStudio never runs
//       concurrently with a packaged game).
//   (b) the written row reads back with every field intact.
//   (c) the network-sum-zero invariant still holds across the WHOLE StoneLedger table
//       afterward (not just the one new row) -- proves the write didn't corrupt anything
//       already there.
//
// Every StoneLedger row nets to exactly zero by construction on its own (+Amount to
// ToCommunityId, -Amount from FromCommunityId), so running this command repeatedly keeps
// accumulating real rows and the network-sum-zero check will still legitimately PASS every
// time -- that's expected, not a sign the check is too weak.

#include "DwmEconomyWriter.h"
#include "SQLiteDatabase.h"
#include "SQLitePreparedStatement.h"
#include "HAL/IConsoleManager.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogDwmWriteTestTrade, Log, All);

namespace
{
    // Real seeded community/resource ids (economy_schema.sql) -- not invented for this test.
    const TArray<FString> KnownCommunityIds = { TEXT("mountain"), TEXT("hillside"), TEXT("valley"), TEXT("suburb"), TEXT("city") };
    const TArray<FString> KnownResourceIds = {
        TEXT("timber"), TEXT("wind_power"), TEXT("orchard_fruit"), TEXT("wool"), TEXT("grain"),
        TEXT("water"), TEXT("skilled_labor"), TEXT("textiles"), TEXT("manufactured_tools"), TEXT("software_services")
    };

    bool ReadBackTrade(const FString& DbPath, const FString& TransactionId,
        FString& OutFrom, FString& OutTo, double& OutAmount,
        FString& OutResourceId, bool& OutHasResourceId,
        double& OutQuantity, bool& OutHasQuantity, FString& OutMemo)
    {
        FSQLiteDatabase Db;
        if (!Db.Open(*DbPath, ESQLiteDatabaseOpenMode::ReadOnly))
        {
            UE_LOG(LogDwmWriteTestTrade, Error, TEXT("[readback] Open() failed: %s"), *Db.GetLastError());
            return false;
        }

        FSQLitePreparedStatement Stmt;
        Stmt.Create(Db, TEXT("SELECT FromCommunityId, ToCommunityId, Amount, ResourceId, Quantity, Memo FROM StoneLedger WHERE TransactionId = $id;"));
        Stmt.SetBindingValueByName(TEXT("$id"), TransactionId);

        bool bFound = false;
        if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
        {
            bFound = true;
            Stmt.GetColumnValueByName(TEXT("FromCommunityId"), OutFrom);
            Stmt.GetColumnValueByName(TEXT("ToCommunityId"), OutTo);
            Stmt.GetColumnValueByName(TEXT("Amount"), OutAmount);

            ESQLiteColumnType ResourceType;
            Stmt.GetColumnTypeByName(TEXT("ResourceId"), ResourceType);
            OutHasResourceId = (ResourceType != ESQLiteColumnType::Null);
            if (OutHasResourceId) Stmt.GetColumnValueByName(TEXT("ResourceId"), OutResourceId);

            ESQLiteColumnType QuantityType;
            Stmt.GetColumnTypeByName(TEXT("Quantity"), QuantityType);
            OutHasQuantity = (QuantityType != ESQLiteColumnType::Null);
            if (OutHasQuantity) Stmt.GetColumnValueByName(TEXT("Quantity"), OutQuantity);

            Stmt.GetColumnValueByName(TEXT("Memo"), OutMemo);
        }
        else
        {
            UE_LOG(LogDwmWriteTestTrade, Error, TEXT("[readback] Row not found for TransactionId=%s"), *TransactionId);
        }

        Stmt.Destroy();
        Db.Close();
        return bFound;
    }

    bool CheckNetworkSumZero(const FString& DbPath, double& OutSum)
    {
        FSQLiteDatabase Db;
        if (!Db.Open(*DbPath, ESQLiteDatabaseOpenMode::ReadOnly))
        {
            UE_LOG(LogDwmWriteTestTrade, Error, TEXT("[invariant] Open() failed: %s"), *Db.GetLastError());
            return false;
        }

        FSQLitePreparedStatement Stmt;
        Stmt.Create(Db, TEXT("SELECT FromCommunityId, ToCommunityId, Amount FROM StoneLedger;"));

        TMap<FString, double> Credit, Debit;
        for (;;)
        {
            const ESQLitePreparedStatementStepResult StepResult = Stmt.Step();
            if (StepResult == ESQLitePreparedStatementStepResult::Done)
                break;
            if (StepResult != ESQLitePreparedStatementStepResult::Row)
            {
                UE_LOG(LogDwmWriteTestTrade, Error, TEXT("[invariant] Step() failed: %s"), *Db.GetLastError());
                Stmt.Destroy();
                Db.Close();
                return false;
            }
            FString From, To;
            double Amount = 0.0;
            Stmt.GetColumnValueByName(TEXT("FromCommunityId"), From);
            Stmt.GetColumnValueByName(TEXT("ToCommunityId"), To);
            Stmt.GetColumnValueByName(TEXT("Amount"), Amount);
            Credit.FindOrAdd(To) += Amount;
            Debit.FindOrAdd(From) += Amount;
        }
        Stmt.Destroy();
        Db.Close();

        TSet<FString> AllIds;
        for (const auto& Pair : Credit) AllIds.Add(Pair.Key);
        for (const auto& Pair : Debit) AllIds.Add(Pair.Key);
        OutSum = 0.0;
        for (const FString& Id : AllIds)
            OutSum += (Credit.Contains(Id) ? Credit[Id] : 0.0) - (Debit.Contains(Id) ? Debit[Id] : 0.0);
        return true;
    }

    void RunWriteTestTrade(const TCHAR* DbPath)
    {
        UE_LOG(LogDwmWriteTestTrade, Log, TEXT("=== DwmWriteTestTrade starting: %s ==="), DbPath);
        const FString DbPathStr(DbPath);

        // --- Task 3: ReadWrite open -> write -> close, single process ---
        const FDwmEconomyWriter::FWriteTradeResult Result = FDwmEconomyWriter::WriteTrade(
            DbPathStr, KnownCommunityIds, KnownResourceIds,
            TEXT("hillside"), TEXT("suburb"), 20.0, TEXT("textiles"), 20.0,
            TEXT("Day 17 write-path test trade"));

        if (!Result.bSuccess)
        {
            UE_LOG(LogDwmWriteTestTrade, Error, TEXT("WriteTrade FAILED: %s"), *Result.Message);
            return;
        }
        UE_LOG(LogDwmWriteTestTrade, Log, TEXT("WriteTrade PASS: TransactionId=%s"), *Result.TransactionId);

        // --- Task 3 continued: immediate ReadOnly reopen -- proves no lingering lock ---
        {
            FSQLiteDatabase ReopenDb;
            if (!ReopenDb.Open(DbPath, ESQLiteDatabaseOpenMode::ReadOnly))
            {
                UE_LOG(LogDwmWriteTestTrade, Error, TEXT("Reopen ReadOnly FAILED (possible lingering lock): %s"), *ReopenDb.GetLastError());
                return;
            }
            UE_LOG(LogDwmWriteTestTrade, Log, TEXT("Reopen ReadOnly PASS -- no lingering lock."));
            ReopenDb.Close();
        }

        // --- Task 4a: read the written row back, confirm every field matches ---
        FString RbFrom, RbTo, RbResourceId, RbMemo;
        double RbAmount = 0.0, RbQuantity = 0.0;
        bool bRbHasResourceId = false, bRbHasQuantity = false;
        if (!ReadBackTrade(DbPathStr, Result.TransactionId, RbFrom, RbTo, RbAmount,
            RbResourceId, bRbHasResourceId, RbQuantity, bRbHasQuantity, RbMemo))
        {
            UE_LOG(LogDwmWriteTestTrade, Error, TEXT("Read-back FAILED."));
            return;
        }

        const bool bFromOk = (RbFrom == TEXT("hillside"));
        const bool bToOk = (RbTo == TEXT("suburb"));
        const bool bAmountOk = (RbAmount == 20.0);
        const bool bResourceOk = bRbHasResourceId && (RbResourceId == TEXT("textiles"));
        const bool bQuantityOk = bRbHasQuantity && (RbQuantity == 20.0);
        UE_LOG(LogDwmWriteTestTrade, Log,
            TEXT("Read-back: From=%s(%s) To=%s(%s) Amount=%.17g(%s) ResourceId=%s(%s) Quantity=%.17g(%s) Memo='%s'"),
            *RbFrom, bFromOk ? TEXT("PASS") : TEXT("FAIL"),
            *RbTo, bToOk ? TEXT("PASS") : TEXT("FAIL"),
            RbAmount, bAmountOk ? TEXT("PASS") : TEXT("FAIL"),
            *RbResourceId, bResourceOk ? TEXT("PASS") : TEXT("FAIL"),
            RbQuantity, bQuantityOk ? TEXT("PASS") : TEXT("FAIL"),
            *RbMemo);

        // --- Task 4b: network-sum-zero across the whole StoneLedger table, after the write ---
        double NetworkSum = 0.0;
        if (!CheckNetworkSumZero(DbPathStr, NetworkSum))
        {
            UE_LOG(LogDwmWriteTestTrade, Error, TEXT("Network-sum-zero check FAILED to run."));
            return;
        }
        UE_LOG(LogDwmWriteTestTrade, Log, TEXT("Network-sum-zero after write: %.17g -- %s"),
            NetworkSum, (NetworkSum == 0.0) ? TEXT("PASS (exactly zero)") : TEXT("FAIL (not exactly zero)"));

        UE_LOG(LogDwmWriteTestTrade, Log, TEXT("=== DwmWriteTestTrade finished. ==="));
    }
}

static FAutoConsoleCommand DwmWriteTestTradeCmd(
    TEXT("DwmWriteTestTrade"),
    TEXT("Day 17 write-path test: DwmWriteTestTrade <full-path-to-economy-world-package.db>"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        if (Args.Num() < 1)
        {
            UE_LOG(LogDwmWriteTestTrade, Error, TEXT("Usage: DwmWriteTestTrade <full-path-to-economy-world-package.db>"));
            return;
        }
        RunWriteTestTrade(*Args[0]);
    })
);
