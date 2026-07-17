// DwmGameInstance.cpp
// Handles dwmworld:// launch URLs and loads DWM world packages from SQLite.
// Actors are spawned in OnStart() (deferred) because GetWorld() is null
// during Init() before the first level has loaded.

#include "DwmGameInstance.h"
#include "DwmWorldPackageTypes.h"
#include "DwmPendulumActor.h"
#include "DwmEconomyWriter.h"
#include "DwmTradeTerminalActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "SQLiteDatabase.h"
#include "SQLitePreparedStatement.h"
#include "TimerManager.h"

// ---------------------------------------------------------------------------
// Init — runs at game startup (before level loads). Read the package here;
// defer spawning to OnStart().
// ---------------------------------------------------------------------------

void UDwmGameInstance::Init()
{
    Super::Init();

    FString LaunchUrl;
    if (TryGetLaunchUrl(LaunchUrl))
    {
        UE_LOG(LogTemp, Log, TEXT("[DWM] Launch URL detected: %s"), *LaunchUrl);
        HandleDwmUrl(LaunchUrl);
    }
    else
    {
        UE_LOG(LogTemp, Log,
            TEXT("[DWM] No dwmworld:// launch URL — will spawn on Play."));

        // For PIE testing without a real launch URL, load the pendulum
        // package directly so hitting Play always shows the pendulum.
        LoadDwmWorld(TEXT("pendulum"));
    }
}

// ---------------------------------------------------------------------------
// OnStart — fires after the first level has loaded. GetWorld() is valid here.
// ---------------------------------------------------------------------------

void UDwmGameInstance::OnStart()
{
    Super::OnStart();
    SpawnWorldActors();
    RefreshEconomyState();

    // The terminal is positioned relative to the gameplay pawn, which is not guaranteed to
    // exist during Init(). Deferring one tick keeps the trigger in the loaded world.
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimerForNextTick(
            FTimerDelegate::CreateUObject(this, &UDwmGameInstance::SpawnDemoTradeTerminal));
    }
}

// ---------------------------------------------------------------------------
// URL parsing
// ---------------------------------------------------------------------------

bool UDwmGameInstance::TryGetLaunchUrl(FString& OutUrl) const
{
    const FString FullCmdLine = FCommandLine::Get();
    const int32 SchemeIdx = FullCmdLine.Find(
        TEXT("dwmworld://"), ESearchCase::IgnoreCase, ESearchDir::FromStart);
    if (SchemeIdx == INDEX_NONE) return false;

    FString Tail = FullCmdLine.RightChop(SchemeIdx);
    int32 SpaceIdx;
    if (Tail.FindChar(TEXT(' '), SpaceIdx))
        Tail = Tail.Left(SpaceIdx);

    Tail = Tail.TrimQuotes();
    OutUrl = Tail;
    return !OutUrl.IsEmpty();
}

void UDwmGameInstance::HandleDwmUrl(const FString& Url)
{
    const FString Prefix = TEXT("dwmworld://");
    if (!Url.StartsWith(Prefix, ESearchCase::IgnoreCase))
    {
        UE_LOG(LogTemp, Warning, TEXT("[DWM] Not a dwmworld:// link: %s"), *Url);
        return;
    }

    FString PathPart, QueryPart;
    Url.RightChop(Prefix.Len()).Split(TEXT("?"), &PathPart, &QueryPart);

    TMap<FString, FString> Params;
    TArray<FString> Pairs;
    QueryPart.ParseIntoArray(Pairs, TEXT("&"), true);
    for (const FString& Pair : Pairs)
    {
        FString Key, Value;
        if (Pair.Split(TEXT("="), &Key, &Value))
            Params.Add(Key, FGenericPlatformHttp::UrlDecode(Value));
    }

    if (const FString* FoundId = Params.Find(TEXT("id")))
    {
        const FString WorldId = *FoundId;
        if (WorldId.IsEmpty() || WorldId.Len() > 128 ||
            WorldId.Contains(TEXT("/")) || WorldId.Contains(TEXT("\\")) ||
            WorldId.Contains(TEXT("..")))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[DWM] Rejected suspicious world id: %s"), *WorldId);
            return;
        }
        PendingWorldId = WorldId;
        UE_LOG(LogTemp, Log, TEXT("[DWM] Parsed world id: %s"), *WorldId);
        LoadDwmWorld(WorldId);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[DWM] No 'id' parameter in launch URL."));
    }
}

// ---------------------------------------------------------------------------
// LoadDwmWorld — reads the SQLite package and stores data for deferred spawn.
// ---------------------------------------------------------------------------

void UDwmGameInstance::LoadDwmWorld(const FString& WorldId)
{
    const FString PackagePath = FString::Printf(
        TEXT("C:/DreamWorldMaker/Packages/DWM_WorldPackage_%s.db"), *WorldId);

    UE_LOG(LogTemp, Log, TEXT("[DWM] Loading world package: %s"), *PackagePath);

    FSQLiteDatabase Db;
    if (!Db.Open(*PackagePath, ESQLiteDatabaseOpenMode::ReadOnly))
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DWM] Failed to open world package: %s"), *PackagePath);
        return;
    }

    // WorldInfo
    {
        FSQLitePreparedStatement Stmt;
        Stmt.Create(Db,
            TEXT("SELECT WorldId, Name, Description, SchemaVersion FROM WorldInfo LIMIT 1;"),
            ESQLitePreparedStatementFlags::Persistent);
        if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
        {
            FString Id, Name, Desc; int64 Ver = 0;
            Stmt.GetColumnValueByIndex(0, Id);
            Stmt.GetColumnValueByIndex(1, Name);
            Stmt.GetColumnValueByIndex(2, Desc);
            Stmt.GetColumnValueByIndex(3, Ver);
            UE_LOG(LogTemp, Log,
                TEXT("[DWM] WorldInfo: id=%s name='%s' schema_v=%lld"),
                *Id, *Name, Ver);
        }
        Stmt.Destroy();
    }

    // Blocks
    TArray<FDwmBlock> Blocks;
    {
        FSQLitePreparedStatement Stmt;
        Stmt.Create(Db,
            TEXT("SELECT BlockId, Name, BlockType FROM Blocks;"),
            ESQLitePreparedStatementFlags::Persistent);
        while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
        {
            FDwmBlock B;
            Stmt.GetColumnValueByIndex(0, B.BlockId);
            Stmt.GetColumnValueByIndex(1, B.Name);
            Stmt.GetColumnValueByIndex(2, B.BlockType);
            Blocks.Add(B);
            UE_LOG(LogTemp, Log,
                TEXT("[DWM] Block: %s (%s)"), *B.Name, *B.BlockType);
        }
        Stmt.Destroy();
    }

    // Parameters (logged only — physics consumed on C# side)
    {
        FSQLitePreparedStatement Stmt;
        Stmt.Create(Db,
            TEXT("SELECT BlockId, Name, Value, Unit FROM Parameters;"),
            ESQLitePreparedStatementFlags::Persistent);
        while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
        {
            FString BId, Name, Unit; double Val = 0.0;
            Stmt.GetColumnValueByIndex(0, BId);
            Stmt.GetColumnValueByIndex(1, Name);
            Stmt.GetColumnValueByIndex(2, Val);
            Stmt.GetColumnValueByIndex(3, Unit);
            UE_LOG(LogTemp, Log,
                TEXT("[DWM] Param: %s = %.4f %s"), *Name, Val, *Unit);
        }
        Stmt.Destroy();
    }

    // Asset bindings
    TMap<FString, FDwmAssetBinding> BindingsByBlock;
    {
        FSQLitePreparedStatement Stmt;
        Stmt.Create(Db,
            TEXT("SELECT BlockId, AssetPath, AssetType, Role FROM AssetBindings;"),
            ESQLitePreparedStatementFlags::Persistent);
        while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
        {
            FDwmAssetBinding Ab;
            Stmt.GetColumnValueByIndex(0, Ab.BlockId);
            Stmt.GetColumnValueByIndex(1, Ab.AssetPath);
            Stmt.GetColumnValueByIndex(2, Ab.AssetType);
            Stmt.GetColumnValueByIndex(3, Ab.Role);
            BindingsByBlock.Add(Ab.BlockId, Ab);
            UE_LOG(LogTemp, Log,
                TEXT("[DWM] Binding: %s -> %s"), *Ab.BlockId, *Ab.AssetPath);
        }
        Stmt.Destroy();
    }

    // Sim samples
    TMap<FString, TArray<FDwmSimSample>> SamplesByBlock;
    {
        FSQLitePreparedStatement Stmt;
        Stmt.Create(Db,
            TEXT("SELECT BlockId, Time, Position, Velocity "
                "FROM SimSamples ORDER BY BlockId, Time ASC;"),
            ESQLitePreparedStatementFlags::Persistent);
        while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
        {
            FDwmSimSample S;
            Stmt.GetColumnValueByIndex(0, S.BlockId);
            Stmt.GetColumnValueByIndex(1, S.Time);
            Stmt.GetColumnValueByIndex(2, S.Position);
            Stmt.GetColumnValueByIndex(3, S.Velocity);
            SamplesByBlock.FindOrAdd(S.BlockId).Add(S);
        }
        UE_LOG(LogTemp, Log, TEXT("[DWM] Sim samples loaded."));
        Stmt.Destroy();
    }

    Db.Close();

    // Store for deferred spawn in OnStart()
    PendingBlocks = Blocks;
    PendingBindings = BindingsByBlock;
    PendingSamples = SamplesByBlock;
    bHasPendingSpawn = true;

    UE_LOG(LogTemp, Log,
        TEXT("[DWM] Package read complete. %d block(s) queued for spawn in OnStart()."),
        Blocks.Num());
}

// ---------------------------------------------------------------------------
// SpawnWorldActors — called from OnStart() when the world is ready.
// ---------------------------------------------------------------------------

void UDwmGameInstance::SpawnWorldActors()
{
    if (!bHasPendingSpawn)
    {
        UE_LOG(LogTemp, Log, TEXT("[DWM] SpawnWorldActors: nothing pending."));
        return;
    }
    bHasPendingSpawn = false;

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[DWM] SpawnWorldActors: GetWorld() is null — cannot spawn."));
        return;
    }

    UE_LOG(LogTemp, Log,
        TEXT("[DWM] SpawnWorldActors: spawning %d block(s)."),
        PendingBlocks.Num());

    int32 SpawnCount = 0;
    for (const FDwmBlock& Block : PendingBlocks)
    {
        const FDwmAssetBinding* Binding = PendingBindings.Find(Block.BlockId);
        if (!Binding)
        {
            UE_LOG(LogTemp, Log,
                TEXT("[DWM] Block '%s' has no binding — skipping."), *Block.Name);
            continue;
        }

        // Space actors along X so multiple blocks don't overlap
        const FVector SpawnLocation(SpawnCount * 300.0f, 0.0f, 200.0f);

        FActorSpawnParameters SpawnParams;
        SpawnParams.Name = FName(*FString::Printf(
            TEXT("DwmActor_%s"), *Block.BlockId));

        ADwmPendulumActor* Actor = World->SpawnActor<ADwmPendulumActor>(
            ADwmPendulumActor::StaticClass(),
            SpawnLocation,
            FRotator::ZeroRotator,
            SpawnParams);

        if (Actor)
        {
            static const TArray<FDwmSimSample> EmptySamples;
            const TArray<FDwmSimSample>& Samples =
                PendingSamples.Contains(Block.BlockId)
                ? PendingSamples[Block.BlockId]
                : EmptySamples;

            Actor->InitialiseFromPackage(Block, *Binding, Samples);
            SpawnCount++;
            UE_LOG(LogTemp, Log,
                TEXT("[DWM] Spawned '%s' at (1160 0, 70)."),
                *Block.Name, SpawnLocation.X);
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[DWM] SpawnActor failed for '%s'."), *Block.Name);
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[DWM] Spawn complete: %d actor(s)."), SpawnCount);
}

// ---------------------------------------------------------------------------
// Day 18 economy round-trip: read current state, settle one fixed demo trade,
// then read again so the visible state comes from the same SQLite file.
// ---------------------------------------------------------------------------

FString UDwmGameInstance::GetEconomyPackagePath() const
{
    // Generated by DWMStudio.WorldPackageCli from the canonical golden scenario.
    // Keep the UE read/write round trip pointed at the same exported package that
    // the DWMStudio economy pipeline produces.
    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Databases/world_economy.db")));
}

void UDwmGameInstance::SetEconomyStatus(const FString& Message, const FColor& Color)
{
    LastEconomyStatusMessage = Message;
    UE_LOG(LogTemp, Log, TEXT("[DWM Economy] %s"), *Message);

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(0xD0018ULL, 6.0f, Color, Message);
    }
}

bool UDwmGameInstance::RefreshEconomyState()
{
    const FString DbPath = GetEconomyPackagePath();
    FSQLiteDatabase Db;
    if (!Db.Open(*DbPath, ESQLiteDatabaseOpenMode::ReadOnly))
    {
        SetEconomyStatus(FString::Printf(
            TEXT("Economy refresh failed to open '%s': %s"), *DbPath, *Db.GetLastError()), FColor::Red);
        return false;
    }

    FSQLitePreparedStatement Stmt;
    if (!Stmt.Create(Db,
        TEXT("SELECT c.CommunityId, c.Name, "
            "COALESCE((SELECT SUM(l.Amount) FROM StoneLedger l WHERE l.ToCommunityId = c.CommunityId), 0.0) "
            "- COALESCE((SELECT SUM(l.Amount) FROM StoneLedger l WHERE l.FromCommunityId = c.CommunityId), 0.0), "
            "v.Balance, v.Threshold, f.State "
            "FROM Communities c "
            "LEFT JOIN CommunityDollarVault v ON v.CommunityId = c.CommunityId "
            "LEFT JOIN CommunityFailureStatus f ON f.CommunityId = c.CommunityId "
            "ORDER BY c.CommunityId;"),
        ESQLitePreparedStatementFlags::Persistent))
    {
        SetEconomyStatus(FString::Printf(
            TEXT("Economy refresh query creation failed: %s"), *Db.GetLastError()), FColor::Red);
        Db.Close();
        return false;
    }

    TArray<FDwmCommunityEconomyState> LoadedStates;
    for (;;)
    {
        const ESQLitePreparedStatementStepResult StepResult = Stmt.Step();
        if (StepResult == ESQLitePreparedStatementStepResult::Done)
        {
            break;
        }
        if (StepResult != ESQLitePreparedStatementStepResult::Row)
        {
            SetEconomyStatus(FString::Printf(
                TEXT("Economy refresh query failed: %s"), *Db.GetLastError()), FColor::Red);
            Stmt.Destroy();
            Db.Close();
            return false;
        }

        FDwmCommunityEconomyState State;
        const bool bReadOk =
            Stmt.GetColumnValueByIndex(0, State.CommunityId)
            && Stmt.GetColumnValueByIndex(1, State.CommunityName)
            && Stmt.GetColumnValueByIndex(2, State.StoneBalance)
            && Stmt.GetColumnValueByIndex(3, State.DollarVaultBalance)
            && Stmt.GetColumnValueByIndex(4, State.DollarVaultThreshold)
            && Stmt.GetColumnValueByIndex(5, State.FailureState);
        if (!bReadOk)
        {
            SetEconomyStatus(FString::Printf(
                TEXT("Economy refresh could not read a community row: %s"), *Db.GetLastError()), FColor::Red);
            Stmt.Destroy();
            Db.Close();
            return false;
        }

        LoadedStates.Add(MoveTemp(State));
    }

    Stmt.Destroy();
    Db.Close();

    if (LoadedStates.IsEmpty())
    {
        SetEconomyStatus(TEXT("Economy refresh found no communities."), FColor::Red);
        return false;
    }

    EconomyStates = MoveTemp(LoadedStates);
    SetEconomyStatus(FString::Printf(
        TEXT("Economy refreshed: %d communities loaded."), EconomyStates.Num()), FColor::Green);
    return true;
}

bool UDwmGameInstance::ExecuteMountainBuysGrainFromValley()
{
    if (!RefreshEconomyState())
    {
        return false;
    }

    const FDwmCommunityEconomyState* MountainBefore = EconomyStates.FindByPredicate(
        [](const FDwmCommunityEconomyState& State) { return State.CommunityId == TEXT("mountain"); });
    const FDwmCommunityEconomyState* ValleyBefore = EconomyStates.FindByPredicate(
        [](const FDwmCommunityEconomyState& State) { return State.CommunityId == TEXT("valley"); });
    if (!MountainBefore || !ValleyBefore)
    {
        SetEconomyStatus(TEXT("Demo trade cannot run: Mountain or Valley is missing from the economy snapshot."), FColor::Red);
        return false;
    }

    const double MountainBalanceBefore = MountainBefore->StoneBalance;
    const double ValleyBalanceBefore = ValleyBefore->StoneBalance;
    const TArray<FString> KnownCommunityIds = {
        TEXT("mountain"), TEXT("hillside"), TEXT("valley"), TEXT("suburb"), TEXT("city") };
    const TArray<FString> KnownResourceIds = {
        TEXT("timber"), TEXT("wind_power"), TEXT("orchard_fruit"), TEXT("wool"), TEXT("grain"),
        TEXT("water"), TEXT("skilled_labor"), TEXT("textiles"), TEXT("manufactured_tools"), TEXT("software_services") };

    constexpr double StoneAmount = 20.0;
    const FDwmEconomyWriter::FWriteTradeResult Result = FDwmEconomyWriter::WriteTrade(
        GetEconomyPackagePath(), KnownCommunityIds, KnownResourceIds,
        TEXT("mountain"), TEXT("valley"), StoneAmount, TEXT("grain"), StoneAmount,
        TEXT("Mountain buys grain from Valley"));
    if (!Result.bSuccess)
    {
        SetEconomyStatus(FString::Printf(TEXT("Trade rejected: %s"), *Result.Message), FColor::Red);
        return false;
    }

    if (!RefreshEconomyState())
    {
        SetEconomyStatus(FString::Printf(
            TEXT("Trade %s settled, but the economy display could not refresh."), *Result.TransactionId), FColor::Red);
        return false;
    }

    const FDwmCommunityEconomyState* MountainAfter = EconomyStates.FindByPredicate(
        [](const FDwmCommunityEconomyState& State) { return State.CommunityId == TEXT("mountain"); });
    const FDwmCommunityEconomyState* ValleyAfter = EconomyStates.FindByPredicate(
        [](const FDwmCommunityEconomyState& State) { return State.CommunityId == TEXT("valley"); });
    const bool bExpectedDeltas = MountainAfter && ValleyAfter
        && FMath::IsNearlyEqual(MountainAfter->StoneBalance, MountainBalanceBefore - StoneAmount)
        && FMath::IsNearlyEqual(ValleyAfter->StoneBalance, ValleyBalanceBefore + StoneAmount);
    if (!bExpectedDeltas)
    {
        SetEconomyStatus(TEXT("Trade settled, but the refreshed balances do not match the expected Mountain/Valley deltas."), FColor::Red);
        return false;
    }

    SetEconomyStatus(FString::Printf(
        TEXT("Trade complete: Mountain %.0f -> %.0f St | Valley %.0f -> %.0f St"),
        MountainBalanceBefore, MountainAfter->StoneBalance,
        ValleyBalanceBefore, ValleyAfter->StoneBalance), FColor::Green);
    return true;
}

void UDwmGameInstance::SpawnDemoTradeTerminal()
{
    if (bDemoTradeTerminalSpawned)
    {
        return;
    }

    UWorld* World = GetWorld();
    APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
    APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
    if (!World || !PlayerPawn)
    {
        if (++DemoTradeTerminalSpawnAttempts <= 5 && World)
        {
            FTimerHandle RetryHandle;
            World->GetTimerManager().SetTimer(
                RetryHandle,
                FTimerDelegate::CreateUObject(this, &UDwmGameInstance::SpawnDemoTradeTerminal),
                0.25f,
                false);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[DWM Economy] Could not find the player pawn to place the Day 18 trade terminal."));
        }
        return;
    }

    const FVector SpawnLocation = PlayerPawn->GetActorLocation()
        + (PlayerPawn->GetActorForwardVector() * 300.0f)
        + FVector(0.0f, 0.0f, -80.0f);
    FActorSpawnParameters SpawnParameters;
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ADwmTradeTerminalActor* Terminal = World->SpawnActor<ADwmTradeTerminalActor>(
        ADwmTradeTerminalActor::StaticClass(), SpawnLocation, PlayerPawn->GetActorRotation(), SpawnParameters);
    if (!Terminal)
    {
        UE_LOG(LogTemp, Error, TEXT("[DWM Economy] Failed to spawn the Day 18 trade terminal."));
        return;
    }

    bDemoTradeTerminalSpawned = true;
    SetEconomyStatus(TEXT("Day 18 ready: walk to the gold trade terminal and press E to buy Grain from Valley."), FColor::Cyan);
}
