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
#include "Engine/DataTable.h"
#include "Engine/PointLight.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "SQLiteDatabase.h"
#include "SQLitePreparedStatement.h"
#include "TimerManager.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CapsuleComponent.h"
#include "Components/MeshComponent.h"
#include "Components/PanelWidget.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
    // Shared by both ExecuteMountainBuysGrainFromValley and ExecuteConfiguredTrade -- the
    // same lists that already existed inline in the former, hoisted out so both share one
    // definition instead of drifting apart.
    const TArray<FString>& KnownCommunityIds()
    {
        static const TArray<FString> Ids = {
            TEXT("mountain"), TEXT("hillside"), TEXT("valley"), TEXT("suburb"), TEXT("city") };
        return Ids;
    }

    const TArray<FString>& KnownResourceIds()
    {
        static const TArray<FString> Ids = {
            TEXT("timber"), TEXT("wind_power"), TEXT("orchard_fruit"), TEXT("wool"), TEXT("grain"),
            TEXT("water"), TEXT("skilled_labor"), TEXT("textiles"), TEXT("manufactured_tools"), TEXT("software_services") };
        return Ids;
    }

    // The marketplace CharacterCustomizer reports "hair data table ... None" when its
    // persistent save has incomplete values.  Its Blueprint fields are private, so log
    // the serialized SaveGame properties through reflection once when the customizer
    // starts.  This is diagnostic only; it does not alter any saved player data.
    void LogCharacterCustomizerSaveData()
    {
        constexpr TCHAR SaveSlot[] = TEXT("CC_SaveGame");
        if (!UGameplayStatics::DoesSaveGameExist(SaveSlot, 0))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[DWM CC] No %s save exists; the Customizer will create fresh defaults."),
                SaveSlot);
            return;
        }

        USaveGame* SaveGame = UGameplayStatics::LoadGameFromSlot(SaveSlot, 0);
        if (!SaveGame)
        {
            UE_LOG(LogTemp, Error,
                TEXT("[DWM CC] Could not load the %s save."), SaveSlot);
            return;
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[DWM CC] Loaded %s as %s; dumping serialized fields to identify missing Customizer data."),
            SaveSlot, *SaveGame->GetClass()->GetPathName());

        for (TFieldIterator<FProperty> PropertyIt(SaveGame->GetClass(), EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
        {
            const FProperty* Property = *PropertyIt;
            // The pack's Blueprint fields do not use the native SaveGame flag, so log
            // all persistent-looking fields rather than only the flagged subset.
            if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
            {
                continue;
            }

            const void* ValueAddress = Property->ContainerPtrToValuePtr<void>(SaveGame);
            FString ExportedValue;
            Property->ExportTextItem_Direct(ExportedValue, ValueAddress, nullptr, SaveGame, PPF_None);
            FString TypeName = Property->GetCPPType();
            if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
            {
                TypeName = FString::Printf(TEXT("Map<%s, %s>"),
                    *MapProperty->KeyProp->GetCPPType(), *MapProperty->ValueProp->GetCPPType());
            }
            UE_LOG(LogTemp, Warning, TEXT("[DWM CC] Save.%s (%s) = %s"),
                *Property->GetName(), *TypeName, *ExportedValue.Left(2000));

        }
    }

    bool BootstrapCharacterCustomizerSave(UObject* CustomizerPawn)
    {
        constexpr TCHAR SaveSlot[] = TEXT("CC_SaveGame");
        if (!CustomizerPawn)
        {
            return false;
        }

        USaveGame* SaveGame = UGameplayStatics::LoadGameFromSlot(SaveSlot, 0);
        if (!SaveGame)
        {
            UE_LOG(LogTemp, Error, TEXT("[DWM CC] Cannot initialize %s: the save object could not be loaded."), SaveSlot);
            return false;
        }

        FMapProperty* SavedCharactersProperty = FindFProperty<FMapProperty>(
            SaveGame->GetClass(), FName(TEXT("Saved Characters")));
        FNameProperty* KeyProperty = SavedCharactersProperty
            ? CastField<FNameProperty>(SavedCharactersProperty->KeyProp) : nullptr;
        FStructProperty* ValueProperty = SavedCharactersProperty
            ? CastField<FStructProperty>(SavedCharactersProperty->ValueProp) : nullptr;
        if (!SavedCharactersProperty || !KeyProperty || !ValueProperty)
        {
            UE_LOG(LogTemp, Error, TEXT("[DWM CC] Cannot initialize the saved character: CC_SaveObject's map schema is unexpected."));
            return false;
        }

        FScriptMapHelper SavedCharacters(SavedCharactersProperty,
            SavedCharactersProperty->ContainerPtrToValuePtr<void>(SaveGame));
        if (SavedCharacters.Num() > 0)
        {
            return false;
        }

        // The sample pawn keeps its authored fallback in "Default Preset", but
        // the runtime "Preset" value is unset in levels that do not originate
        // from the pack's demo map.  The asset's Spawn Character Local routine
        // reads the runtime value, so promote the authored fallback before we
        // ask it to create the first saved character.
        FObjectPropertyBase* ActivePresetProperty = FindFProperty<FObjectPropertyBase>(CustomizerPawn->GetClass(),
            FName(TEXT("Preset")));
        FObjectPropertyBase* DefaultPresetProperty = FindFProperty<FObjectPropertyBase>(CustomizerPawn->GetClass(),
            FName(TEXT("Default Preset")));
        if (!ActivePresetProperty || !DefaultPresetProperty)
        {
            FProperty* RawActivePresetProperty = FindFProperty<FProperty>(CustomizerPawn->GetClass(), FName(TEXT("Preset")));
            FProperty* RawDefaultPresetProperty = FindFProperty<FProperty>(CustomizerPawn->GetClass(), FName(TEXT("Default Preset")));
            UE_LOG(LogTemp, Error, TEXT("[DWM CC] Could not access pawn presets as object properties. Active=%s (%s), Default=%s (%s)"),
                *GetNameSafe(RawActivePresetProperty),
                RawActivePresetProperty ? *RawActivePresetProperty->GetClass()->GetName() : TEXT("missing"),
                *GetNameSafe(RawDefaultPresetProperty),
                RawDefaultPresetProperty ? *RawDefaultPresetProperty->GetClass()->GetName() : TEXT("missing"));
        }
        if (ActivePresetProperty && DefaultPresetProperty)
        {
            void* ActivePresetAddress = ActivePresetProperty->ContainerPtrToValuePtr<void>(CustomizerPawn);
            const void* DefaultPresetAddress = DefaultPresetProperty->ContainerPtrToValuePtr<void>(CustomizerPawn);
            UObject* DefaultPreset = DefaultPresetProperty->GetObjectPropertyValue(DefaultPresetAddress);
            if (DefaultPreset)
            {
                ActivePresetProperty->SetObjectPropertyValue(ActivePresetAddress, DefaultPreset);

                if (FObjectPropertyBase* CustomizableCharacterProperty = FindFProperty<FObjectPropertyBase>(CustomizerPawn->GetClass(),
                    FName(TEXT("Customizable Character"))))
                {
                    const void* CharacterAddress = CustomizableCharacterProperty->ContainerPtrToValuePtr<void>(CustomizerPawn);
                    if (UObject* CustomizableCharacter = CustomizableCharacterProperty->GetObjectPropertyValue(CharacterAddress))
                    {
                        if (FObjectPropertyBase* CharacterPresetProperty = FindFProperty<FObjectPropertyBase>(CustomizableCharacter->GetClass(),
                            FName(TEXT("Preset"))))
                        {
                            void* CharacterPresetAddress = CharacterPresetProperty->ContainerPtrToValuePtr<void>(CustomizableCharacter);
                            if (!CharacterPresetProperty->GetObjectPropertyValue(CharacterPresetAddress))
                            {
                                CharacterPresetProperty->SetObjectPropertyValue(CharacterPresetAddress, DefaultPreset);
                            }
                        }
                    }
                }

                UE_LOG(LogTemp, Log, TEXT("[DWM CC] Applied the pawn's authored Default Preset before initializing the first saved character: %s"),
                    *GetPathNameSafe(DefaultPreset));
            }
        }

        // The pack fills Local Data as part of its Spawn Character path.  A
        // duplicated level has no previous saved character to trigger that
        // branch, so run it once using the pawn's current transform before
        // asking Spawn Character Local for its serialized result.
        if (AActor* PawnActor = Cast<AActor>(CustomizerPawn))
        {
            if (UFunction* SpawnCharacterFunction = CustomizerPawn->FindFunction(FName(TEXT("Spawn Character"))))
            {
                FStructProperty* SpawnTransformProperty = nullptr;
                for (TFieldIterator<FProperty> PropertyIt(SpawnCharacterFunction); PropertyIt; ++PropertyIt)
                {
                    if (FStructProperty* StructProperty = CastField<FStructProperty>(*PropertyIt))
                    {
                        if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
                        {
                            SpawnTransformProperty = StructProperty;
                            break;
                        }
                    }
                }
                if (SpawnTransformProperty)
                {
                    FStructOnScope SpawnCharacterParameters(SpawnCharacterFunction);
                    void* SpawnTransformAddress = SpawnTransformProperty->ContainerPtrToValuePtr<void>(SpawnCharacterParameters.GetStructMemory());
                    *static_cast<FTransform*>(SpawnTransformAddress) = PawnActor->GetActorTransform();
                    CustomizerPawn->ProcessEvent(SpawnCharacterFunction, SpawnCharacterParameters.GetStructMemory());
                    UE_LOG(LogTemp, Log, TEXT("[DWM CC] Ran the asset's Spawn Character initialization for the first saved character."));
                }
            }
        }

        UFunction* SpawnLocalFunction = CustomizerPawn->FindFunction(FName(TEXT("Spawn Character Local")));
        if (!SpawnLocalFunction)
        {
            UE_LOG(LogTemp, Error, TEXT("[DWM CC] Cannot initialize the saved character: Spawn Character Local was not found."));
            return false;
        }

        FStructProperty* SpawnDataProperty = nullptr;
        for (TFieldIterator<FProperty> PropertyIt(SpawnLocalFunction); PropertyIt; ++PropertyIt)
        {
            FProperty* Property = *PropertyIt;
            if (Property->HasAnyPropertyFlags(CPF_OutParm))
            {
                SpawnDataProperty = CastField<FStructProperty>(Property);
                break;
            }
        }
        if (!SpawnDataProperty || SpawnDataProperty->Struct != ValueProperty->Struct)
        {
            UE_LOG(LogTemp, Error, TEXT("[DWM CC] Cannot initialize the saved character: Spawn Character Local returned an unexpected struct."));
            return false;
        }

        FStructOnScope SpawnParameters(SpawnLocalFunction);
        CustomizerPawn->ProcessEvent(SpawnLocalFunction, SpawnParameters.GetStructMemory());
        const void* InitialData = SpawnDataProperty->ContainerPtrToValuePtr<void>(SpawnParameters.GetStructMemory());

        bool bHasPreset = false;
        for (TFieldIterator<FProperty> FieldIt(ValueProperty->Struct); FieldIt; ++FieldIt)
        {
            FProperty* Field = *FieldIt;
            if (Field->GetName().Contains(TEXT("Preset"), ESearchCase::IgnoreCase))
            {
                if (const FObjectPropertyBase* PresetProperty = CastField<FObjectPropertyBase>(Field))
                {
                    const void* PresetAddress = PresetProperty->ContainerPtrToValuePtr<void>(InitialData);
                    bHasPreset = PresetProperty->GetObjectPropertyValue(PresetAddress) != nullptr;
                }
                break;
            }
        }
        if (!bHasPreset)
        {
            UE_LOG(LogTemp, Error, TEXT("[DWM CC] Spawn Character Local produced no preset; leaving the save unchanged."));
            return false;
        }

        FName CharacterName = TEXT("DWM_Player");
        if (UFunction* GetNameFunction = CustomizerPawn->FindFunction(FName(TEXT("Get Character Name"))))
        {
            FStructOnScope NameParameters(GetNameFunction);
            CustomizerPawn->ProcessEvent(GetNameFunction, NameParameters.GetStructMemory());
            for (TFieldIterator<FProperty> PropertyIt(GetNameFunction); PropertyIt; ++PropertyIt)
            {
                const FProperty* Property = *PropertyIt;
                if (Property->HasAnyPropertyFlags(CPF_OutParm))
                {
                    if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
                    {
                        const void* NameAddress = NameProperty->ContainerPtrToValuePtr<void>(NameParameters.GetStructMemory());
                        const FName ReportedName = NameProperty->GetPropertyValue(NameAddress);
                        if (!ReportedName.IsNone())
                        {
                            CharacterName = ReportedName;
                        }
                    }
                    break;
                }
            }
        }

        const int32 NewIndex = SavedCharacters.AddDefaultValue_Invalid_NeedsRehash();
        KeyProperty->SetPropertyValue(SavedCharacters.GetKeyPtr(NewIndex), CharacterName);
        ValueProperty->CopyCompleteValue(SavedCharacters.GetValuePtr(NewIndex), InitialData);
        SavedCharacters.Rehash();

        if (!UGameplayStatics::SaveGameToSlot(SaveGame, SaveSlot, 0))
        {
            UE_LOG(LogTemp, Error, TEXT("[DWM CC] Failed to save the initialized %s record."), SaveSlot);
            return false;
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[DWM CC] Initialized the first saved character '%s' from the asset's default preset."),
            *CharacterName.ToString());

        // Ask the asset to reload the record through its own normal Server → Client path.
        if (UFunction* GetSavedFunction = CustomizerPawn->FindFunction(FName(TEXT("SRV Get Saved Character"))))
        {
            CustomizerPawn->ProcessEvent(GetSavedFunction, nullptr);
        }
        return true;
    }

    void LogRelevantCustomizerObjectProperties(const UObject* Object)
    {
        if (!Object)
        {
            return;
        }

        UE_LOG(LogTemp, Warning, TEXT("[DWM CC] Inspecting %s"), *Object->GetPathName());
        for (TFieldIterator<FProperty> PropertyIt(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
        {
            const FProperty* Property = *PropertyIt;
            const FString Name = Property->GetName();
            if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient) ||
                !(Name.Contains(TEXT("preset"), ESearchCase::IgnoreCase) ||
                  Name.Contains(TEXT("character"), ESearchCase::IgnoreCase) ||
                  Name.Contains(TEXT("save"), ESearchCase::IgnoreCase) ||
                  Name.Contains(TEXT("hair"), ESearchCase::IgnoreCase) ||
                  Name.Contains(TEXT("apparel"), ESearchCase::IgnoreCase) ||
                  Name.Contains(TEXT("data"), ESearchCase::IgnoreCase)))
            {
                continue;
            }

            const void* ValueAddress = Property->ContainerPtrToValuePtr<void>(Object);
            FString ExportedValue;
            Property->ExportTextItem_Direct(ExportedValue, ValueAddress, nullptr, const_cast<UObject*>(Object), PPF_None);
            UE_LOG(LogTemp, Warning, TEXT("[DWM CC] %s.%s (%s) = %s"),
                *Object->GetName(), *Name, *Property->GetCPPType(), *ExportedValue.Left(2000));

            if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
            {
                UE_LOG(LogTemp, Warning, TEXT("[DWM CC]   %s has fields:"), *StructProperty->Struct->GetName());
                for (TFieldIterator<FProperty> FieldIt(StructProperty->Struct); FieldIt; ++FieldIt)
                {
                    const FProperty* Field = *FieldIt;
                    const void* FieldAddress = Field->ContainerPtrToValuePtr<void>(ValueAddress);
                    FString FieldValue;
                    Field->ExportTextItem_Direct(FieldValue, FieldAddress, nullptr, const_cast<UObject*>(Object), PPF_None);
                    UE_LOG(LogTemp, Warning, TEXT("[DWM CC]     %s %s = %s"),
                        *Field->GetCPPType(), *Field->GetName(), *FieldValue.Left(1000));
                }
            }
        }

        for (TFieldIterator<UFunction> FunctionIt(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
        {
            const UFunction* Function = *FunctionIt;
            const FString Name = Function->GetName();
            if (Name.Contains(TEXT("character"), ESearchCase::IgnoreCase) ||
                Name.Contains(TEXT("save"), ESearchCase::IgnoreCase) ||
                Name.Contains(TEXT("preset"), ESearchCase::IgnoreCase) ||
                Name.Contains(TEXT("apparel"), ESearchCase::IgnoreCase) ||
                Name.Contains(TEXT("random"), ESearchCase::IgnoreCase) ||
                Name.Contains(TEXT("confirm"), ESearchCase::IgnoreCase))
            {
                UE_LOG(LogTemp, Warning, TEXT("[DWM CC] %s exposes function %s"),
                    *Object->GetName(), *Name);
                for (TFieldIterator<FProperty> ParameterIt(Function); ParameterIt; ++ParameterIt)
                {
                    const FProperty* Parameter = *ParameterIt;
                    if (Parameter->HasAnyPropertyFlags(CPF_Parm))
                    {
                        FString Direction = Parameter->HasAnyPropertyFlags(CPF_ReturnParm) ? TEXT(" return") :
                            (Parameter->HasAnyPropertyFlags(CPF_OutParm) ? TEXT(" out") : TEXT(" in"));
                        UE_LOG(LogTemp, Warning, TEXT("[DWM CC]   %s %s [%s]"),
                            *Parameter->GetCPPType(), *Parameter->GetName(),
                            *Direction);
                    }
                }
            }
        }
    }

}

// ---------------------------------------------------------------------------
// Init — runs at game startup (before level loads). Read the package here;
// defer spawning to OnStart().
// ---------------------------------------------------------------------------

void UDwmGameInstance::Init()
{
    Super::Init();

    // GameInstance survives OpenLevel. Listen for every completed map load so the
    // Character Creator can hand off cleanly to a normal DWM gameplay world.
    if (!PostLoadMapHandle.IsValid())
    {
        PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
            this, &UDwmGameInstance::HandlePostLoadMap);
    }

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

    // UGameInstance::OnStart runs before the world's GameMode has necessarily been
    // created.  Defer the mode-dependent setup until the world starts ticking so a
    // DWM map that intentionally uses CC_GameMode is detected reliably.
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimerForNextTick(
            FTimerDelegate::CreateUObject(this, &UDwmGameInstance::InitializeWorldRuntime));
    }
}

void UDwmGameInstance::Shutdown()
{
    if (PostLoadMapHandle.IsValid())
    {
        FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
        PostLoadMapHandle.Reset();
    }

    Super::Shutdown();
}

void UDwmGameInstance::HandlePostLoadMap(UWorld* LoadedWorld)
{
    if (!LoadedWorld || LoadedWorld != GetWorld())
    {
        return;
    }

    const FName LoadedMapName = GetStableMapName(LoadedWorld);
    if (IsLevelTransitionWorld())
    {
        // Preserve the community being left while Labgames temporarily owns the world.
        if (IsCommunityMap(LastCommunityMapName))
        {
            PendingArrivalSourceMapName = LastCommunityMapName;
        }
    }
    else if (IsCommunityMap(LoadedMapName))
    {
        // This also supports a direct OpenLevel path that bypasses the intermediate map.
        if (PendingArrivalSourceMapName.IsNone()
            && IsCommunityMap(LastCommunityMapName)
            && LastCommunityMapName != LoadedMapName)
        {
            PendingArrivalSourceMapName = LastCommunityMapName;
        }
        LastCommunityMapName = LoadedMapName;
    }

    LoadedWorld->GetTimerManager().SetTimerForNextTick(
        FTimerDelegate::CreateUObject(this, &UDwmGameInstance::InitializeWorldRuntime));
}

void UDwmGameInstance::InitializeWorldRuntime()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // OnStart and PostLoadMap can both schedule the initial world. Initialize each
    // world once, while still allowing a later OpenLevel destination to initialize.
    if (LastInitializedWorld.Get() == World)
    {
        return;
    }
    LastInitializedWorld = World;

    // The initial PIE/startup map reaches OnStart without a preceding PostLoadMap
    // callback in some editor configurations. Seed the route source here as well.
    const FName CurrentMapName = GetStableMapName(World);
    if (LastCommunityMapName.IsNone() && IsCommunityMap(CurrentMapName))
    {
        LastCommunityMapName = CurrentMapName;
    }

    bCharacterCreatorTransitionQueued = false;
    CharacterCreatorConfirmButton.Reset();
    World->GetTimerManager().ClearTimer(CharacterCreatorTransitionTimer);
    World->GetTimerManager().ClearTimer(TransitionCharacterDisplayTimer);
    World->GetTimerManager().ClearTimer(TransitionArrivalTimer);
    bDemoTradeTerminalSpawned = false;
    DemoTradeTerminalSpawnAttempts = 0;
    TransitionCharacterDisplayAttempts = 0;
    TransitionArrivalAttempts = 0;
    TransitionCharacterDisplay.Reset();

    if (IsLevelTransitionWorld())
    {
        // Labgames owns the travel timing and fade widget in this map. DWM only swaps
        // its abstract rotating mesh for the user's saved CharacterCustomizer model.
        // Do not inject the economy HUD, pendulum, or trade terminal here.
        World->GetTimerManager().SetTimer(
            TransitionCharacterDisplayTimer,
            FTimerDelegate::CreateUObject(this, &UDwmGameInstance::SetupTransitionCharacterDisplay),
            0.05f,
            false);
        return;
    }

    if (!ShouldRunDwmRuntimeSystems())
    {
        UE_LOG(LogTemp, Log,
            TEXT("[DWM] CharacterCustomizer GameMode detected; enabling its UI input and skipping DWM runtime injection."));

        // CC_Customizer_Pawn creates its UI during BeginPlay, then leaves the controller
        // in a game-only input state. Delay until that initialization has completed and
        // explicitly restore game-and-UI input: UMG buttons receive mouse clicks while the
        // pawn still receives its Enhanced Input actions (preset arrows, cursor toggle).
        // The Character Customizer finishes part of its own input setup after
        // BeginPlay. Start this retry sequence fresh whenever a CC level loads,
        // so our focused UMG input wins after the pack's late initialization.
        CharacterCustomizerInputAttempts = 0;
        World->GetTimerManager().SetTimer(
            CharacterCustomizerInputTimer,
            FTimerDelegate::CreateUObject(this, &UDwmGameInstance::EnableCharacterCustomizerInput),
            0.25f,
            false);
        return;
    }

    // CustomSpawn_BP is a placement marker, not gameplay scenery. Keep its editor
    // visualization available while ensuring no marker mesh/arrow appears in PIE.
    for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
    {
        AActor* Candidate = *ActorIt;
        if (Candidate && Candidate->ActorHasTag(FName(TEXT("DWM_TransitionArrival"))))
        {
            Candidate->SetActorHiddenInGame(true);
        }
    }

    // A PlayerController can survive briefly across travel. Explicitly restore the
    // gameplay input state before the first-person pawn and interactive doors take over.
    if (APlayerController* PlayerController = World->GetFirstPlayerController())
    {
        PlayerController->bShowMouseCursor = false;
        PlayerController->bEnableClickEvents = false;
        PlayerController->bEnableMouseOverEvents = false;
        PlayerController->SetInputMode(FInputModeGameOnly());
    }

    if (!PendingArrivalSourceMapName.IsNone())
    {
        // The destination GameMode may create/possess its pawn a little after map load.
        // Retry briefly, then teleport it to the marker tagged for the source community.
        World->GetTimerManager().SetTimer(
            TransitionArrivalTimer,
            FTimerDelegate::CreateUObject(this, &UDwmGameInstance::ApplyRouteSpecificTransitionArrival),
            0.15f,
            false);
    }

    SpawnWorldActors();
    RefreshEconomyState();

    // The terminal is positioned relative to the gameplay pawn, which is not guaranteed to
    // exist during Init(). Deferring one tick keeps the trigger in the loaded world.
    World->GetTimerManager().SetTimerForNextTick(
        FTimerDelegate::CreateUObject(this, &UDwmGameInstance::SpawnDemoTradeTerminal));
}

FName UDwmGameInstance::GetStableMapName(const UWorld* World)
{
    FString MapName = World ? World->GetMapName() : FString();
    const FString StreamingPrefix = World ? World->StreamingLevelsPrefix : FString();
    if (!StreamingPrefix.IsEmpty())
    {
        MapName.RemoveFromStart(StreamingPrefix);
    }
    return FName(*MapName);
}

bool UDwmGameInstance::IsCommunityMap(FName MapName)
{
    static const TSet<FName> CommunityMaps = {
        FName(TEXT("DWM_Mountain")),
        FName(TEXT("DWM_Hillside")),
        FName(TEXT("DWM_Valley")),
        FName(TEXT("DWM_Suburbs")),
        FName(TEXT("DWM_City")),
    };
    return CommunityMaps.Contains(MapName);
}

void UDwmGameInstance::ApplyRouteSpecificTransitionArrival()
{
    UWorld* World = GetWorld();
    if (!World || PendingArrivalSourceMapName.IsNone() || !IsCommunityMap(GetStableMapName(World)))
    {
        return;
    }

    FString SourceName = PendingArrivalSourceMapName.ToString();
    SourceName.RemoveFromStart(TEXT("DWM_"));
    const FName SourceTag(*FString::Printf(TEXT("DWM_From_%s"), *SourceName));

    AActor* ArrivalMarker = nullptr;
    for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
    {
        AActor* Candidate = *ActorIt;
        if (Candidate
            && Candidate->ActorHasTag(FName(TEXT("DWM_TransitionArrival")))
            && Candidate->ActorHasTag(SourceTag))
        {
            ArrivalMarker = Candidate;
            break;
        }
    }

    APlayerController* PlayerController = World->GetFirstPlayerController();
    APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
    if (!ArrivalMarker || !PlayerPawn)
    {
        if (++TransitionArrivalAttempts <= 20)
        {
            World->GetTimerManager().SetTimer(
                TransitionArrivalTimer,
                FTimerDelegate::CreateUObject(this, &UDwmGameInstance::ApplyRouteSpecificTransitionArrival),
                0.25f,
                false);
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[DWM Transition] Could not resolve arrival from %s in %s (marker=%s, pawn=%s)."),
                *PendingArrivalSourceMapName.ToString(),
                *GetStableMapName(World).ToString(),
                ArrivalMarker ? TEXT("yes") : TEXT("no"),
                PlayerPawn ? TEXT("yes") : TEXT("no"));
            PendingArrivalSourceMapName = NAME_None;
        }
        return;
    }

    const FRotator ArrivalRotation = ArrivalMarker->GetActorRotation();
    PlayerPawn->SetActorLocationAndRotation(
        ArrivalMarker->GetActorLocation(), ArrivalRotation, false, nullptr, ETeleportType::TeleportPhysics);
    PlayerPawn->SetActorHiddenInGame(false);
    PlayerPawn->SetActorEnableCollision(true);
    PlayerController->SetControlRotation(ArrivalRotation);

    UE_LOG(LogTemp, Log,
        TEXT("[DWM Transition] Arrived in %s from %s at %s."),
        *GetStableMapName(World).ToString(),
        *PendingArrivalSourceMapName.ToString(),
        *ArrivalMarker->GetActorLocation().ToCompactString());

    PendingArrivalSourceMapName = NAME_None;
    TransitionArrivalAttempts = 0;
}

void UDwmGameInstance::EnableCharacterCustomizerInput()
{
    UWorld* World = GetWorld();
    if (!World || ShouldRunDwmRuntimeSystems())
    {
        // A delayed retry may outlive a level change. Never impose Customizer input
        // mode on a normal DWM gameplay map.
        return;
    }

    APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
    if (!World || !PlayerController)
    {
        if (++CharacterCustomizerInputAttempts <= 5 && World)
        {
            World->GetTimerManager().SetTimer(
                CharacterCustomizerInputTimer,
                FTimerDelegate::CreateUObject(this, &UDwmGameInstance::EnableCharacterCustomizerInput),
                0.25f,
                false);
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[DWM CC] Could not find a PlayerController to enable Customizer UI input."));
        }
        return;
    }

    PlayerController->bShowMouseCursor = true;
    PlayerController->bEnableClickEvents = true;
    PlayerController->bEnableMouseOverEvents = true;

    UUserWidget* CustomizerWidget = nullptr;
    for (TObjectIterator<UUserWidget> WidgetIt; WidgetIt; ++WidgetIt)
    {
        UUserWidget* Widget = *WidgetIt;
        if (Widget && Widget->GetWorld() == World &&
            Widget->GetClass()->GetPathName().Contains(TEXT("Widget_CC_Customization_UI")))
        {
            CustomizerWidget = Widget;
            break;
        }
    }

    FInputModeGameAndUI InputMode;
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InputMode.SetHideCursorDuringCapture(false);
    if (CustomizerWidget)
    {
        InputMode.SetWidgetToFocus(CustomizerWidget->TakeWidget());

        UButton* ConfirmButton = Cast<UButton>(CustomizerWidget->GetWidgetFromName(TEXT("Confirm")));
        if (!ConfirmButton && CustomizerWidget->WidgetTree)
        {
            // Marketplace widgets often give the button an auto-generated name while
            // only the nested TextBlock is named/displayed "Confirm". Resolve either
            // representation so the original asset remains untouched.
            TArray<UWidget*> Widgets;
            CustomizerWidget->WidgetTree->GetAllWidgets(Widgets);
            for (UWidget* Widget : Widgets)
            {
                if (UButton* Button = Cast<UButton>(Widget))
                {
                    if (Button->GetName().Contains(TEXT("Confirm"), ESearchCase::IgnoreCase))
                    {
                        ConfirmButton = Button;
                        break;
                    }
                }
                else if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
                {
                    if (TextBlock->GetText().ToString().Equals(TEXT("Confirm"), ESearchCase::IgnoreCase))
                    {
                        UWidget* Parent = TextBlock->GetParent();
                        while (Parent && !ConfirmButton)
                        {
                            ConfirmButton = Cast<UButton>(Parent);
                            Parent = Parent->GetParent();
                        }
                        if (ConfirmButton)
                        {
                            break;
                        }
                    }
                }
            }
        }

        if (!ConfirmButton)
        {
            // Some of the pack's controls live inside nested UserWidgets. Their internal
            // WidgetTrees are not returned by the parent tree's GetAllWidgets call, so
            // search the live buttons and inspect each button's visual descendants.
            TFunction<bool(UWidget*)> ContainsConfirmText;
            ContainsConfirmText = [&ContainsConfirmText](UWidget* Widget)
            {
                if (const UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
                {
                    return TextBlock->GetText().ToString().Contains(
                        TEXT("Confirm"), ESearchCase::IgnoreCase);
                }

                if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
                {
                    for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
                    {
                        if (ContainsConfirmText(Panel->GetChildAt(ChildIndex)))
                        {
                            return true;
                        }
                    }
                }
                return false;
            };

            for (TObjectIterator<UButton> ButtonIt; ButtonIt; ++ButtonIt)
            {
                UButton* Button = *ButtonIt;
                if (Button && Button->GetWorld() == World &&
                    (Button->GetName().Contains(TEXT("Confirm"), ESearchCase::IgnoreCase) ||
                     Button->GetPathName().Contains(TEXT("ButtonConfirm"), ESearchCase::IgnoreCase) ||
                     ContainsConfirmText(Button)))
                {
                    ConfirmButton = Button;
                    break;
                }
            }
        }

        if (ConfirmButton)
        {
            ConfirmButton->OnClicked.AddUniqueDynamic(
                this, &UDwmGameInstance::HandleCharacterCreatorConfirmed);
            CharacterCreatorConfirmButton = ConfirmButton;
        }
    }
    PlayerController->SetInputMode(InputMode);

    UE_LOG(LogTemp, Log,
        TEXT("[DWM CC] Enabled focused UI input (widget: %s, Confirm transition: %s)."),
        CustomizerWidget ? TEXT("yes") : TEXT("no"),
        CharacterCreatorConfirmButton.IsValid() ? TEXT("bound") : TEXT("not found"));

    // The marketplace pawn can set GameOnly input during its delayed setup. Re-apply
    // the widget focus for a short, bounded period rather than relying on a single
    // timing-sensitive call.
    if (++CharacterCustomizerInputAttempts < 6)
    {
        World->GetTimerManager().SetTimer(
            CharacterCustomizerInputTimer,
            FTimerDelegate::CreateUObject(this, &UDwmGameInstance::EnableCharacterCustomizerInput),
            0.5f,
            false);
    }
}

void UDwmGameInstance::HandleCharacterCreatorConfirmed()
{
    if (bCharacterCreatorTransitionQueued)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World || ShouldRunDwmRuntimeSystems())
    {
        return;
    }

    // Snapshot the live data immediately. The marketplace's own Confirm listener
    // destroys CC_Customizer_Pawn during this same click dispatch.
    CaptureCharacterCreatorSelection();
    bCharacterCreatorTransitionQueued = true;
    UE_LOG(LogTemp, Log,
        TEXT("[DWM CC] Confirm selected; captured the live player and allowing the CharacterCustomizer handler to finish."));

    World->GetTimerManager().SetTimer(
        CharacterCreatorTransitionTimer,
        FTimerDelegate::CreateUObject(this, &UDwmGameInstance::TravelFromCharacterCreatorToMountain),
        0.35f,
        false);
}

void UDwmGameInstance::TravelFromCharacterCreatorToMountain()
{
    UWorld* World = GetWorld();
    if (!World || ShouldRunDwmRuntimeSystems())
    {
        bCharacterCreatorTransitionQueued = false;
        return;
    }

    // CharacterCustomizer writes its SaveGame in its own Confirm handler. Our click
    // listener intentionally waits before travelling, so refresh from that completed
    // save now instead of relying on the earlier live struct (whose Preset is not yet
    // populated even though the assembled body is already visible).
    RefreshCapturedCharacterSelectionFromSave();

    static const FName MountainMap(TEXT("/Game/DWM/Maps/DWM_Mountain"));
    UE_LOG(LogTemp, Log, TEXT("[DWM CC] Loading %s."), *MountainMap.ToString());
    UGameplayStatics::OpenLevel(World, MountainMap);
}

bool UDwmGameInstance::RefreshCapturedCharacterSelectionFromSave()
{
    constexpr TCHAR SaveSlot[] = TEXT("CC_SaveGame");
    USaveGame* CompletedSave = UGameplayStatics::LoadGameFromSlot(SaveSlot, 0);
    if (!CompletedSave)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DWM CC] Could not reload %s after Confirm; keeping the live transition snapshot."),
            SaveSlot);
        return false;
    }

    FMapProperty* SavedCharactersProperty = FindFProperty<FMapProperty>(
        CompletedSave->GetClass(), FName(TEXT("Saved Characters")));
    FNameProperty* KeyProperty = SavedCharactersProperty
        ? CastField<FNameProperty>(SavedCharactersProperty->KeyProp) : nullptr;
    FStructProperty* ValueProperty = SavedCharactersProperty
        ? CastField<FStructProperty>(SavedCharactersProperty->ValueProp) : nullptr;
    if (!SavedCharactersProperty || !KeyProperty || !ValueProperty)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DWM CC] The completed CharacterCustomizer save has an unexpected schema."));
        return false;
    }

    FScriptMapHelper CompletedCharacters(SavedCharactersProperty,
        SavedCharactersProperty->ContainerPtrToValuePtr<void>(CompletedSave));
    if (CompletedCharacters.Num() == 0)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DWM CC] CharacterCustomizer completed Confirm without a saved character record."));
        return false;
    }

    // The customizer currently edits one active record. If more records are added for
    // NPC authoring later, prefer the record whose apparel and hair arrays match the
    // live character captured at the instant Confirm was clicked.
    TArray<FName> LiveApparelNames;
    TArray<FName> LiveHairNames;
    const auto ReadSelectionNames = [](const void* SavedData, const UScriptStruct* SavedDataStruct,
        const TCHAR* NameToken, TArray<FName>& OutNames)
    {
        if (!SavedData || !SavedDataStruct)
        {
            return;
        }

        for (TFieldIterator<FProperty> PropertyIt(SavedDataStruct); PropertyIt; ++PropertyIt)
        {
            FProperty* Property = *PropertyIt;
            FString NormalizedName = Property->GetName().ToLower();
            NormalizedName.ReplaceInline(TEXT("_"), TEXT(""));
            NormalizedName.ReplaceInline(TEXT(" "), TEXT(""));
            if (!NormalizedName.Contains(NameToken))
            {
                continue;
            }

            FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
            FNameProperty* NameProperty = ArrayProperty
                ? CastField<FNameProperty>(ArrayProperty->Inner) : nullptr;
            if (!ArrayProperty || !NameProperty)
            {
                return;
            }

            FScriptArrayHelper ArrayHelper(
                ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(SavedData));
            for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
            {
                OutNames.Add(NameProperty->GetPropertyValue(ArrayHelper.GetRawPtr(Index)));
            }
            return;
        }
    };

    if (SelectedCharacterSnapshot)
    {
        FMapProperty* SnapshotMapProperty = FindFProperty<FMapProperty>(
            SelectedCharacterSnapshot->GetClass(), FName(TEXT("Saved Characters")));
        if (SnapshotMapProperty)
        {
            FScriptMapHelper SnapshotCharacters(SnapshotMapProperty,
                SnapshotMapProperty->ContainerPtrToValuePtr<void>(SelectedCharacterSnapshot));
            for (int32 Index = 0; Index < SnapshotCharacters.GetMaxIndex(); ++Index)
            {
                if (SnapshotCharacters.IsValidIndex(Index))
                {
                    ReadSelectionNames(SnapshotCharacters.GetValuePtr(Index), ValueProperty->Struct,
                        TEXT("apparelnames"), LiveApparelNames);
                    ReadSelectionNames(SnapshotCharacters.GetValuePtr(Index), ValueProperty->Struct,
                        TEXT("hairnames"), LiveHairNames);
                    break;
                }
            }
        }
    }

    int32 SelectedIndex = INDEX_NONE;
    int32 BestScore = MIN_int32;
    for (int32 Index = 0; Index < CompletedCharacters.GetMaxIndex(); ++Index)
    {
        if (!CompletedCharacters.IsValidIndex(Index))
        {
            continue;
        }

        TArray<FName> CandidateApparelNames;
        TArray<FName> CandidateHairNames;
        ReadSelectionNames(CompletedCharacters.GetValuePtr(Index), ValueProperty->Struct,
            TEXT("apparelnames"), CandidateApparelNames);
        ReadSelectionNames(CompletedCharacters.GetValuePtr(Index), ValueProperty->Struct,
            TEXT("hairnames"), CandidateHairNames);

        int32 Score = 0;
        if (LiveApparelNames.Num() > 0 && CandidateApparelNames == LiveApparelNames)
        {
            Score += 100;
        }
        if (LiveHairNames.Num() > 0 && CandidateHairNames == LiveHairNames)
        {
            Score += 100;
        }
        for (const FName Name : LiveApparelNames)
        {
            Score += CandidateApparelNames.Contains(Name) ? 1 : 0;
        }
        for (const FName Name : LiveHairNames)
        {
            Score += CandidateHairNames.Contains(Name) ? 1 : 0;
        }

        if (SelectedIndex == INDEX_NONE || Score > BestScore)
        {
            SelectedIndex = Index;
            BestScore = Score;
        }
    }

    if (SelectedIndex == INDEX_NONE)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DWM CC] Could not select the completed player appearance record."));
        return false;
    }

    USaveGame* Snapshot = DuplicateObject<USaveGame>(CompletedSave, this);
    if (!Snapshot)
    {
        return false;
    }

    FScriptMapHelper SnapshotCharacters(SavedCharactersProperty,
        SavedCharactersProperty->ContainerPtrToValuePtr<void>(Snapshot));
    SnapshotCharacters.EmptyValues();
    const int32 SnapshotIndex = SnapshotCharacters.AddDefaultValue_Invalid_NeedsRehash();
    KeyProperty->SetPropertyValue(SnapshotCharacters.GetKeyPtr(SnapshotIndex), FName(TEXT("DWM_Player")));
    ValueProperty->CopyCompleteValue(
        SnapshotCharacters.GetValuePtr(SnapshotIndex),
        CompletedCharacters.GetValuePtr(SelectedIndex));
    SnapshotCharacters.Rehash();
    SelectedCharacterSnapshot = Snapshot;

    if (!bLiveAppearanceAssetsCaptured)
    {
        CaptureSelectedCharacterAssets(
            SnapshotCharacters.GetValuePtr(SnapshotIndex), ValueProperty->Struct);
    }
    else
    {
        UE_LOG(LogTemp, Log,
            TEXT("[DWM CC] Preserving the exact apparel and hair resolved from the live Confirm state; the completed save is retained only as the player-data snapshot."));
    }

    UE_LOG(LogTemp, Log,
        TEXT("[DWM CC] Refreshed the confirmed player preset, apparel, and hair from the completed save before travel."));
    return true;
}

void UDwmGameInstance::CaptureCharacterCreatorSelection()
{
    SelectedCharacterSnapshot = nullptr;
    CapturedCharacterMeshes.Reset();
    bLiveAppearanceAssetsCaptured = false;

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    APawn* CustomizerPawn = nullptr;
    for (TActorIterator<APawn> PawnIt(World); PawnIt; ++PawnIt)
    {
        APawn* Pawn = *PawnIt;
        if (Pawn && Pawn->GetClass()->GetPathName().Contains(TEXT("CC_Customizer_Pawn")))
        {
            CustomizerPawn = Pawn;
            break;
        }
    }

    if (!CustomizerPawn)
    {
        UE_LOG(LogTemp, Error, TEXT("[DWM CC] Could not find the live Customizer Pawn; player appearance was not captured."));
        return;
    }

    UObject* LiveCharacter = nullptr;
    if (FObjectPropertyBase* CharacterProperty = FindFProperty<FObjectPropertyBase>(
        CustomizerPawn->GetClass(), FName(TEXT("Customizable Character"))))
    {
        LiveCharacter = CharacterProperty->GetObjectPropertyValue(
            CharacterProperty->ContainerPtrToValuePtr<void>(CustomizerPawn));
    }

    if (AActor* LiveCharacterActor = Cast<AActor>(LiveCharacter))
    {
        CaptureVisibleCharacterMeshes(LiveCharacterActor, CustomizerPawn);
    }
    else
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DWM CC] The live customizable character actor was not available at Confirm; no transition mesh snapshot was captured."));
    }

    constexpr TCHAR SaveSlot[] = TEXT("CC_SaveGame");
    USaveGame* SaveTemplate = UGameplayStatics::LoadGameFromSlot(SaveSlot, 0);
    if (!SaveTemplate)
    {
        UE_LOG(LogTemp, Error, TEXT("[DWM CC] Could not load %s as a schema template; player appearance was not captured."), SaveSlot);
        return;
    }

    FMapProperty* SavedCharactersProperty = FindFProperty<FMapProperty>(
        SaveTemplate->GetClass(), FName(TEXT("Saved Characters")));
    FNameProperty* KeyProperty = SavedCharactersProperty
        ? CastField<FNameProperty>(SavedCharactersProperty->KeyProp) : nullptr;
    FStructProperty* ValueProperty = SavedCharactersProperty
        ? CastField<FStructProperty>(SavedCharactersProperty->ValueProp) : nullptr;
    if (!SavedCharactersProperty || !KeyProperty || !ValueProperty)
    {
        UE_LOG(LogTemp, Error, TEXT("[DWM CC] CharacterCustomizer save schema was not recognized; player appearance was not captured."));
        return;
    }

    const void* LiveAppearanceData = nullptr;
    TUniquePtr<FStructOnScope> FunctionParameters;

    // Read the actor that is already assembled and visible first. Calling Spawn Character
    // Local during the marketplace Confirm click re-enters its widget graph and can produce
    // a null Preset (the Get_Preset_Preset errors seen in PIE).
    const FName CandidateNames[] = {
        FName(TEXT("Local Data")),
        FName(TEXT("CC_Loaded_Variables"))
    };
    UObject* CandidateObjects[] = { LiveCharacter, CustomizerPawn };
    for (UObject* CandidateObject : CandidateObjects)
    {
        if (!CandidateObject || LiveAppearanceData)
        {
            continue;
        }
        for (const FName CandidateName : CandidateNames)
        {
            if (FStructProperty* StructProperty = FindFProperty<FStructProperty>(
                CandidateObject->GetClass(), CandidateName))
            {
                if (StructProperty->Struct == ValueProperty->Struct)
                {
                    LiveAppearanceData = StructProperty->ContainerPtrToValuePtr<void>(CandidateObject);
                    break;
                }
            }
        }
    }

    // Last resort only: use the pack's serializer if its live actor did not expose the
    // compatible data. The exact visual mesh snapshot above is independent of this path.
    if (!LiveAppearanceData)
    {
        if (UFunction* SpawnLocalFunction = CustomizerPawn->FindFunction(FName(TEXT("Spawn Character Local"))))
        {
            FunctionParameters = MakeUnique<FStructOnScope>(SpawnLocalFunction);
            CustomizerPawn->ProcessEvent(SpawnLocalFunction, FunctionParameters->GetStructMemory());
            for (TFieldIterator<FProperty> PropertyIt(SpawnLocalFunction); PropertyIt; ++PropertyIt)
            {
                FStructProperty* StructProperty = CastField<FStructProperty>(*PropertyIt);
                if (StructProperty &&
                    StructProperty->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm) &&
                    StructProperty->Struct == ValueProperty->Struct)
                {
                    LiveAppearanceData = StructProperty->ContainerPtrToValuePtr<void>(
                        FunctionParameters->GetStructMemory());
                    break;
                }
            }
        }
    }

    if (!LiveAppearanceData)
    {
        UE_LOG(LogTemp, Error, TEXT("[DWM CC] The live customized appearance could not be read; the transition will keep its normal fallback image."));
        return;
    }

    USaveGame* Snapshot = DuplicateObject<USaveGame>(SaveTemplate, this);
    if (!Snapshot)
    {
        UE_LOG(LogTemp, Error, TEXT("[DWM CC] Could not allocate the transient player-appearance snapshot."));
        return;
    }

    FScriptMapHelper SnapshotCharacters(SavedCharactersProperty,
        SavedCharactersProperty->ContainerPtrToValuePtr<void>(Snapshot));
    SnapshotCharacters.EmptyValues();
    const int32 SnapshotIndex = SnapshotCharacters.AddDefaultValue_Invalid_NeedsRehash();
    KeyProperty->SetPropertyValue(SnapshotCharacters.GetKeyPtr(SnapshotIndex), FName(TEXT("DWM_Player")));
    ValueProperty->CopyCompleteValue(SnapshotCharacters.GetValuePtr(SnapshotIndex), LiveAppearanceData);
    SnapshotCharacters.Rehash();
    SelectedCharacterSnapshot = Snapshot;

    // The live struct can legitimately have a null Preset until CharacterCustomizer's
    // own Confirm listener finishes. Resolve its current apparel/hair row names through
    // the pawn's active authored preset rather than falling back to an older SaveGame row.
    UObject* FallbackPreset = nullptr;
    const FName PresetPropertyNames[] = {
        FName(TEXT("Preset")),
        FName(TEXT("Default Preset"))
    };
    UObject* PresetOwners[] = { LiveCharacter, CustomizerPawn };
    for (UObject* PresetOwner : PresetOwners)
    {
        if (!PresetOwner || FallbackPreset)
        {
            continue;
        }

        for (const FName PresetPropertyName : PresetPropertyNames)
        {
            if (FObjectPropertyBase* PresetProperty = FindFProperty<FObjectPropertyBase>(
                PresetOwner->GetClass(), PresetPropertyName))
            {
                FallbackPreset = PresetProperty->LoadObjectPropertyValue(
                    PresetProperty->ContainerPtrToValuePtr<void>(PresetOwner));
            }
            if (FallbackPreset)
            {
                break;
            }
        }
    }

    const int32 MeshCountBeforeSelection = CapturedCharacterMeshes.Num();
    CaptureSelectedCharacterAssets(
        SnapshotCharacters.GetValuePtr(SnapshotIndex), ValueProperty->Struct, FallbackPreset);
    bLiveAppearanceAssetsCaptured = CapturedCharacterMeshes.Num() > MeshCountBeforeSelection;

    UE_LOG(LogTemp, Log,
        TEXT("[DWM CC] Captured the live customized player appearance for later transitions (NPC save records were not used)."));
}

void UDwmGameInstance::CaptureSelectedCharacterAssets(
    const void* SavedData, const UScriptStruct* SavedDataStruct, UObject* FallbackPreset)
{
    if (!SavedData || !SavedDataStruct)
    {
        return;
    }

    UObject* Preset = nullptr;
    TArray<FName> ApparelNames;
    TArray<FName> HairNames;

    for (TFieldIterator<FProperty> PropertyIt(SavedDataStruct); PropertyIt; ++PropertyIt)
    {
        FProperty* Property = *PropertyIt;
        FString NormalizedName = Property->GetName().ToLower();
        NormalizedName.ReplaceInline(TEXT("_"), TEXT(""));
        NormalizedName.ReplaceInline(TEXT(" "), TEXT(""));
        const void* ValueAddress = Property->ContainerPtrToValuePtr<void>(SavedData);

        if (!Preset && NormalizedName.Contains(TEXT("preset")))
        {
            if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
            {
                Preset = ObjectProperty->LoadObjectPropertyValue(ValueAddress);
            }
        }

        FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
        FNameProperty* NameProperty = ArrayProperty
            ? CastField<FNameProperty>(ArrayProperty->Inner) : nullptr;
        if (!ArrayProperty || !NameProperty)
        {
            continue;
        }

        TArray<FName>* Destination = nullptr;
        if (NormalizedName.Contains(TEXT("apparelnames")))
        {
            Destination = &ApparelNames;
        }
        else if (NormalizedName.Contains(TEXT("hairnames")))
        {
            Destination = &HairNames;
        }
        if (!Destination)
        {
            continue;
        }

        FScriptArrayHelper ArrayHelper(
            ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(SavedData));
        for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
        {
            Destination->Add(NameProperty->GetPropertyValue(ArrayHelper.GetRawPtr(Index)));
        }
    }

    if (!Preset)
    {
        Preset = FallbackPreset;
    }

    if (!Preset)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DWM CC] Confirmed appearance has no loadable Preset; apparel tables cannot be resolved."));
        return;
    }

    TArray<UDataTable*> ApparelTables;
    TArray<UDataTable*> HairTables;
    const auto AddTable = [](TArray<UDataTable*>& Tables, UObject* Candidate)
    {
        if (UDataTable* Table = Cast<UDataTable>(Candidate))
        {
            Tables.AddUnique(Table);
        }
    };

    for (TFieldIterator<FProperty> PropertyIt(Preset->GetClass(), EFieldIteratorFlags::IncludeSuper);
         PropertyIt; ++PropertyIt)
    {
        FProperty* Property = *PropertyIt;
        FString NormalizedName = Property->GetName().ToLower();
        NormalizedName.ReplaceInline(TEXT("_"), TEXT(""));
        NormalizedName.ReplaceInline(TEXT(" "), TEXT(""));

        TArray<UDataTable*>* Destination = nullptr;
        if (NormalizedName.Contains(TEXT("dtapparel")))
        {
            Destination = &ApparelTables;
        }
        else if (NormalizedName.Contains(TEXT("dthair")))
        {
            Destination = &HairTables;
        }
        if (!Destination)
        {
            continue;
        }

        void* ValueAddress = Property->ContainerPtrToValuePtr<void>(Preset);
        if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        {
            AddTable(*Destination, ObjectProperty->LoadObjectPropertyValue(ValueAddress));
            continue;
        }

        FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
        FObjectPropertyBase* InnerObjectProperty = ArrayProperty
            ? CastField<FObjectPropertyBase>(ArrayProperty->Inner) : nullptr;
        if (!ArrayProperty || !InnerObjectProperty)
        {
            continue;
        }

        FScriptArrayHelper ArrayHelper(ArrayProperty, ValueAddress);
        for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
        {
            AddTable(*Destination,
                InnerObjectProperty->LoadObjectPropertyValue(ArrayHelper.GetRawPtr(Index)));
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[DWM CC] Resolving %d apparel and %d hair selections through preset %s (%d apparel tables, %d hair tables)."),
        ApparelNames.Num(), HairNames.Num(), *Preset->GetPathName(),
        ApparelTables.Num(), HairTables.Num());

    int32 VisibleBodyMeshIndex = CapturedCharacterMeshes.IndexOfByPredicate(
        [](const FDwmCapturedCharacterMesh& Candidate)
        {
            return Candidate.SkeletalMesh &&
                Candidate.SourceComponentName.IsEqual(FName(TEXT("Body")), ENameCase::IgnoreCase);
        });
    if (VisibleBodyMeshIndex == INDEX_NONE && !CapturedCharacterMeshes.IsEmpty())
    {
        VisibleBodyMeshIndex = 0;
    }

    const auto CaptureRows = [this, VisibleBodyMeshIndex](
        const TArray<FName>& SelectedNames,
        const TArray<UDataTable*>& Tables,
        bool bHair)
    {
        for (const FName SelectedName : SelectedNames)
        {
            if (SelectedName.IsNone() || SelectedName.ToString().Contains(TEXT("Default")))
            {
                continue;
            }

            USkeletalMesh* SelectedMesh = nullptr;
            for (UDataTable* Table : Tables)
            {
                uint8* RowData = Table ? Table->FindRowUnchecked(SelectedName) : nullptr;
                const UScriptStruct* RowStruct = Table ? Table->GetRowStruct() : nullptr;
                if (!RowData || !RowStruct)
                {
                    continue;
                }

                for (TFieldIterator<FProperty> RowPropertyIt(RowStruct); RowPropertyIt; ++RowPropertyIt)
                {
                    FProperty* RowProperty = *RowPropertyIt;
                    FString RowPropertyName = RowProperty->GetName().ToLower();
                    RowPropertyName.ReplaceInline(TEXT("_"), TEXT(""));
                    if (!RowPropertyName.StartsWith(TEXT("mesh")))
                    {
                        continue;
                    }

                    if (FObjectPropertyBase* MeshProperty =
                        CastField<FObjectPropertyBase>(RowProperty))
                    {
                        SelectedMesh = Cast<USkeletalMesh>(MeshProperty->LoadObjectPropertyValue(
                            MeshProperty->ContainerPtrToValuePtr<void>(RowData)));
                    }
                    if (SelectedMesh)
                    {
                        break;
                    }
                }

                if (SelectedMesh)
                {
                    break;
                }
            }

            if (!SelectedMesh)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[DWM CC] Could not resolve selected %s row %s to a skeletal mesh."),
                    bHair ? TEXT("hair") : TEXT("apparel"), *SelectedName.ToString());
                continue;
            }

            // This CharacterCustomizer pack prevents the female torso from protruding
            // through upper-body garments with a body-only morph. The garments themselves
            // (including TShirt_01) contain no morph targets, so copying body curves to the
            // garment cannot reproduce the asset's masking behavior in the transition map.
            // Apply the pack's authored shrink morph to the captured body whenever an
            // upper-body apparel row is present.
            if (!bHair &&
                SelectedMesh->GetPathName().Contains(TEXT("/UpperBody/"), ESearchCase::IgnoreCase) &&
                CapturedCharacterMeshes.IsValidIndex(VisibleBodyMeshIndex))
            {
                CapturedCharacterMeshes[VisibleBodyMeshIndex].MorphTargets.Add(
                    FName(TEXT("mod_breast_shrink")), 1.0f);
                UE_LOG(LogTemp, Log,
                    TEXT("[DWM CC] Applied mod_breast_shrink to the transition body for upper-body apparel %s."),
                    *SelectedName.ToString());
            }

            FDwmCapturedCharacterMesh& Captured = CapturedCharacterMeshes.AddDefaulted_GetRef();
            Captured.SourceComponentName = SelectedName;
            Captured.SkeletalMesh = SelectedMesh;
            Captured.RelativeTransform = FTransform::Identity;

            if (bHair)
            {
                // Hair's AnimBP copies the pose from its attached skeletal parent. Attach
                // it to the visible Body, not CharacterMesh0's hidden animation dummy.
                Captured.AttachmentMeshIndex = VisibleBodyMeshIndex;
                Captured.AnimClass = LoadClass<UAnimInstance>(nullptr,
                    TEXT("/Game/CharacterCustomizer/Characters/CCMH/Hair/CC_Hair_AnimBP.CC_Hair_AnimBP_C"));
            }
            else
            {
                // The Body may itself follow CharacterMesh0, but using Body as the direct
                // leader keeps apparel on the exact visible pose and reference skeleton.
                Captured.LeaderMeshIndex = VisibleBodyMeshIndex;
                // Apparel uses LeaderPose, but it is attached to the display actor's
                // capsule root. It must therefore share the leader component's relative
                // transform (normally the character-mesh Z offset and rotation). Leaving
                // it at identity places the clothing roughly one capsule half-height
                // above the body even though the bones themselves are following correctly.
                if (CapturedCharacterMeshes.IsValidIndex(Captured.LeaderMeshIndex))
                {
                    const FDwmCapturedCharacterMesh& BodyCapture =
                        CapturedCharacterMeshes[Captured.LeaderMeshIndex];
                    Captured.RelativeTransform = BodyCapture.RelativeTransform;

                    // CharacterCustomizer's body proportions are morph targets, while
                    // LeaderPose only shares bone transforms. Apply the same morph values
                    // to compatible apparel meshes so the selected clothes follow the
                    // customized torso instead of exposing the unmorphed body beneath.
                    Captured.MorphTargets = BodyCapture.MorphTargets;
                }
            }

            for (const FSkeletalMaterial& SkeletalMaterial : SelectedMesh->GetMaterials())
            {
                Captured.Materials.Add(SkeletalMaterial.MaterialInterface);
            }

            UE_LOG(LogTemp, Log,
                TEXT("[DWM CC] Resolved selected %s %s to %s."),
                bHair ? TEXT("hair") : TEXT("apparel"),
                *SelectedName.ToString(), *SelectedMesh->GetPathName());
        }
    };

    CaptureRows(ApparelNames, ApparelTables, false);
    CaptureRows(HairNames, HairTables, true);
}

void UDwmGameInstance::CaptureVisibleCharacterMeshes(AActor* LiveCharacterActor, UObject* CustomizerContext)
{
    CapturedCharacterMeshes.Reset();
    if (!LiveCharacterActor)
    {
        return;
    }

    // CharacterCustomizer does not keep every assembled part as an owned component on the
    // visible character actor. Apparel and hair can be runtime components referenced by a
    // Blueprint array (for example CharacterMesh0), or components owned by an attached
    // helper actor. Collect all three forms so the transition snapshot represents what the
    // player actually confirmed instead of only the naked body components.
    TArray<UMeshComponent*> SourceMeshes;
    TSet<UMeshComponent*> UniqueSourceMeshes;

    const auto AddMesh = [&SourceMeshes, &UniqueSourceMeshes](UMeshComponent* Mesh)
    {
        if (Mesh && !UniqueSourceMeshes.Contains(Mesh))
        {
            UniqueSourceMeshes.Add(Mesh);
            SourceMeshes.Add(Mesh);
        }
    };

    const auto AddOwnedMeshes = [&AddMesh](AActor* Actor)
    {
        if (!Actor)
        {
            return;
        }

        TArray<UMeshComponent*> ActorMeshes;
        Actor->GetComponents<UMeshComponent>(ActorMeshes);
        for (UMeshComponent* Mesh : ActorMeshes)
        {
            AddMesh(Mesh);
        }
    };

    const auto AddReflectedMeshReferences = [&AddMesh](UObject* Object)
    {
        if (!Object)
        {
            return;
        }

        for (TFieldIterator<FProperty> PropertyIt(Object->GetClass(), EFieldIteratorFlags::IncludeSuper);
             PropertyIt; ++PropertyIt)
        {
            FProperty* Property = *PropertyIt;
            if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
            {
                if (ObjectProperty->PropertyClass &&
                    ObjectProperty->PropertyClass->IsChildOf(UMeshComponent::StaticClass()))
                {
                    AddMesh(Cast<UMeshComponent>(ObjectProperty->GetObjectPropertyValue(
                        ObjectProperty->ContainerPtrToValuePtr<void>(Object))));
                }
                continue;
            }

            FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
            FObjectPropertyBase* InnerObjectProperty = ArrayProperty
                ? CastField<FObjectPropertyBase>(ArrayProperty->Inner) : nullptr;
            if (!ArrayProperty || !InnerObjectProperty || !InnerObjectProperty->PropertyClass ||
                !InnerObjectProperty->PropertyClass->IsChildOf(UMeshComponent::StaticClass()))
            {
                continue;
            }

            FScriptArrayHelper ArrayHelper(
                ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Object));
            for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
            {
                AddMesh(Cast<UMeshComponent>(InnerObjectProperty->GetObjectPropertyValue(
                    ArrayHelper.GetRawPtr(Index))));
            }
        }
    };

    AddOwnedMeshes(LiveCharacterActor);

    TArray<AActor*> AttachedActors;
    LiveCharacterActor->GetAttachedActors(AttachedActors, true, true);
    for (AActor* AttachedActor : AttachedActors)
    {
        AddOwnedMeshes(AttachedActor);
        AddReflectedMeshReferences(AttachedActor);
    }

    AddReflectedMeshReferences(LiveCharacterActor);
    AddReflectedMeshReferences(CustomizerContext);

    // The pack's apparel graph spawns some selected parts as independent skeletal-mesh
    // actors at the character's location. They are intentionally not attached to the
    // customizable-character actor and are held only in Blueprint-local variables, so
    // neither ownership nor reflected-property traversal can find them. Limit this scan
    // to visible skeletal meshes inside the live character's bounds: it catches the
    // confirmed shirt, trousers, footwear, eyebrows and hairstyle without collecting the
    // creator's cameras, lights or stage geometry.
    const FBox CharacterBounds = LiveCharacterActor->GetComponentsBoundingBox(true);
    const FVector CharacterCenter = CharacterBounds.IsValid
        ? CharacterBounds.GetCenter() : LiveCharacterActor->GetActorLocation();
    constexpr float CharacterPartSearchRadius = 250.0f;
    for (TActorIterator<AActor> ActorIt(LiveCharacterActor->GetWorld()); ActorIt; ++ActorIt)
    {
        AActor* CandidateActor = *ActorIt;
        if (!CandidateActor)
        {
            continue;
        }

        TArray<USkeletalMeshComponent*> CandidateMeshes;
        CandidateActor->GetComponents<USkeletalMeshComponent>(CandidateMeshes);
        for (USkeletalMeshComponent* CandidateMesh : CandidateMeshes)
        {
            if (!CandidateMesh || !CandidateMesh->GetSkeletalMeshAsset() ||
                !CandidateMesh->IsVisible() || CandidateMesh->bHiddenInGame)
            {
                continue;
            }

            const FVector CandidateCenter = CandidateMesh->Bounds.Origin;
            if (FVector::DistSquared(CandidateCenter, CharacterCenter) <=
                FMath::Square(CharacterPartSearchRadius))
            {
                const int32 PreviousCount = SourceMeshes.Num();
                AddMesh(CandidateMesh);
                if (SourceMeshes.Num() != PreviousCount && CandidateActor != LiveCharacterActor)
                {
                    UE_LOG(LogTemp, Log,
                        TEXT("[DWM CC] Found nearby assembled character part %s on %s, asset %s."),
                        *CandidateMesh->GetName(), *CandidateActor->GetName(),
                        *GetNameSafe(CandidateMesh->GetSkeletalMeshAsset()));
                }
            }
        }
    }

    TArray<UMeshComponent*> CapturedSources;

    for (UMeshComponent* SourceMesh : SourceMeshes)
    {
        if (!SourceMesh || !SourceMesh->IsVisible() || SourceMesh->bHiddenInGame)
        {
            if (SourceMesh)
            {
                UE_LOG(LogTemp, Verbose,
                    TEXT("[DWM CC] Skipped hidden transition component %s owned by %s."),
                    *SourceMesh->GetName(), *GetNameSafe(SourceMesh->GetOwner()));
            }
            continue;
        }

        USkeletalMeshComponent* SourceSkeletal = Cast<USkeletalMeshComponent>(SourceMesh);
        UStaticMeshComponent* SourceStatic = Cast<UStaticMeshComponent>(SourceMesh);
        if ((!SourceSkeletal || !SourceSkeletal->GetSkeletalMeshAsset()) &&
            (!SourceStatic || !SourceStatic->GetStaticMesh()))
        {
            continue;
        }

        FDwmCapturedCharacterMesh& Captured = CapturedCharacterMeshes.AddDefaulted_GetRef();
        Captured.SourceComponentName = SourceMesh->GetFName();
        Captured.RelativeTransform = SourceMesh->GetComponentTransform().GetRelativeTransform(
            LiveCharacterActor->GetActorTransform());
        Captured.SkeletalMesh = SourceSkeletal ? SourceSkeletal->GetSkeletalMeshAsset() : nullptr;
        Captured.StaticMesh = SourceStatic ? SourceStatic->GetStaticMesh() : nullptr;
        Captured.AnimClass = SourceSkeletal ? SourceSkeletal->GetAnimClass() : nullptr;

        const int32 MaterialCount = SourceMesh->GetNumMaterials();
        Captured.Materials.Reserve(MaterialCount);
        for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
        {
            UMaterialInterface* Material = SourceMesh->GetMaterial(MaterialIndex);
            if (UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(Material))
            {
                Material = DuplicateObject<UMaterialInstanceDynamic>(DynamicMaterial, this);
            }
            Captured.Materials.Add(Material);
        }

        if (SourceSkeletal)
        {
            Captured.MorphTargets = SourceSkeletal->GetMorphTargetCurves();

            Captured.MaterialSectionVisibilityLOD0.Reserve(MaterialCount);
            for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
            {
                Captured.MaterialSectionVisibilityLOD0.Add(
                    SourceSkeletal->IsMaterialSectionShown(MaterialIndex, 0) ? 1 : 0);
            }

            const int32 BoneCount = SourceSkeletal->GetNumBones();
            for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
            {
                const FName BoneName = SourceSkeletal->GetBoneName(BoneIndex);
                if (!BoneName.IsNone() && SourceSkeletal->IsBoneHiddenByName(BoneName))
                {
                    Captured.HiddenBones.Add(BoneName);
                }
            }
        }
        CapturedSources.Add(SourceMesh);

        int32 HiddenSectionCount = 0;
        for (const uint8 bSectionVisible : Captured.MaterialSectionVisibilityLOD0)
        {
            HiddenSectionCount += bSectionVisible == 0 ? 1 : 0;
        }

        UE_LOG(LogTemp, Log,
            TEXT("[DWM CC] Transition component %d: %s owned by %s, asset %s, hidden sections=%d, hidden bones=%d."),
            CapturedSources.Num() - 1,
            *SourceMesh->GetName(),
            *GetNameSafe(SourceMesh->GetOwner()),
            SourceSkeletal
                ? *GetNameSafe(SourceSkeletal->GetSkeletalMeshAsset())
                : *GetNameSafe(SourceStatic ? SourceStatic->GetStaticMesh() : nullptr),
            HiddenSectionCount,
            Captured.HiddenBones.Num());
    }

    for (int32 CapturedIndex = 0; CapturedIndex < CapturedCharacterMeshes.Num(); ++CapturedIndex)
    {
        const USkeletalMeshComponent* SourceSkeletal =
            Cast<USkeletalMeshComponent>(CapturedSources[CapturedIndex]);
        if (SourceSkeletal && SourceSkeletal->LeaderPoseComponent.IsValid())
        {
            CapturedCharacterMeshes[CapturedIndex].LeaderMeshIndex =
                CapturedSources.IndexOfByKey(SourceSkeletal->LeaderPoseComponent.Get());
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[DWM CC] Captured %d assembled visible mesh components for transition imagery."),
        CapturedCharacterMeshes.Num());
}

bool UDwmGameInstance::IsLevelTransitionWorld() const
{
    const UWorld* World = GetWorld();
    const UPackage* Package = World ? World->GetOutermost() : nullptr;
    if (!Package)
    {
        return false;
    }

    // PIE inserts UEDPIE_<instance>_ between the package directory and asset name,
    // for example:
    // /Game/Level_Transition/Maps/Transition/UEDPIE_0_Level_Transition_Updated.
    // Match the stable directory and asset-name portions separately so the transition
    // character setup also runs in the editor, not only in standalone/headless games.
    const FString PackageName = Package->GetName();
    return PackageName.Contains(TEXT("/Level_Transition/Maps/Transition/")) &&
        PackageName.Contains(TEXT("Level_Transition_Updated"));
}

bool UDwmGameInstance::ApplySavedCharacterAppearance(UObject* CharacterObject)
{
    if (!CharacterObject)
    {
        return false;
    }

    USaveGame* SaveGame = SelectedCharacterSnapshot;
    if (!SaveGame)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[DWM Transition] No player appearance was captured in Character Creator; keeping the transition fallback."));
        return false;
    }

    FMapProperty* SavedCharactersProperty = FindFProperty<FMapProperty>(
        SaveGame->GetClass(), FName(TEXT("Saved Characters")));
    FNameProperty* KeyProperty = SavedCharactersProperty
        ? CastField<FNameProperty>(SavedCharactersProperty->KeyProp) : nullptr;
    FStructProperty* ValueProperty = SavedCharactersProperty
        ? CastField<FStructProperty>(SavedCharactersProperty->ValueProp) : nullptr;
    if (!SavedCharactersProperty || !KeyProperty || !ValueProperty)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DWM Transition] CharacterCustomizer save schema was not recognized."));
        return false;
    }

    FScriptMapHelper SavedCharacters(SavedCharactersProperty,
        SavedCharactersProperty->ContainerPtrToValuePtr<void>(SaveGame));
    int32 SelectedIndex = INDEX_NONE;
    for (int32 Index = 0; Index < SavedCharacters.GetMaxIndex(); ++Index)
    {
        if (!SavedCharacters.IsValidIndex(Index))
        {
            continue;
        }

        SelectedIndex = Index;
        break;
    }

    if (SelectedIndex == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DWM Transition] The captured player snapshot contains no appearance data."));
        return false;
    }

    const void* SavedData = SavedCharacters.GetValuePtr(SelectedIndex);
    bool bCopiedData = false;
    FName AppearanceApplyFunctionName = NAME_None;
    const FName CandidateProperties[] = {
        FName(TEXT("CC_Loaded_Variables")),
        FName(TEXT("Local Data"))
    };
    for (const FName PropertyName : CandidateProperties)
    {
        if (FStructProperty* TargetProperty = FindFProperty<FStructProperty>(
            CharacterObject->GetClass(), PropertyName))
        {
            if (TargetProperty->Struct == ValueProperty->Struct)
            {
                void* TargetData = TargetProperty->ContainerPtrToValuePtr<void>(CharacterObject);
                TargetProperty->CopyCompleteValue(TargetData, SavedData);
                bCopiedData = true;
                if (!TargetProperty->RepNotifyFunc.IsNone())
                {
                    AppearanceApplyFunctionName = TargetProperty->RepNotifyFunc;
                }
            }
        }
    }

    if (!bCopiedData)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[DWM Transition] The display character has no compatible saved-variable property."));
        return false;
    }

    UFunction* AppearanceApplyFunction = AppearanceApplyFunctionName.IsNone()
        ? nullptr
        : CharacterObject->FindFunction(AppearanceApplyFunctionName);
    if (!AppearanceApplyFunction)
    {
        AppearanceApplyFunction = CharacterObject->FindFunction(
            FName(TEXT("OnRep_CC_Loaded_Variables")));
    }

    // The marketplace's packaged Blueprint folds its RepNotify graph into this public
    // event rather than exporting OnRep_CC_Loaded_Variables as a callable UFunction.
    if (!AppearanceApplyFunction)
    {
        AppearanceApplyFunction = CharacterObject->FindFunction(
            FName(TEXT("On Character Loaded")));
    }

    // Blueprint function names can be regenerated with display-name separators. Match
    // a normalized spelling as a safe fallback without taking a dependency on the pack.
    if (!AppearanceApplyFunction)
    {
        const FString ExpectedName(TEXT("onrepccloadedvariables"));
        for (TFieldIterator<UFunction> FunctionIt(
            CharacterObject->GetClass(), EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
        {
            FString NormalizedName = FunctionIt->GetName().ToLower();
            NormalizedName.ReplaceInline(TEXT("_"), TEXT(""));
            NormalizedName.ReplaceInline(TEXT(" "), TEXT(""));
            if (NormalizedName == ExpectedName)
            {
                AppearanceApplyFunction = *FunctionIt;
                break;
            }
        }
    }

    bool bAppliedAppearance = false;
    if (AppearanceApplyFunction && AppearanceApplyFunction->ParmsSize == 0)
    {
        CharacterObject->ProcessEvent(AppearanceApplyFunction, nullptr);
        bAppliedAppearance = true;
    }
    else if (AppearanceApplyFunction)
    {
        // On Character Loaded takes the same FCC_Saved_Variables struct stored in the
        // SaveGame map. Build a correctly sized parameter buffer and pass that value;
        // calling a Blueprint UFunction with a null parameter buffer is unsafe.
        FStructOnScope ApplyParameters(AppearanceApplyFunction);
        for (TFieldIterator<FProperty> ParameterIt(AppearanceApplyFunction); ParameterIt; ++ParameterIt)
        {
            FStructProperty* StructParameter = CastField<FStructProperty>(*ParameterIt);
            if (StructParameter &&
                StructParameter->HasAnyPropertyFlags(CPF_Parm) &&
                !StructParameter->HasAnyPropertyFlags(CPF_ReturnParm) &&
                StructParameter->Struct == ValueProperty->Struct)
            {
                void* ParameterData = StructParameter->ContainerPtrToValuePtr<void>(
                    ApplyParameters.GetStructMemory());
                StructParameter->CopyCompleteValue(ParameterData, SavedData);
                CharacterObject->ProcessEvent(
                    AppearanceApplyFunction, ApplyParameters.GetStructMemory());
                bAppliedAppearance = true;
                break;
            }
        }
    }

    if (!bAppliedAppearance)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[DWM Transition] Saved data was copied, but its apply function was not found."));
    }

    const FName LoadedName = KeyProperty->GetPropertyValue(SavedCharacters.GetKeyPtr(SelectedIndex));
    UE_LOG(LogTemp, Log, TEXT("[DWM Transition] Applied captured player appearance '%s' to the transition display."),
        *LoadedName.ToString());
    return bAppliedAppearance;
}

void UDwmGameInstance::SetupTransitionCharacterDisplay()
{
    UWorld* World = GetWorld();
    if (!World || !IsLevelTransitionWorld() || TransitionCharacterDisplay.IsValid())
    {
        return;
    }

    AActor* RotatingActor = nullptr;
    UStaticMeshComponent* RotatingMesh = nullptr;
    for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
    {
        AActor* Actor = *ActorIt;
        if (Actor && Actor->GetClass()->GetPathName().Contains(TEXT("BP_RotatingActor_Updated")))
        {
            RotatingActor = Actor;
            RotatingMesh = Actor->FindComponentByClass<UStaticMeshComponent>();
            break;
        }
    }

    if (!RotatingActor)
    {
        if (++TransitionCharacterDisplayAttempts < 20)
        {
            World->GetTimerManager().SetTimer(
                TransitionCharacterDisplayTimer,
                FTimerDelegate::CreateUObject(this, &UDwmGameInstance::SetupTransitionCharacterDisplay),
                0.05f,
                false);
        }
        return;
    }

    if (CapturedCharacterMeshes.IsEmpty())
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DWM Transition] No assembled player meshes were captured; preserving Labgames' visible fallback."));
        return;
    }

    // The mesh component is Labgames' actual presentation anchor. Its transform can
    // differ from the owning Blueprint actor after the selected container is applied.
    FTransform DisplayTransform = RotatingMesh
        ? RotatingMesh->GetComponentTransform()
        : RotatingActor->GetActorTransform();
    FRotator DisplayRotation = DisplayTransform.Rotator();
    DisplayRotation.Yaw += 180.0f; // face the PlayerStart camera instead of looking away from it
    DisplayTransform.SetRotation(DisplayRotation.Quaternion());

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ACharacter* DisplayCharacter = World->SpawnActor<ACharacter>(
        ACharacter::StaticClass(), DisplayTransform, SpawnParameters);
    if (!DisplayCharacter)
    {
        UE_LOG(LogTemp, Error, TEXT("[DWM Transition] Could not spawn the customized display character."));
        return;
    }

    // ACharacter's origin is the middle of its capsule, while the transition actor's
    // authored origin sits on its display floor. Raise the actor so its feet rest on
    // that floor instead of placing half of the customized body below it.
    if (const UCapsuleComponent* Capsule = DisplayCharacter->GetCapsuleComponent())
    {
        DisplayCharacter->AddActorWorldOffset(
            FVector::UpVector * Capsule->GetScaledCapsuleHalfHeight(), false);
    }

    DisplayCharacter->SetActorEnableCollision(false);
    TArray<UPrimitiveComponent*> PrimitiveComponents;
    DisplayCharacter->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
    for (UPrimitiveComponent* Component : PrimitiveComponents)
    {
        if (Component)
        {
            Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    }

    // Rebuild the exact meshes that were visible on the avatar at Confirm. This avoids
    // running CharacterCustomizer's UI-oriented save/load Blueprint in Labgames' GameMode,
    // where its Preset soft reference can be null even though the actor itself exists.
    if (USkeletalMeshComponent* DefaultMesh = DisplayCharacter->GetMesh())
    {
        DefaultMesh->SetVisibility(false, true);
    }

    TArray<USkinnedMeshComponent*> ClonedSkinnedMeshes;
    ClonedSkinnedMeshes.SetNumZeroed(CapturedCharacterMeshes.Num());
    int32 RebuiltMeshCount = 0;
    for (int32 MeshIndex = 0; MeshIndex < CapturedCharacterMeshes.Num(); ++MeshIndex)
    {
        const FDwmCapturedCharacterMesh& Captured = CapturedCharacterMeshes[MeshIndex];
        UMeshComponent* NewMesh = nullptr;

        if (Captured.SkeletalMesh)
        {
            USkeletalMeshComponent* NewSkeletal = NewObject<USkeletalMeshComponent>(
                DisplayCharacter,
                MakeUniqueObjectName(DisplayCharacter, USkeletalMeshComponent::StaticClass(),
                    Captured.SourceComponentName));
            USceneComponent* AttachmentParent = DisplayCharacter->GetRootComponent();
            if (ClonedSkinnedMeshes.IsValidIndex(Captured.AttachmentMeshIndex) &&
                ClonedSkinnedMeshes[Captured.AttachmentMeshIndex])
            {
                AttachmentParent = ClonedSkinnedMeshes[Captured.AttachmentMeshIndex];
            }
            NewSkeletal->SetupAttachment(AttachmentParent);
            NewSkeletal->SetRelativeTransform(Captured.RelativeTransform);
            NewSkeletal->SetSkeletalMesh(Captured.SkeletalMesh);
            NewSkeletal->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            NewSkeletal->SetGenerateOverlapEvents(false);
            if (Captured.AnimClass && Captured.LeaderMeshIndex == INDEX_NONE)
            {
                NewSkeletal->SetAnimInstanceClass(Captured.AnimClass);
            }
            DisplayCharacter->AddInstanceComponent(NewSkeletal);
            NewSkeletal->RegisterComponent();

            for (int32 MaterialIndex = 0;
                 MaterialIndex < Captured.MaterialSectionVisibilityLOD0.Num();
                 ++MaterialIndex)
            {
                if (Captured.MaterialSectionVisibilityLOD0[MaterialIndex] == 0)
                {
                    // An invalid section index intentionally bypasses LODMaterialMap
                    // remapping so MaterialIndex addresses the captured material directly.
                    NewSkeletal->ShowMaterialSection(MaterialIndex, INDEX_NONE, false, 0);
                }
            }
            for (const FName HiddenBone : Captured.HiddenBones)
            {
                NewSkeletal->HideBoneByName(HiddenBone, PBO_None);
            }
            for (const TPair<FName, float>& MorphTarget : Captured.MorphTargets)
            {
                NewSkeletal->SetMorphTarget(MorphTarget.Key, MorphTarget.Value);
            }
            ClonedSkinnedMeshes[MeshIndex] = NewSkeletal;
            NewMesh = NewSkeletal;
        }
        else if (Captured.StaticMesh)
        {
            UStaticMeshComponent* NewStatic = NewObject<UStaticMeshComponent>(
                DisplayCharacter,
                MakeUniqueObjectName(DisplayCharacter, UStaticMeshComponent::StaticClass(),
                    Captured.SourceComponentName));
            NewStatic->SetupAttachment(DisplayCharacter->GetRootComponent());
            NewStatic->SetRelativeTransform(Captured.RelativeTransform);
            NewStatic->SetStaticMesh(Captured.StaticMesh);
            NewStatic->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            NewStatic->SetGenerateOverlapEvents(false);
            DisplayCharacter->AddInstanceComponent(NewStatic);
            NewStatic->RegisterComponent();
            NewMesh = NewStatic;
        }

        if (!NewMesh)
        {
            continue;
        }

        for (int32 MaterialIndex = 0; MaterialIndex < Captured.Materials.Num(); ++MaterialIndex)
        {
            if (Captured.Materials[MaterialIndex])
            {
                NewMesh->SetMaterial(MaterialIndex, Captured.Materials[MaterialIndex]);
            }
        }
        NewMesh->SetVisibility(true, true);
        NewMesh->SetHiddenInGame(false, true);
        NewMesh->SetCastShadow(true);
        ++RebuiltMeshCount;
    }

    for (int32 MeshIndex = 0; MeshIndex < CapturedCharacterMeshes.Num(); ++MeshIndex)
    {
        USkinnedMeshComponent* Follower = ClonedSkinnedMeshes[MeshIndex];
        const int32 LeaderIndex = CapturedCharacterMeshes[MeshIndex].LeaderMeshIndex;
        if (Follower && ClonedSkinnedMeshes.IsValidIndex(LeaderIndex) &&
            ClonedSkinnedMeshes[LeaderIndex])
        {
            Follower->SetLeaderPoseComponent(ClonedSkinnedMeshes[LeaderIndex], true);
        }
    }

    if (RebuiltMeshCount == 0)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DWM Transition] The assembled player snapshot contained no rebuildable meshes; preserving Labgames' fallback."));
        DisplayCharacter->Destroy();
        return;
    }

    // Attach to the same component Labgames rotates so its original presentation and
    // lighting still work. Hide only the abstract mesh; hiding recursively would also
    // hide our newly attached character.
    USceneComponent* RotationAnchor = RotatingMesh
        ? static_cast<USceneComponent*>(RotatingMesh)
        : RotatingActor->GetRootComponent();
    if (RotationAnchor)
    {
        DisplayCharacter->AttachToComponent(
            RotationAnchor, FAttachmentTransformRules::KeepWorldTransform);
    }

    // Labgames' custom-container scene was authored around a bright/emissive abstract
    // mesh. Its three Rect Lights use intensities of only 8, 4, and 3, which leaves the
    // physically shaded CharacterCustomizer materials almost completely black. Add a
    // transition-local fill light between the active camera and the captured player.
    FVector ViewLocation = DisplayCharacter->GetActorLocation() + FVector(-200.0f, 0.0f, 100.0f);
    FRotator ViewRotation = FRotator::ZeroRotator;
    if (APlayerController* PlayerController = World->GetFirstPlayerController())
    {
        PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
    }

    const FVector CharacterLightTarget =
        DisplayCharacter->GetActorLocation() + FVector(0.0f, 0.0f, 65.0f);
    const FVector FillLightLocation =
        FMath::Lerp(CharacterLightTarget, ViewLocation, 0.45f) + FVector(0.0f, 0.0f, 25.0f);
    FActorSpawnParameters LightSpawnParameters;
    LightSpawnParameters.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (APointLight* FillLight = World->SpawnActor<APointLight>(
        APointLight::StaticClass(), FillLightLocation, FRotator::ZeroRotator,
        LightSpawnParameters))
    {
#if WITH_EDITOR
        FillLight->SetActorLabel(TEXT("DWM_Transition_Player_FillLight"));
#endif
        if (UPointLightComponent* LightComponent =
            Cast<UPointLightComponent>(FillLight->GetLightComponent()))
        {
            LightComponent->SetMobility(EComponentMobility::Movable);
            LightComponent->SetIntensity(10000.0f);
            LightComponent->SetAttenuationRadius(900.0f);
            LightComponent->SetLightColor(FLinearColor(1.0f, 0.92f, 0.82f));
            LightComponent->SetCastShadows(false);
        }
        UE_LOG(LogTemp, Log,
            TEXT("[DWM Transition] Added player fill light at %s."),
            *FillLightLocation.ToCompactString());
    }

    TransitionCharacterDisplay = DisplayCharacter;
    if (RotatingMesh)
    {
        RotatingMesh->SetVisibility(false, false);
    }
    UE_LOG(LogTemp, Log,
        TEXT("[DWM Transition] Rebuilt the confirmed player from %d assembled mesh components and hid the abstract fallback."),
        RebuiltMeshCount);
}

bool UDwmGameInstance::ShouldRunDwmRuntimeSystems() const
{
    const UWorld* World = GetWorld();
    if (const AGameModeBase* GameMode = World ? World->GetAuthGameMode() : nullptr)
    {
        // A DWM map may intentionally use the marketplace CharacterCustomizer GameMode
        // during character creation. In that mode the pack owns its pawn, enhanced-input
        // mapping, and UI. Do not inject DWM's pendulum, economy HUD, or terminal into it.
        // Checking the active GameMode (rather than just the package path) also works when
        // the customizer is used from DWM_Mountain instead of its supplied demo map.
        if (GameMode->GetClass()->GetPathName().StartsWith(TEXT("/Game/CharacterCustomizer/")))
        {
            return false;
        }
    }

    const UPackage* Package = World ? World->GetOutermost() : nullptr;
    if (!Package)
    {
        return true;
    }

    // PIE packages are named UEDPIE_* but retain their content path, so this also covers
    // Play-In-Editor sessions of the supplied CharacterCustomizer demo levels.
    return !Package->GetName().StartsWith(TEXT("/Game/CharacterCustomizer/"));
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
    // Reuse, don't rebuild (Day 20 Task 2): the original Day 18 demo trade, now expressed as
    // a call into the generalized path with its original fixed parameters. Mountain is the
    // BUYER (pays Stone, first arg); Valley is the SELLER (receives Stone, provides Grain,
    // second arg) -- matches the original hardcoded WriteTrade(..., "mountain", "valley", ...)
    // call exactly.
    return ExecuteConfiguredTrade(TEXT("mountain"), TEXT("valley"), TEXT("grain"), 20.0, 20.0,
        TEXT("Mountain buys grain from Valley"));
}

bool UDwmGameInstance::ExecuteConfiguredTrade(const FString& BuyerCommunityId, const FString& SellerCommunityId,
    const FString& ResourceId, double Amount, double Quantity, const FString& Memo)
{
    if (!RefreshEconomyState())
    {
        return false;
    }

    const FDwmCommunityEconomyState* BuyerBefore = EconomyStates.FindByPredicate(
        [&BuyerCommunityId](const FDwmCommunityEconomyState& State) { return State.CommunityId == BuyerCommunityId; });
    const FDwmCommunityEconomyState* SellerBefore = EconomyStates.FindByPredicate(
        [&SellerCommunityId](const FDwmCommunityEconomyState& State) { return State.CommunityId == SellerCommunityId; });
    if (!BuyerBefore || !SellerBefore)
    {
        SetEconomyStatus(FString::Printf(
            TEXT("Trade cannot run: '%s' or '%s' is missing from the economy snapshot."),
            *BuyerCommunityId, *SellerCommunityId), FColor::Red);
        return false;
    }

    const double BuyerBalanceBefore = BuyerBefore->StoneBalance;
    const double SellerBalanceBefore = SellerBefore->StoneBalance;

    // BuyerCommunityId pays Stone and receives the resource; SellerCommunityId receives Stone
    // and provides the resource -- passed straight through as WriteTrade's
    // FromCommunityId/ToCommunityId with no crossing (WriteTrade's From = payer, To =
    // receiver; this function's Buyer/Seller naming matches that directly, unlike the
    // terminal actor's own field names, which are Buyer/Seller for a DIFFERENT reason: see
    // DwmTradeTerminalActor.h's comment on why "From"/"To" was avoided there).
    const FDwmEconomyWriter::FWriteTradeResult Result = FDwmEconomyWriter::WriteTrade(
        GetEconomyPackagePath(), KnownCommunityIds(), KnownResourceIds(),
        BuyerCommunityId, SellerCommunityId, Amount, ResourceId, Quantity, Memo);
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

    const FDwmCommunityEconomyState* BuyerAfter = EconomyStates.FindByPredicate(
        [&BuyerCommunityId](const FDwmCommunityEconomyState& State) { return State.CommunityId == BuyerCommunityId; });
    const FDwmCommunityEconomyState* SellerAfter = EconomyStates.FindByPredicate(
        [&SellerCommunityId](const FDwmCommunityEconomyState& State) { return State.CommunityId == SellerCommunityId; });
    const bool bExpectedDeltas = BuyerAfter && SellerAfter
        && FMath::IsNearlyEqual(BuyerAfter->StoneBalance, BuyerBalanceBefore - Amount)
        && FMath::IsNearlyEqual(SellerAfter->StoneBalance, SellerBalanceBefore + Amount);
    if (!bExpectedDeltas)
    {
        SetEconomyStatus(TEXT("Trade settled, but the refreshed balances do not match the expected deltas."), FColor::Red);
        return false;
    }

    SetEconomyStatus(FString::Printf(
        TEXT("Trade complete: %s %.0f -> %.0f St | %s %.0f -> %.0f St"),
        *BuyerCommunityId, BuyerBalanceBefore, BuyerAfter->StoneBalance,
        *SellerCommunityId, SellerBalanceBefore, SellerAfter->StoneBalance), FColor::Green);
    return true;
}

bool UDwmGameInstance::GetCompletedTradePartners(
    const FString& BuyerCommunityId, TArray<FString>& OutPartners) const
{
    OutPartners.Reset();

    FSQLiteDatabase Db;
    if (!Db.Open(*GetEconomyPackagePath(), ESQLiteDatabaseOpenMode::ReadOnly))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[DWM Economy] Could not read completed trade partners: %s"),
            *Db.GetLastError());
        return false;
    }

    FSQLitePreparedStatement Stmt;
    if (!Stmt.Create(Db,
        TEXT("SELECT DISTINCT ToCommunityId FROM StoneLedger ")
        TEXT("WHERE FromCommunityId = $buyer ORDER BY ToCommunityId;"),
        ESQLitePreparedStatementFlags::Persistent)
        || !Stmt.SetBindingValueByName(TEXT("$buyer"), BuyerCommunityId))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[DWM Economy] Could not prepare completed trade partner query: %s"),
            *Db.GetLastError());
        Stmt.Destroy();
        Db.Close();
        return false;
    }

    for (;;)
    {
        const ESQLitePreparedStatementStepResult StepResult = Stmt.Step();
        if (StepResult == ESQLitePreparedStatementStepResult::Done)
        {
            break;
        }
        if (StepResult != ESQLitePreparedStatementStepResult::Row)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[DWM Economy] Completed trade partner query failed: %s"),
                *Db.GetLastError());
            Stmt.Destroy();
            Db.Close();
            OutPartners.Reset();
            return false;
        }

        FString PartnerId;
        if (!Stmt.GetColumnValueByIndex(0, PartnerId))
        {
            Stmt.Destroy();
            Db.Close();
            OutPartners.Reset();
            return false;
        }
        OutPartners.Add(MoveTemp(PartnerId));
    }

    Stmt.Destroy();
    Db.Close();
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

    // Terminal's default UPROPERTY values (ToCommunityId="mountain", FromCommunityId="valley",
    // ResourceId="grain", Amount=20, Quantity=20) reproduce the original Day 18 demo trade
    // exactly, so this runtime-spawned convenience terminal keeps working unchanged. The four
    // additional storyline terminals (Hillside/Suburb/City) are level-placed instances with
    // their own configured trades instead -- see AGENT_LOG.md for placement instructions.
    bDemoTradeTerminalSpawned = true;
    SetEconomyStatus(TEXT("Day 18 ready: walk to the gold trade terminal and press E to buy Grain from Valley."), FColor::Cyan);
}
