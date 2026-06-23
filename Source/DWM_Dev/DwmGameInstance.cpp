// DwmGameInstance.cpp
// Handles dwmworld:// launch URLs and loads DWM world packages from SQLite.

#include "DwmGameInstance.h"
#include "DwmWorldPackageTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "SQLiteDatabase.h"

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
        UE_LOG(LogTemp, Log, TEXT("[DWM] No dwmworld:// launch URL on command line."));
    }
}

bool UDwmGameInstance::TryGetLaunchUrl(FString& OutUrl) const
{
    const FString FullCmdLine = FCommandLine::Get();

    const int32 SchemeIdx = FullCmdLine.Find(
        TEXT("dwmworld://"),
        ESearchCase::IgnoreCase,
        ESearchDir::FromStart);

    if (SchemeIdx == INDEX_NONE)
        return false;

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
    const FString SchemePrefix = TEXT("dwmworld://");
    if (!Url.StartsWith(SchemePrefix, ESearchCase::IgnoreCase))
    {
        UE_LOG(LogTemp, Warning, TEXT("[DWM] Not a dwmworld:// link: %s"), *Url);
        return;
    }

    const FString WithoutScheme = Url.RightChop(SchemePrefix.Len());

    FString PathPart;
    FString QueryPart;
    if (!WithoutScheme.Split(TEXT("?"), &PathPart, &QueryPart))
    {
        PathPart = WithoutScheme;
        QueryPart = TEXT("");
    }

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
            UE_LOG(LogTemp, Warning, TEXT("[DWM] Rejected suspicious world id: %s"), *WorldId);
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

void UDwmGameInstance::LoadDwmWorld(const FString& WorldId)
{
    // Build the expected package path.
    // Convention: C:\DreamWorldMaker\Packages\DWM_WorldPackage_<id>.db
    const FString PackagePath = FString::Printf(
        TEXT("C:/DreamWorldMaker/Packages/DWM_WorldPackage_%s.db"), *WorldId);

    UE_LOG(LogTemp, Log, TEXT("[DWM] Loading world package: %s"), *PackagePath);

    // Open the SQLite database.
    FSQLiteDatabase Db;
    if (!Db.Open(*PackagePath, ESQLiteDatabaseOpenMode::ReadOnly))
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DWM] Failed to open world package at: %s"), *PackagePath);
        return;
    }

    // ------------------------------------------------------------------
    // Read WorldInfo
    // ------------------------------------------------------------------
    {
        FSQLitePreparedStatement Stmt;
        Stmt.Create(Db, TEXT("SELECT WorldId, Name, Description, SchemaVersion FROM WorldInfo LIMIT 1;"),
            ESQLitePreparedStatementFlags::Persistent);

        if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
        {
            FString Id, Name, Desc;
            int64 Version = 0;
            Stmt.GetColumnValueByIndex(0, Id);
            Stmt.GetColumnValueByIndex(1, Name);
            Stmt.GetColumnValueByIndex(2, Desc);
            Stmt.GetColumnValueByIndex(3, Version);
            UE_LOG(LogTemp, Log,
                TEXT("[DWM] WorldInfo: id=%s name='%s' schema_v=%lld"),
                *Id, *Name, Version);
        }
        Stmt.Destroy();
    }

    // ------------------------------------------------------------------
    // Read Blocks
    // ------------------------------------------------------------------
    TArray<FDwmBlock> Blocks;
    {
        FSQLitePreparedStatement Stmt;
        Stmt.Create(Db, TEXT("SELECT BlockId, Name, BlockType FROM Blocks;"),
            ESQLitePreparedStatementFlags::Persistent);

        while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
        {
            FDwmBlock Block;
            Stmt.GetColumnValueByIndex(0, Block.BlockId);
            Stmt.GetColumnValueByIndex(1, Block.Name);
            Stmt.GetColumnValueByIndex(2, Block.BlockType);
            Blocks.Add(Block);
            UE_LOG(LogTemp, Log,
                TEXT("[DWM] Block: id=%s name='%s' type=%s"),
                *Block.BlockId, *Block.Name, *Block.BlockType);
        }
        Stmt.Destroy();
    }

    // ------------------------------------------------------------------
    // Read Parameters
    // ------------------------------------------------------------------
    {
        FSQLitePreparedStatement Stmt;
        Stmt.Create(Db, TEXT("SELECT BlockId, Name, Value, Unit FROM Parameters;"),
            ESQLitePreparedStatementFlags::Persistent);

        while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
        {
            FString BlockId, Name, Unit;
            double Value = 0.0;
            Stmt.GetColumnValueByIndex(0, BlockId);
            Stmt.GetColumnValueByIndex(1, Name);
            Stmt.GetColumnValueByIndex(2, Value);
            Stmt.GetColumnValueByIndex(3, Unit);
            UE_LOG(LogTemp, Log,
                TEXT("[DWM] Param: block=%s %s=%.4f %s"),
                *BlockId, *Name, Value, *Unit);
        }
        Stmt.Destroy();
    }

    // ------------------------------------------------------------------
    // Read AssetBindings
    // ------------------------------------------------------------------
    TArray<FDwmAssetBinding> Bindings;
    {
        FSQLitePreparedStatement Stmt;
        Stmt.Create(Db,
            TEXT("SELECT BlockId, AssetPath, AssetType, Role FROM AssetBindings;"),
            ESQLitePreparedStatementFlags::Persistent);

        while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
        {
            FDwmAssetBinding Binding;
            Stmt.GetColumnValueByIndex(0, Binding.BlockId);
            Stmt.GetColumnValueByIndex(1, Binding.AssetPath);
            Stmt.GetColumnValueByIndex(2, Binding.AssetType);
            Stmt.GetColumnValueByIndex(3, Binding.Role);
            Bindings.Add(Binding);
            UE_LOG(LogTemp, Log,
                TEXT("[DWM] Asset: block=%s path=%s role=%s"),
                *Binding.BlockId, *Binding.AssetPath, *Binding.Role);
        }
        Stmt.Destroy();
    }

    // ------------------------------------------------------------------
    // Read SimSamples (log first 3 only to keep log readable)
    // ------------------------------------------------------------------
    {
        FSQLitePreparedStatement Stmt;
        Stmt.Create(Db,
            TEXT("SELECT BlockId, Time, Position, Velocity FROM SimSamples ORDER BY Time ASC;"),
            ESQLitePreparedStatementFlags::Persistent);

        int32 SampleCount = 0;
        while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
        {
            if (SampleCount < 3)
            {
                FString BlockId;
                double Time = 0.0, Position = 0.0, Velocity = 0.0;
                Stmt.GetColumnValueByIndex(0, BlockId);
                Stmt.GetColumnValueByIndex(1, Time);
                Stmt.GetColumnValueByIndex(2, Position);
                Stmt.GetColumnValueByIndex(3, Velocity);
                UE_LOG(LogTemp, Log,
                    TEXT("[DWM] Sample[%d]: t=%.3f pos=%.4f vel=%.4f"),
                    SampleCount, Time, Position, Velocity);
            }
            SampleCount++;
        }
        UE_LOG(LogTemp, Log, TEXT("[DWM] Total sim samples: %d"), SampleCount);
        Stmt.Destroy();
    }

    Db.Close();

    UE_LOG(LogTemp, Log,
        TEXT("[DWM] World package loaded. Blocks=%d Bindings=%d — spawn step next."),
        Blocks.Num(), Bindings.Num());

    // Next Phase 3 task: spawn an actor per block, load the bound mesh,
    // and drive it from the SimSamples each tick.
}