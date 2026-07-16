// DwmEconomySqliteSpike.cpp
// ============================================================================
// THROWAWAY SPIKE -- Day 16. DELETE THIS FILE (and nothing else) when done.
// ============================================================================
// Registers a console command: DwmEconomySqliteSpike <full-path-to-exported-.db>
// Run it from the in-editor console (PIE or editor, either should work since this
// doesn't touch GetWorld()) once the exported world-package .db is somewhere on disk.
//
// Verifies, against a REAL exported economy snapshot (see chat for how it was produced):
//   2. CommunityDollarVault Balance/Threshold (REAL) read back with full precision, compared
//      against known DWMStudio-side ground-truth values.
//   3. StoneLedger null-vs-non-null ResourceId/Quantity/Memo handling, explicit null check
//      named below.
//   4. Network-sum-zero invariant recomputed independently on the C++ side.
//   5. Every distinct CommunityResources.Role / CommunityFailureStatus.State literal seen.
//   6. Close-then-reopen with no lingering lock.
//   7. Every FSQLiteDatabase/FSQLitePreparedStatement call's result checked; GetLastError()
//      logged on any failure.
//
// VERIFIED against this project's actual headers (SQLiteDatabase.h, SQLitePreparedStatement.h,
// SQLiteTypes.h, pasted and checked line-by-line): Open/Close/GetLastError/PrepareStatement,
// Step()/ESQLitePreparedStatementStepResult (Error/Busy/Row/Done),
// SetBindingValueByName/GetColumnValueByName for double/FString/int64, and
// GetColumnTypeByName(name, ESQLiteColumnType&)/ESQLiteColumnType::Null for null-checking.
// One real bug was caught and fixed during this verification: the first draft treated
// GetColumnTypeByName as returning ESQLiteColumnType directly -- it actually returns bool and
// writes into an out-parameter. See IsColumnNullByName's comment below.
//
// The ONE thing still unverified: FAutoConsoleCommand/FConsoleCommandWithArgsDelegate
// (HAL/IConsoleManager.h) is standard engine-wide UE5 API, not part of SQLiteCore, so it
// wasn't checked against project headers -- low risk, but worth knowing if it doesn't compile.
//
// Console-command argument parsing note: if the .db path contains spaces, quote it when typing
// the command in-console (e.g. DwmEconomySqliteSpike "C:\Some Path\world.db"); unquoted
// whitespace will split into multiple Args.

#include "SQLiteDatabase.h"
#include "SQLitePreparedStatement.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogDwmEconomySpike, Log, All);

namespace
{
    // Ground truth from the DWMStudio/C# side for THIS specific exported .db -- printed to
    // console when the file was generated; paste your own values here if you regenerate it.
    struct FKnownVault { const TCHAR* CommunityId; double Balance; double Threshold; const TCHAR* State; };
    const FKnownVault KnownVaults[] = {
        { TEXT("mountain"), 4200.0, 500.0, TEXT("Healthy") },
        { TEXT("hillside"), 4400.0, 500.0, TEXT("Healthy") },
        { TEXT("valley"),   4600.0, 500.0, TEXT("Healthy") },
        { TEXT("suburb"),   4000.0, 500.0, TEXT("Healthy") },
        { TEXT("city"),      400.0, 500.0, TEXT("CascadingFailure") },
    };

    bool CheckStep(FSQLitePreparedStatement& Stmt, FSQLiteDatabase& Db, const TCHAR* Context, ESQLitePreparedStatementStepResult& OutResult)
    {
        // CONFIRMED against SQLitePreparedStatement.h: Error/Busy/Row/Done.
        OutResult = Stmt.Step();
        if (OutResult == ESQLitePreparedStatementStepResult::Error)
        {
            UE_LOG(LogDwmEconomySpike, Error, TEXT("[%s] Step() failed: %s"), Context, *Db.GetLastError());
            return false;
        }
        if (OutResult == ESQLitePreparedStatementStepResult::Busy)
        {
            // Not expected for a single ReadOnly connection with no concurrent writer -- this
            // spike has no retry/backoff logic, so treat it as a hard stop rather than an
            // infinite loop if it ever somehow happens.
            UE_LOG(LogDwmEconomySpike, Error, TEXT("[%s] Step() returned Busy (unexpected for ReadOnly, no retry logic in this spike)."), Context);
            return false;
        }
        return true;
    }

    // CONFIRMED against SQLitePreparedStatement.h: GetColumnTypeByName returns bool and writes
    // into an OUT parameter (ESQLiteColumnType& OutColumnType) -- it does NOT return the type
    // directly. My first draft got this wrong (wrote it as a direct-return comparison, which
    // doesn't even compile) -- caught during header verification, fixed here.
    bool IsColumnNullByName(const FSQLitePreparedStatement& Stmt, FSQLiteDatabase& Db, const TCHAR* ColumnName, bool& OutIsNull)
    {
        ESQLiteColumnType ColumnType;
        if (!Stmt.GetColumnTypeByName(ColumnName, ColumnType))
        {
            UE_LOG(LogDwmEconomySpike, Error, TEXT("GetColumnTypeByName('%s') failed: %s"), ColumnName, *Db.GetLastError());
            return false;
        }
        // CONFIRMED against SQLiteTypes.h: "Null" is the exact enumerator name.
        OutIsNull = (ColumnType == ESQLiteColumnType::Null);
        return true;
    }

    // Steps 1-5. Deliberately does NOT own Db's lifecycle -- every `return`/`continue` in
    // here is safe to use freely because RunSpike (below) always calls Db.Close() itself
    // afterward, regardless of how this function exits. This split exists specifically to
    // fix a real bug: the original single-function version had several early `return`s
    // after a successful Open() that skipped Close() entirely, which crashes
    // FSQLiteDatabase's destructor (SQLiteDatabase.cpp asserts `!Database` on destruction --
    // it does NOT auto-close for you). Caught via a real assertion failure + debug break
    // when Step 1's "no such table: StoneLedger" (expected -- pendulum.db has no economy
    // tables) triggered exactly this path.
    void RunSpikeSteps(FSQLiteDatabase& Db, const TCHAR* DbPath)
    {
        // ------------------------------------------------------------------
        // Step 1 (partial, re-confirmed here): StoneLedger row count > 0.
        // ------------------------------------------------------------------
        {
            FSQLitePreparedStatement CountStmt = Db.PrepareStatement(TEXT("SELECT COUNT(*) FROM StoneLedger;"));
            if (!CountStmt.IsValid())
            {
                UE_LOG(LogDwmEconomySpike, Error, TEXT("PrepareStatement(COUNT) failed: %s"), *Db.GetLastError());
                return;
            }
            ESQLitePreparedStatementStepResult StepResult;
            if (!CheckStep(CountStmt, Db, TEXT("StoneLedger COUNT"), StepResult) || StepResult != ESQLitePreparedStatementStepResult::Row)
            {
                UE_LOG(LogDwmEconomySpike, Error, TEXT("StoneLedger COUNT query produced no row."));
                return;
            }
            int64 RowCount = 0;
            // CONFIRMED: GetColumnValueByIndex(int32, int64&) overload exists.
            if (!CountStmt.GetColumnValueByIndex(0, RowCount))
            {
                UE_LOG(LogDwmEconomySpike, Error, TEXT("GetColumnValueByIndex(0) failed reading COUNT."));
                return;
            }
            UE_LOG(LogDwmEconomySpike, Log, TEXT("StoneLedger row count: %lld"), RowCount);
            if (RowCount == 0)
            {
                UE_LOG(LogDwmEconomySpike, Error, TEXT("StoneLedger is EMPTY -- this file proves nothing about REAL precision. Aborting."));
                return;
            }
        }

        // ------------------------------------------------------------------
        // Step 2: CommunityDollarVault (Balance, Threshold -- both REAL) + CommunityFailureStatus.
        // ------------------------------------------------------------------
        UE_LOG(LogDwmEconomySpike, Log, TEXT("--- Step 2: CommunityDollarVault / CommunityFailureStatus ---"));
        for (const FKnownVault& Known : KnownVaults)
        {
            FSQLitePreparedStatement VaultStmt = Db.PrepareStatement(
                TEXT("SELECT Balance, Threshold FROM CommunityDollarVault WHERE CommunityId = $id;"));
            if (!VaultStmt.IsValid())
            {
                UE_LOG(LogDwmEconomySpike, Error, TEXT("PrepareStatement(vault) failed: %s"), *Db.GetLastError());
                continue;
            }
            // CONFIRMED: SetBindingValueByName(const TCHAR*, const FString&) overload exists.
            if (!VaultStmt.SetBindingValueByName(TEXT("$id"), FString(Known.CommunityId)))
            {
                UE_LOG(LogDwmEconomySpike, Error, TEXT("SetBindingValueByName($id) failed: %s"), *Db.GetLastError());
                continue;
            }

            ESQLitePreparedStatementStepResult StepResult;
            if (!CheckStep(VaultStmt, Db, Known.CommunityId, StepResult) || StepResult != ESQLitePreparedStatementStepResult::Row)
            {
                UE_LOG(LogDwmEconomySpike, Error, TEXT("[%s] No CommunityDollarVault row found."), Known.CommunityId);
                continue;
            }

            double Balance = 0.0, Threshold = 0.0;
            // CONFIRMED: GetColumnValueByName(const TCHAR*, double&) overload exists.
            bool bBalanceOk = VaultStmt.GetColumnValueByName(TEXT("Balance"), Balance);
            bool bThresholdOk = VaultStmt.GetColumnValueByName(TEXT("Threshold"), Threshold);
            if (!bBalanceOk || !bThresholdOk)
            {
                UE_LOG(LogDwmEconomySpike, Error, TEXT("[%s] GetColumnValueByName failed for Balance/Threshold."), Known.CommunityId);
                continue;
            }

            // Full precision (%.17g -- enough decimal digits to round-trip an IEEE 754 double
            // exactly) so a truncation/rounding difference is visible, not hidden by %f's
            // default 6-digit formatting.
            UE_LOG(LogDwmEconomySpike, Log, TEXT("[%s] Balance=%.17g  Threshold=%.17g"), Known.CommunityId, Balance, Threshold);

            const bool bBalanceMatches = (Balance == Known.Balance);
            const bool bThresholdMatches = (Threshold == Known.Threshold);
            UE_LOG(LogDwmEconomySpike, Log, TEXT("[%s] Balance %s (expected %.17g) | Threshold %s (expected %.17g)"),
                Known.CommunityId,
                bBalanceMatches ? TEXT("PASS") : TEXT("FAIL"), Known.Balance,
                bThresholdMatches ? TEXT("PASS") : TEXT("FAIL"), Known.Threshold);

            FSQLitePreparedStatement StateStmt = Db.PrepareStatement(
                TEXT("SELECT State FROM CommunityFailureStatus WHERE CommunityId = $id;"));
            StateStmt.SetBindingValueByName(TEXT("$id"), FString(Known.CommunityId));
            ESQLitePreparedStatementStepResult StateStepResult;
            if (CheckStep(StateStmt, Db, Known.CommunityId, StateStepResult) && StateStepResult == ESQLitePreparedStatementStepResult::Row)
            {
                FString State;
                StateStmt.GetColumnValueByName(TEXT("State"), State);
                const bool bStateMatches = (State == Known.State);
                UE_LOG(LogDwmEconomySpike, Log, TEXT("[%s] State=%s %s (expected %s)"),
                    Known.CommunityId, *State, bStateMatches ? TEXT("PASS") : TEXT("FAIL"), Known.State);
            }
        }

        // ------------------------------------------------------------------
        // Step 3: StoneLedger rows, explicit null-vs-non-null handling.
        // ------------------------------------------------------------------
        UE_LOG(LogDwmEconomySpike, Log, TEXT("--- Step 3: StoneLedger null handling ---"));
        FSQLitePreparedStatement LedgerStmt = Db.PrepareStatement(
            TEXT("SELECT TransactionId, Amount, FromCommunityId, ToCommunityId, ResourceId, Quantity, Memo FROM StoneLedger;"));
        if (!LedgerStmt.IsValid())
        {
            UE_LOG(LogDwmEconomySpike, Error, TEXT("PrepareStatement(StoneLedger) failed: %s"), *Db.GetLastError());
            return;
        }

        double NetworkSum = 0.0;
        TMap<FString, double> CreditByCommunity, DebitByCommunity;
        int32 RowsRead = 0, NullResourceRowsRead = 0, NonNullResourceRowsRead = 0;

        for (;;)
        {
            ESQLitePreparedStatementStepResult StepResult;
            if (!CheckStep(LedgerStmt, Db, TEXT("StoneLedger row"), StepResult))
                return;
            if (StepResult == ESQLitePreparedStatementStepResult::Done)
                break;
            if (StepResult != ESQLitePreparedStatementStepResult::Row)
                continue;

            RowsRead++;

            FString TransactionId, FromCommunityId, ToCommunityId, Memo;
            double Amount = 0.0;
            LedgerStmt.GetColumnValueByName(TEXT("TransactionId"), TransactionId);
            LedgerStmt.GetColumnValueByName(TEXT("Amount"), Amount);
            LedgerStmt.GetColumnValueByName(TEXT("FromCommunityId"), FromCommunityId);
            LedgerStmt.GetColumnValueByName(TEXT("ToCommunityId"), ToCommunityId);

            // *** NULL-CHECK-BEFORE-READ, explicitly named ***
            // Uses FSQLitePreparedStatement::GetColumnTypeByName(name, ESQLiteColumnType&) --
            // CONFIRMED against SQLitePreparedStatement.h (bool return + out-param, not a
            // direct-return comparison -- see IsColumnNullByName's comment above for the bug
            // this caught in the first draft).
            bool bResourceIdIsNull = false;
            if (!IsColumnNullByName(LedgerStmt, Db, TEXT("ResourceId"), bResourceIdIsNull))
                continue;

            FString ResourceId;
            TOptional<double> Quantity;
            if (!bResourceIdIsNull)
            {
                LedgerStmt.GetColumnValueByName(TEXT("ResourceId"), ResourceId);
                double QuantityValue = 0.0;
                bool bQuantityIsNull = false;
                if (IsColumnNullByName(LedgerStmt, Db, TEXT("Quantity"), bQuantityIsNull)
                    && !bQuantityIsNull && LedgerStmt.GetColumnValueByName(TEXT("Quantity"), QuantityValue))
                {
                    Quantity = QuantityValue;
                }
                NonNullResourceRowsRead++;
                UE_LOG(LogDwmEconomySpike, Log, TEXT("[non-null row] TxId=%s Amount=%.17g ResourceId=%s Quantity=%s"),
                    *TransactionId, Amount, *ResourceId,
                    Quantity.IsSet() ? *FString::Printf(TEXT("%.17g"), Quantity.GetValue()) : TEXT("<unexpectedly null>"));
            }
            else
            {
                NullResourceRowsRead++;
                // Deliberately do NOT read Amount from a "reading a NULL REAL as 0.0" shortcut
                // anywhere here -- ResourceId/Quantity are simply never read at all when the
                // null check above says so, which is the correct behavior this step is meant
                // to prove (no silently-wrong zero standing in for absence).
                UE_LOG(LogDwmEconomySpike, Log, TEXT("[NULL row] TxId=%s Amount=%.17g ResourceId=<NULL, correctly skipped> Quantity=<NULL, correctly skipped>"),
                    *TransactionId, Amount);
            }

            if (!CreditByCommunity.Contains(ToCommunityId)) CreditByCommunity.Add(ToCommunityId, 0.0);
            if (!DebitByCommunity.Contains(FromCommunityId)) DebitByCommunity.Add(FromCommunityId, 0.0);
            CreditByCommunity[ToCommunityId] += Amount;
            DebitByCommunity[FromCommunityId] += Amount;
        }

        UE_LOG(LogDwmEconomySpike, Log, TEXT("StoneLedger rows read: %d (null-ResourceId: %d, non-null-ResourceId: %d)"),
            RowsRead, NullResourceRowsRead, NonNullResourceRowsRead);

        // ------------------------------------------------------------------
        // Step 4: network-sum-zero, recomputed independently on the C++ side.
        // ------------------------------------------------------------------
        TSet<FString> AllCommunityIds;
        for (const auto& Pair : CreditByCommunity) AllCommunityIds.Add(Pair.Key);
        for (const auto& Pair : DebitByCommunity) AllCommunityIds.Add(Pair.Key);
        for (const FString& CommunityId : AllCommunityIds)
        {
            const double Credit = CreditByCommunity.Contains(CommunityId) ? CreditByCommunity[CommunityId] : 0.0;
            const double Debit = DebitByCommunity.Contains(CommunityId) ? DebitByCommunity[CommunityId] : 0.0;
            NetworkSum += (Credit - Debit);
        }
        UE_LOG(LogDwmEconomySpike, Log, TEXT("--- Step 4: Network-sum-zero (THE most important check) ---"));
        UE_LOG(LogDwmEconomySpike, Log, TEXT("Computed network sum: %.17g -- %s"),
            NetworkSum, (NetworkSum == 0.0) ? TEXT("PASS (exactly zero)") : TEXT("FAIL (not exactly zero)"));

        // ------------------------------------------------------------------
        // Step 5: distinct Role / State literals actually observed.
        // ------------------------------------------------------------------
        UE_LOG(LogDwmEconomySpike, Log, TEXT("--- Step 5: distinct Role / State literals ---"));
        {
            FSQLitePreparedStatement RoleStmt = Db.PrepareStatement(TEXT("SELECT DISTINCT Role FROM CommunityResources;"));
            ESQLitePreparedStatementStepResult StepResult;
            while (CheckStep(RoleStmt, Db, TEXT("Role"), StepResult) && StepResult == ESQLitePreparedStatementStepResult::Row)
            {
                FString Role;
                RoleStmt.GetColumnValueByName(TEXT("Role"), Role);
                const bool bKnown = (Role == TEXT("Produces") || Role == TEXT("Needs"));
                UE_LOG(LogDwmEconomySpike, Log, TEXT("Role literal observed: '%s' %s"), *Role, bKnown ? TEXT("(expected)") : TEXT("(**UNEXPECTED**)"));
            }
        }
        {
            FSQLitePreparedStatement StateStmt2 = Db.PrepareStatement(TEXT("SELECT DISTINCT State FROM CommunityFailureStatus;"));
            ESQLitePreparedStatementStepResult StepResult;
            while (CheckStep(StateStmt2, Db, TEXT("State"), StepResult) && StepResult == ESQLitePreparedStatementStepResult::Row)
            {
                FString State;
                StateStmt2.GetColumnValueByName(TEXT("State"), State);
                const bool bKnown = (State == TEXT("Healthy") || State == TEXT("CascadingFailure"));
                UE_LOG(LogDwmEconomySpike, Log, TEXT("State literal observed: '%s' %s"), *State, bKnown ? TEXT("(expected)") : TEXT("(**UNEXPECTED**)"));
            }
        }
    }

    void RunSpike(const TCHAR* DbPath)
    {
        UE_LOG(LogDwmEconomySpike, Log, TEXT("=== DwmEconomySqliteSpike starting: %s ==="), DbPath);

        // Diagnostic added to isolate whether this is a SQLite-internal problem or a more
        // fundamental "the engine process can't see this file" problem -- checks entirely
        // independent of FSQLiteDatabase/SQLite's own VFS layer.
        const bool bFileExists = IFileManager::Get().FileExists(DbPath);
        const int64 FileSize = IFileManager::Get().FileSize(DbPath);
        UE_LOG(LogDwmEconomySpike, Log, TEXT("IFileManager check: FileExists=%s FileSize=%lld"),
            bFileExists ? TEXT("true") : TEXT("false"), FileSize);

        FSQLiteDatabase Db;
        // CONFIRMED against SQLiteDatabase.h: Open(filename, ESQLiteDatabaseOpenMode).
        if (!Db.Open(DbPath, ESQLiteDatabaseOpenMode::ReadOnly))
        {
            UE_LOG(LogDwmEconomySpike, Error, TEXT("Open() failed: %s"), *Db.GetLastError());
            return; // nothing to close -- Open() itself failed.
        }
        UE_LOG(LogDwmEconomySpike, Log, TEXT("Opened ReadOnly successfully."));

        RunSpikeSteps(Db, DbPath);

        // ------------------------------------------------------------------
        // Step 6: close, then re-open ReadOnly to confirm no lingering lock. ALWAYS runs
        // here, regardless of which internal path RunSpikeSteps took (success, an expected
        // "no such table" on a non-economy file, or any other early problem) -- that
        // guarantee is the entire reason Steps 1-5 were split into their own function.
        // ------------------------------------------------------------------
        UE_LOG(LogDwmEconomySpike, Log, TEXT("--- Step 6: close + reopen lock check ---"));
        if (!Db.Close())
        {
            UE_LOG(LogDwmEconomySpike, Error, TEXT("Close() failed: %s"), *Db.GetLastError());
            return;
        }
        UE_LOG(LogDwmEconomySpike, Log, TEXT("Closed successfully."));

        FSQLiteDatabase ReopenDb;
        if (!ReopenDb.Open(DbPath, ESQLiteDatabaseOpenMode::ReadOnly))
        {
            UE_LOG(LogDwmEconomySpike, Error, TEXT("Reopen FAILED (possible lingering lock): %s"), *ReopenDb.GetLastError());
        }
        else
        {
            UE_LOG(LogDwmEconomySpike, Log, TEXT("Reopen PASS -- no lingering lock."));
            ReopenDb.Close();
        }

        UE_LOG(LogDwmEconomySpike, Log, TEXT("=== DwmEconomySqliteSpike finished. ==="));
    }
}

// Standard UE5 FAutoConsoleCommand idiom -- see top-of-file note: this one part is not
// verified against project-specific headers (it isn't part of SQLiteCore).
static FAutoConsoleCommand DwmEconomySqliteSpikeCmd(
    TEXT("DwmEconomySqliteSpike"),
    TEXT("Day 16 throwaway spike: DwmEconomySqliteSpike <full-path-to-exported-economy-.db>"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        if (Args.Num() < 1)
        {
            UE_LOG(LogDwmEconomySpike, Error, TEXT("Usage: DwmEconomySqliteSpike <full-path-to-exported-economy-.db>"));
            return;
        }
        RunSpike(*Args[0]);
    })
);
