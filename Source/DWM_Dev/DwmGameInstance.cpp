// DwmGameInstance.cpp

#include "DwmGameInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GenericPlatform/GenericPlatformHttp.h"

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
    {
        return false;
    }

    // Take everything from "dwmworld://" onward, then trim at the first
    // whitespace (the URL is a single token) and strip surrounding quotes.
    FString Tail = FullCmdLine.RightChop(SchemeIdx);

    int32 SpaceIdx;
    if (Tail.FindChar(TEXT(' '), SpaceIdx))
    {
        Tail = Tail.Left(SpaceIdx);
    }

    Tail = Tail.TrimQuotes();
    OutUrl = Tail;
    return !OutUrl.IsEmpty();
}

void UDwmGameInstance::HandleDwmUrl(const FString& Url)
{
    // Expected shape: dwmworld://launch?id=abc123&platform=Windows
    const FString SchemePrefix = TEXT("dwmworld://");
    if (!Url.StartsWith(SchemePrefix, ESearchCase::IgnoreCase))
    {
        UE_LOG(LogTemp, Warning, TEXT("[DWM] URL is not a dwmworld:// link: %s"), *Url);
        return;
    }

    // Strip the scheme, then split host/path from query string.
    const FString WithoutScheme = Url.RightChop(SchemePrefix.Len());

    FString PathPart;
    FString QueryPart;
    if (!WithoutScheme.Split(TEXT("?"), &PathPart, &QueryPart))
    {
        PathPart = WithoutScheme;
        QueryPart = TEXT("");
    }

    // Parse query parameters into a map.
    TMap<FString, FString> Params;
    TArray<FString> Pairs;
    QueryPart.ParseIntoArray(Pairs, TEXT("&"), /*CullEmpty*/ true);
    for (const FString& Pair : Pairs)
    {
        FString Key;
        FString Value;
        if (Pair.Split(TEXT("="), &Key, &Value))
        {
            Params.Add(Key, FGenericPlatformHttp::UrlDecode(Value));
        }
    }

    // Validate before use — the URL is untrusted input.
    if (const FString* FoundId = Params.Find(TEXT("id")))
    {
        const FString WorldId = *FoundId;

        // Basic sanity check: non-empty, reasonable length, no path separators.
        if (WorldId.IsEmpty() || WorldId.Len() > 128 ||
            WorldId.Contains(TEXT("/")) || WorldId.Contains(TEXT("\\")) ||
            WorldId.Contains(TEXT("..")))
        {
            UE_LOG(LogTemp, Warning, TEXT("[DWM] Rejected suspicious world id: %s"), *WorldId);
            return;
        }

        PendingWorldId = WorldId;
        UE_LOG(LogTemp, Log, TEXT("[DWM] Parsed world id: %s (path=%s)"), *WorldId, *PathPart);
        LoadDwmWorld(WorldId);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[DWM] No 'id' parameter in launch URL."));
    }
}

void UDwmGameInstance::LoadDwmWorld(const FString& WorldId)
{
    // Phase 3 stub. Next steps will:
    //   1. Locate the DWM_WorldPackage.db for this WorldId.
    //   2. Open it via USQLite and read block params + asset bindings.
    //   3. Open the world level and spawn actors with bound meshes.
    UE_LOG(LogTemp, Log, TEXT("[DWM] LoadDwmWorld('%s') — stub. World loading wired up in later Phase 3 tasks."), *WorldId);
}
