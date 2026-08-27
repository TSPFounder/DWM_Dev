// DwmNpcActor.cpp
//
// Keep the source ASCII-safe; approved em dashes use universal character names.

#include "DwmNpcActor.h"

#include "DWM_DevCharacter.h"
#include "DWM_DevPlayerController.h"
#include "DwmDialogueWidget.h"
#include "DwmGameInstance.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "DwmNpc"

namespace
{
    // Distinct from the trade terminal's own on-screen message key (0xD0018A) so an NPC
    // and a terminal standing near each other don't overwrite one another's prompt.
    constexpr uint64 NpcPromptMessageKey = 0xD00190ULL;
}

ADwmNpcActor::ADwmNpcActor()
{
    PrimaryActorTick.bCanEverTick = true;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    NpcMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("NpcMesh"));
    NpcMesh->SetupAttachment(SceneRoot);
    // The Hank asset is authored facing +Y, while AActor movement treats +X as forward.
    // Rotate only the visual mesh so actor yaw, waypoint math, and interaction transforms
    // stay in normal Unreal coordinates.
    NpcMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
    // Set-dressing, not a physical obstacle: the player should not be able to shove Hank
    // around, and he should not block movement through the marker area. This also means
    // his walk loop can't push the player -- the movement below is a plain SetActorLocation
    // with no sweep, so nothing collides either way.
    NpcMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
    InteractionSphere->SetupAttachment(SceneRoot);
    InteractionSphere->InitSphereRadius(200.0f);
    InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    InteractionSphere->SetCollisionObjectType(ECC_WorldDynamic);
    InteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
    InteractionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    InteractionSphere->SetGenerateOverlapEvents(true);

    DisplayName = LOCTEXT("HankName", "Hank");
    RequiredSellerCommunityIds = { TEXT("hillside"), TEXT("valley"), TEXT("suburb"), TEXT("city") };

    // Give the MVP Hank a complete, usable setup even when no content-only Blueprint
    // has been created yet. These are the civilian Hank mesh and in-place mobility
    // clips already shipped in DWM_Dev. Individual Blueprint children may override all
    // three assets later without changing the native interaction/movement behavior.
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> HankMeshFinder(
        TEXT("/Game/YI_NPC/Meshes/Hank/Skeletal/SK_Hank_02.SK_Hank_02"));
    if (HankMeshFinder.Succeeded())
    {
        NpcMesh->SetSkeletalMesh(HankMeshFinder.Object);
    }

    static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleAnimationFinder(
        TEXT("/Game/YI_NPC/Animation/Mocap_Mobility/IPC/MOB1_Stand_Relaxed_Idle_v2_IPC.MOB1_Stand_Relaxed_Idle_v2_IPC"));
    if (IdleAnimationFinder.Succeeded())
    {
        IdleAnimation = IdleAnimationFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UAnimSequence> WalkAnimationFinder(
        TEXT("/Game/YI_NPC/Animation/Mocap_Mobility/IPC/MOB1_Walk_F_Loop_IPC.MOB1_Walk_F_Loop_IPC"));
    if (WalkAnimationFinder.Succeeded())
    {
        WalkAnimation = WalkAnimationFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UAnimSequence> TurnLeftAnimationFinder(
        TEXT("/Game/YI_NPC/Animation/Mocap_Mobility/IPC/MOB1_Stand_Rlx_Turn_In_Place_L_Loop_IPC.MOB1_Stand_Rlx_Turn_In_Place_L_Loop_IPC"));
    if (TurnLeftAnimationFinder.Succeeded())
    {
        TurnLeftAnimation = TurnLeftAnimationFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UAnimSequence> TurnRightAnimationFinder(
        TEXT("/Game/YI_NPC/Animation/Mocap_Mobility/IPC/MOB1_Stand_Rlx_Turn_In_Place_R_Loop_IPC.MOB1_Stand_Rlx_Turn_In_Place_R_Loop_IPC"));
    if (TurnRightAnimationFinder.Succeeded())
    {
        TurnRightAnimation = TurnRightAnimationFinder.Object;
    }

    // The native widget builds a clean static dialogue panel. A WBP_DwmDialogue child
    // can still be assigned later for visual polish without changing Hank's C++ state
    // machine or the E-key interaction route.
    DialogueWidgetClass = UDwmDialogueWidget::StaticClass();

    PopulateDefaultHankDialogue();
}

void ADwmNpcActor::ConfigureProfileFromSource(EDwmNpcProfile NewProfile,
    USkeletalMeshComponent* SourceMesh)
{
    NpcProfile = NewProfile;

    if (SourceMesh && NpcMesh)
    {
        NpcMesh->SetSkeletalMesh(SourceMesh->GetSkeletalMeshAsset());
        // The placed source people use their skeletal mesh as the actor root, so that
        // component's relative transform is already its saved world transform. The NPC
        // actor is spawned at that world transform; copying it here would apply it twice.
        //
        // NOTE this also drops the constructor's -90 facing correction, which is what
        // keeps the actor's +X pointing where the character looks. The mesh is left
        // alone on purpose -- rotating it would move art that was placed deliberately --
        // and the difference is carried in MeshFacingYawOffset for FaceDirection to
        // compensate instead. Without this the NPC turns to show the player its side
        // rather than its face (issue #10).
        NpcMesh->SetRelativeTransform(FTransform::Identity);
        MeshFacingYawOffset = CopiedMeshFacingYawOffset;
        NpcMesh->SetAnimInstanceClass(nullptr);

        for (int32 MaterialIndex = 0; MaterialIndex < SourceMesh->GetNumMaterials(); ++MaterialIndex)
        {
            NpcMesh->SetMaterial(MaterialIndex, SourceMesh->GetMaterial(MaterialIndex));
        }
    }

    DialogueByState.Empty();
    RequiredSellerCommunityIds.Empty();
    BuyerCommunityId.Reset();
    bEnableScriptedMovement = false;
    bSnapToGround = false;
    TurbineActor = nullptr;
    WalkAnimation = nullptr;
    TurnLeftAnimation = nullptr;
    TurnRightAnimation = nullptr;
    GestureAtTurbineAnimation = nullptr;
    GestureAtTurbineMontage = nullptr;
    TalkAnimation = nullptr;
    TalkMontage = nullptr;
    InteractionSphere->SetSphereRadius(250.0f);

    IdleAnimation = LoadObject<UAnimSequence>(nullptr,
        TEXT("/Game/Scanned3DPeoplePack/RP_Character/00_Animations/"
             "rp_sophia_animated_003_idling_ue4.rp_sophia_animated_003_idling_ue4"));

    // Every Hillside NPC previously took the single Sophia idle above, so a group
    // standing together performed identical motion (issue #9). Where a compatible
    // alternative exists, take one at random instead; PickVariedIdleAnimation returns
    // nullptr when nothing matches this mesh, and the configured idle stands.
    if (UAnimSequence* VariedIdle = PickVariedIdleAnimation())
    {
        IdleAnimation = VariedIdle;
    }

    PopulateHillsideDialogue();
}

void ADwmNpcActor::ConfigureDialogueProxy(EDwmNpcProfile NewProfile)
{
    ConfigureProfileFromSource(NewProfile, nullptr);

    if (NpcMesh)
    {
        NpcMesh->SetVisibility(false, true);
        NpcMesh->SetHiddenInGame(true, true);
        NpcMesh->SetComponentTickEnabled(false);
    }
}

// ---------------------------------------------------------------------------
// Default dialogue -- Hank's copy, verbatim from DWM_MVP_Dialogue.md (Mountain section).
// ---------------------------------------------------------------------------

void ADwmNpcActor::PopulateDefaultHankDialogue()
{
    const FText Hank = LOCTEXT("HankSpeaker", "Hank");

    auto MakeLine = [&Hank](const FText& Body, const FText& Prompt)
    {
        FDwmDialogueLine Line;
        Line.Speaker = Hank;
        Line.Body = Body;
        Line.AdvancePrompt = Prompt;
        return Line;
    };

    {
        FDwmDialogueSequence Approach;
        Approach.Lines.Add(MakeLine(
            LOCTEXT("HankApproach",
                "That turbine came with the land when we settled here. Nobody's turned it in "
                "years, and nobody left detailed drawings of how it goes back together. We need "
                "real plans before anyone touches a wrench to that thing. We need hands, parts, "
                "and food for the crew to fix it. Head down to the other communities. Trade fair, "
                "come back with what we need, and let's get this thing spinning again."),
            LOCTEXT("HankApproachPrompt", "What exactly do we need?")));
        DialogueByState.Add(EDwmDialogueState::Approach, Approach);
    }

    {
        FDwmDialogueSequence Details;
        Details.Lines.Add(MakeLine(
            LOCTEXT("HankQuestDetails",
                "Start with Hillside \u2014 they've got engineers who can put together real CAD "
                "drawings and a simulation model, so whoever fixes this thing isn't guessing. After "
                "that go to the valley for grain, fruits, and vegetables to feed the crew while "
                "they're working the mount. Hands that know rigging, because none of us have hung "
                "something this heavy before. And tools from the city \u2014 precision work no one "
                "up here can forge."),
            FText::GetEmpty()));
        Details.Lines.Add(MakeLine(
            LOCTEXT("HankDirections",
                "Go out through the gate and follow the track down to Hillside. Ask for Sophia "
                "Sandoval \u2014 she's got the room above the realty office. She'll be expecting "
                "you."),
            FText::GetEmpty()));
        DialogueByState.Add(EDwmDialogueState::QuestDetails, Details);
    }

    {
        FDwmDialogueSequence InProgress;
        InProgress.Lines.Add(MakeLine(
            LOCTEXT("HankReturnInProgress",
                "How's it going down there? Whatever you've got, it's a start."),
            FText::GetEmpty()));
        DialogueByState.Add(EDwmDialogueState::ReturnInProgress, InProgress);
    }

    {
        FDwmDialogueSequence AllDone;
        AllDone.Lines.Add(MakeLine(
            LOCTEXT("HankReturnComplete",
                "Real plans for the mount, meals for the crew, hands to do the rigging, and tools "
                "to finish it right. That's everything. Let's bring this old thing back to life."),
            FText::GetEmpty()));
        DialogueByState.Add(EDwmDialogueState::ReturnAllTradesComplete, AllDone);
    }

    {
        FDwmDialogueSequence Farewell;
        Farewell.Lines.Add(MakeLine(
            LOCTEXT("HankFarewell",
                "There it goes. Every community up on that ledger had a hand in this \u2014 yours "
                "included."),
            FText::GetEmpty()));
        DialogueByState.Add(EDwmDialogueState::Farewell, Farewell);
    }

    {
        // Flavor only -- gates nothing. The doc marks all three as freely cuttable.
        // NOTE: the first line references "the storm", but DWM_MVP_Storyline.md's
        // 2026-07-18 update reframed the premise away from storm damage to a turbine that
        // was simply never run. Left verbatim rather than silently rewritten -- flagged
        // for a content decision.
        FDwmDialogueSequence Ambient;
        Ambient.Lines.Add(MakeLine(
            LOCTEXT("HankAmbient1",
                "Freshcan crew finished the new houses last week \u2014 good timing, since the storm "
                "put three families out of their own. Village's a little more crowded now, but "
                "nobody's sleeping in a barn."),
            FText::GetEmpty()));
        Ambient.Lines.Add(MakeLine(
            LOCTEXT("HankAmbient2",
                "Solar panels are holding the mountain over while the turbine's down. Control "
                "station keeps it steady, and the bank stores what we don't use by day. Won't power "
                "much more than lights and the radio, but it's something."),
            FText::GetEmpty()));
        Ambient.Lines.Add(MakeLine(
            LOCTEXT("HankAmbient3",
                "Fish trap's been good to us this season \u2014 one less thing to worry about while "
                "everyone's hands are full with the turbine. Small mercy."),
            FText::GetEmpty()));
        DialogueByState.Add(EDwmDialogueState::Ambient, Ambient);
    }
}

void ADwmNpcActor::PopulateHillsideDialogue()
{
    auto MakeLine = [](const FText& Speaker, const FText& Body, const FText& Prompt)
    {
        FDwmDialogueLine Line;
        Line.Speaker = Speaker;
        Line.Body = Body;
        Line.AdvancePrompt = Prompt;
        return Line;
    };

    FDwmDialogueSequence Approach;
    FDwmDialogueSequence Details;
    FDwmDialogueSequence Ambient;

    switch (NpcProfile)
    {
    case EDwmNpcProfile::Sophia:
    {
        DisplayName = LOCTEXT("SophiaName", "Sophia");
        const FText Speaker = DisplayName;

        // Issue #38: found on the couch when the player walks in, and she gets up.
        // The offsets are how an office-chair clip is made to land on a couch cushion;
        // they are EditAnywhere so this is a level tweak, not a recompile.
        // Left half of the couch; Nathan takes the right. Without this they would
        // both snap to the couch origin -- and, now that both of them get up,
        // both end up on the same spot in front of it too.
        SeatLateralOffset = -45.0f;
        bStartsSeated = true;
        bStandsForPlayer = true;
        Approach.Lines.Add(MakeLine(Speaker,
            LOCTEXT("SophiaApproach",
                "So that's the turbine that came with your land. Ambitious purchase \u2014 nobody's "
                "touched that thing in years. Good news is, my two here already worked up what you "
                "need to bring it back."),
            LOCTEXT("SophiaPrompt", "What exactly did you put together?")));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("SophiaDetails",
                "Full CAD models and drawings of the mount and rotor assembly, plus a Simulink "
                "model of how it should actually behave once it's running. Owen did the drawings, "
                "Nathan built the model. Between the two, whoever's doing the repair up there "
                "won't be guessing."),
            FText::GetEmpty()));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("SophiaTrade",
                "Here's everything \u2014 drawings and model both. Take care of it; that's real "
                "engineering hours in your hands, not just a sketch on a napkin."),
            FText::GetEmpty()));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("SophiaDirections",
                "Down the stairs, right as you come out of the office, then straight on down the "
                "market street \u2014 don't turn off it. Road climbs out the far end and drops you "
                "into the Valley. Maria Vega will be on her porch. She generally is."),
            FText::GetEmpty()));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("SophiaFarewell",
                "Good luck up there. Come back through when it's spinning \u2014 I'd like to see it."),
            FText::GetEmpty()));
        Ambient.Lines.Add(MakeLine(Speaker,
            LOCTEXT("SophiaAmbient1",
                "Old sawmill building still stands out back \u2014 not running these days, but I like "
                "the space. Good light for drafting."), FText::GetEmpty()));
        Ambient.Lines.Add(MakeLine(Speaker,
            LOCTEXT("SophiaAmbient2",
                "Got solar on the roof now, battery bank right beside it. Doesn't run much, but it "
                "keeps the lights on through a cloudy week while these two are hunched over a screen."),
            FText::GetEmpty()));
        break;
    }

    case EDwmNpcProfile::Owen:
    {
        DisplayName = LOCTEXT("OwenName", "Owen");
        const FText Speaker = DisplayName;
        Approach.Lines.Add(MakeLine(Speaker,
            LOCTEXT("OwenApproach",
                "I pulled the mount and rotor assembly apart in CAD, piece by piece. Whoever repairs "
                "that thing won't have to reverse-engineer it in the field \u2014 every bracket, every "
                "bolt pattern, it's all there."), FText::GetEmpty()));
        Ambient.Lines.Add(MakeLine(Speaker,
            LOCTEXT("OwenAmbient1",
                "Hardest part wasn't the rotor \u2014 it was the mount. Whoever built that turbine "
                "originally didn't leave much documentation. Had to measure half of it by hand from "
                "photos."), FText::GetEmpty()));
        Ambient.Lines.Add(MakeLine(Speaker,
            LOCTEXT("OwenAmbient2",
                "If the repair crew finds something in the field that doesn't match my drawings, tell "
                "them to trust what they're looking at, not the paper. Old hardware doesn't always "
                "match what was on file."), FText::GetEmpty()));
        break;
    }

    case EDwmNpcProfile::Nathan:
    {
        DisplayName = LOCTEXT("NathanName", "Nathan");
        const FText Speaker = DisplayName;

        // Issue #38: on the couch beside Sophia, and gets up just after her -- the
        // pause reads as following her lead rather than the two rising on a cue.
        StandDelay = 0.7f;
        // Right half of the couch, opposite her.
        SeatLateralOffset = 45.0f;
        bStartsSeated = true;
        bStandsForPlayer = true;
        Approach.Lines.Add(MakeLine(Speaker,
            LOCTEXT("NathanApproach",
                "Owen's drawings tell you what the turbine looks like. My model tells you how it's "
                "supposed to behave once it's spinning again \u2014 load, response, where it'll struggle. "
                "Saves you finding out the hard way."), FText::GetEmpty()));
        Ambient.Lines.Add(MakeLine(Speaker,
            LOCTEXT("NathanAmbient1",
                "Model's only as good as what we know about that specific turbine. I built it off "
                "Owen's CAD data and some reasonable assumptions \u2014 real sensor data once it's running "
                "would tighten it up, but it'll get you started."), FText::GetEmpty()));
        Ambient.Lines.Add(MakeLine(Speaker,
            LOCTEXT("NathanAmbient2",
                "Half my job is knowing when the simple model is good enough and when it isn't. For a "
                "first repair pass, simple's fine."), FText::GetEmpty()));
        break;
    }

    case EDwmNpcProfile::Maria:
    {
        DisplayName = LOCTEXT("MariaName", "Maria");
        const FText Speaker = DisplayName;
        Approach.Lines.Add(MakeLine(Speaker,
            LOCTEXT("MariaApproach",
                "The Valley can support the Mountain crews. We have grain in bulk, plus corn, "
                "vegetables, and fruit from a strong season. That will keep people fed while "
                "they bring the turbine back."),
            LOCTEXT("MariaPrompt", "How would the supply agreement work?")));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("MariaDetails",
                "We'll keep the formal ledger simple: Valley grain for Mountain stone. The corn, "
                "vegetables, and fruit go into the crew meals around that exchange, and when the "
                "season permits we can add honey and meat. Fair weights on both sides, written down."),
            FText::GetEmpty()));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("MariaTrade",
                "Tell Hank the first grain shipment is ready. Valley will keep the food moving as "
                "long as Mountain keeps the stone moving."),
            FText::GetEmpty()));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("MariaDirections",
                "Left out of the house and follow the dirt road down \u2014 it runs all the way "
                "into the Suburb. Ask for DeShawn Okafor; he works out of the realty office "
                "there. Walk straight in, he won't stand on ceremony."),
            FText::GetEmpty()));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("MariaFarewell",
                "And tell Hank I said good luck. He still owes me for last winter."),
            FText::GetEmpty()));
        Ambient.Lines.Add(MakeLine(Speaker,
            LOCTEXT("MariaAmbient1",
                "Corn and vegetables move quickly. Grain is what lets us promise support through "
                "the whole repair instead of only the first week."), FText::GetEmpty()));
        Ambient.Lines.Add(MakeLine(Speaker,
            LOCTEXT("MariaAmbient2",
                "A fair ledger matters. If every community can see what came in and what went out, "
                "people keep trading even after the emergency is over."), FText::GetEmpty()));
        break;
    }

    case EDwmNpcProfile::DeShawn:
    {
        DisplayName = LOCTEXT("DeShawnName", "DeShawn");
        const FText Speaker = DisplayName;
        Approach.Lines.Add(MakeLine(Speaker,
            LOCTEXT("DeShawnApproach",
                "The Suburbs can put people on the Mountain repair: riggers, builders, and the steady "
                "hands who know how to work safely around heavy parts."),
            LOCTEXT("DeShawnPrompt", "What do the crews need?")));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("DeShawnDetails",
                "Maria's food keeps the crews moving, and the City can turn scrap and plans into "
                "parts. My job is making sure people, tools, and transport show up in the same place "
                "on the same day."),
            FText::GetEmpty()));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("DeShawnTrade",
                "Tell Hank the crew is ready once the parts and food are lined up. We can move fast, "
                "but not messy."),
            FText::GetEmpty()));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("DeShawnDirections",
                "City's too far to walk and I'm not sending you on foot. Bus stop's on the "
                "corner \u2014 take the loop. When you get to the city, ask for Mike Dayton, he'll "
                "be on the shop floor, and he'll be the loudest thing in the building."),
            FText::GetEmpty()));
        Ambient.Lines.Add(MakeLine(Speaker,
            LOCTEXT("DeShawnAmbient1",
                "A repair is logistics before it is heroics: people, food, transport, tools, and "
                "someone keeping the list honest."),
            FText::GetEmpty()));
        break;
    }

    case EDwmNpcProfile::Mike:
    {
        DisplayName = LOCTEXT("MikeName", "Mike");
        const FText Speaker = DisplayName;

        // Issue #18: starts on the couch, gets up and faces the player when they walk
        // in. Same mechanism as Sophia -- see the Seated properties.
        bStartsSeated = true;
        bStandsForPlayer = true;
        Approach.Lines.Add(MakeLine(Speaker,
            LOCTEXT("MikeApproach",
                "I have Owen's CAD drawings for the mount and rotor assembly. Good drawings mean I can "
                "quote real machining time instead of guessing with a tape measure."),
            LOCTEXT("MikePrompt", "What can the City build?")));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("MikeDetails",
                "This batch of replacement parts will hold. Long term, though, every custom part on "
                "those drawings is one-at-a-time work unless the communities invest in better tooling."),
            FText::GetEmpty()));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("MikeTrade",
                "Take this number back with Sophia's design hours. Hank needs both halves before he "
                "chooses the repair plan."),
            FText::GetEmpty()));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("MikeDirections",
                "You'll want Kai before you go anywhere. He's in the back, over there. Metal's "
                "only half of what you're carrying home."),
            FText::GetEmpty()));
        Ambient.Lines.Add(MakeLine(Speaker,
            LOCTEXT("MikeAmbient1",
                "Kai is upstairs over the shop floor. He has Nathan's control model turning into code "
                "while I keep the metal honest."),
            FText::GetEmpty()));
        break;
    }

    case EDwmNpcProfile::Kai:
    {
        DisplayName = LOCTEXT("KaiName", "Kai");
        const FText Speaker = DisplayName;
        Approach.Lines.Add(MakeLine(Speaker,
            LOCTEXT("KaiApproach",
                "Nathan's Simulink model gave me the behavior target. I turned that into controller "
                "code and a Fusion enclosure layout so the turbine can monitor wind speed, load, and "
                "response."),
            LOCTEXT("KaiPrompt", "How does that help the repair?")));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("KaiDetails",
                "Software is cheaper than a broken rotor, but it still needs fittings, wiring, and "
                "maintenance. Tell Hank the controller is part of the repair, not a decoration."),
            FText::GetEmpty()));
        Details.Lines.Add(MakeLine(Speaker,
            LOCTEXT("KaiDirections",
                "Loop runs back round the way you came \u2014 stays on it all the way up to "
                "Mountain. Get on, and the next thing you'll be looking at is that turbine. "
                "Hank will be waiting."),
            FText::GetEmpty()));
        Ambient.Lines.Add(MakeLine(Speaker,
            LOCTEXT("KaiAmbient1",
                "Mike can machine a beautiful part. My job is making sure the turbine does not destroy "
                "that beautiful part on day one."),
            FText::GetEmpty()));
        break;
    }

    default:
        return;
    }

    DialogueByState.Add(EDwmDialogueState::Approach, Approach);
    if (Details.Lines.Num() > 0)
    {
        DialogueByState.Add(EDwmDialogueState::QuestDetails, Details);
    }
    if (Ambient.Lines.Num() > 0)
    {
        DialogueByState.Add(EDwmDialogueState::Ambient, Ambient);
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ADwmNpcActor::BeginPlay()
{
    Super::BeginPlay();

    InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &ADwmNpcActor::OnInteractionSphereBeginOverlap);
    InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &ADwmNpcActor::OnInteractionSphereEndOverlap);

    // Everything the loop does is relative to wherever he was placed, so a level designer
    // moves the whole routine by moving the actor -- no waypoint actors to keep in sync.
    HomeLocation = GetActorLocation();
    HomeRotation = GetActorRotation();
    PreviousLocation = HomeLocation;

    // OpenLevel destroys this actor, but the GameInstance survives. Restore Hank's
    // conversation progress so returning from another community is actually a return visit.
    if (IsHankProfile())
    {
        if (const UDwmGameInstance* GameInstance = GetGameInstance<UDwmGameInstance>())
        {
            bHasDeliveredOpening = GameInstance->HasDeliveredHankOpening();
            bFarewellUnlocked = GameInstance->IsHankFarewellUnlocked();
            ReturnVisitCount = GameInstance->GetHankReturnVisitCount();
            AmbientCursor = GameInstance->GetHankAmbientCursor();
        }
    }

    // Which animation path applies is decided by what's actually on the mesh, not by a
    // separate flag that could disagree with it.
    bUsingAnimBlueprint = (NpcMesh && NpcMesh->GetAnimClass() != nullptr);

    StandingLocation = GetActorLocation();
    if (bStartsSeated)
    {
        BeginSeated();
    }

    if (bUsingAnimBlueprint)
    {
        NpcMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        UE_LOG(LogTemp, Log,
            TEXT("[DWM NPC] '%s' is driving animation through its Anim Blueprint (blended)."),
            *GetNameSafe(this));
    }
    else
    {
        UE_LOG(LogTemp, Log,
            TEXT("[DWM NPC] '%s' has no Anim Blueprint -- falling back to single-node playback "
                "(clips will cut rather than blend)."),
            *GetNameSafe(this));

        if (!IdleAnimation)
        {
            // Deliberately an error, not a silent no-op: with no Anim Blueprint AND no
            // idle sequence, nothing drives the skeleton at all and the character stands
            // in its reference (T) pose -- the exact problem this class exists to fix.
            UE_LOG(LogTemp, Error,
                TEXT("[DWM NPC] '%s' has neither an Anim Blueprint nor an IdleAnimation -- it "
                    "will render in its reference (T) pose. Assign one or the other."),
                *GetNameSafe(this));
        }
        if (bEnableScriptedMovement && !WalkAnimation)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[DWM NPC] '%s' has scripted movement enabled but no WalkAnimation -- it "
                    "will slide along in the idle pose."),
                *GetNameSafe(this));
        }
    }

    // NOT when already seated. BeginSeated has set the Seated activity and started the
    // sit clip; resetting to IdleAtMarker here overwrote BOTH -- which is exactly why
    // the seated NPCs moved onto their seat and then stood up on it.
    if (Activity != EDwmNpcActivity::Seated)
    {
        EnterActivity(EDwmNpcActivity::IdleAtMarker);
    }
}

void ADwmNpcActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    EndDialogue();
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(OneShotAnimationTimer);
    }
    Super::EndPlay(EndPlayReason);
}

void ADwmNpcActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // Measured, not assumed from WalkSpeed: this reads zero while standing, tapers on the
    // arrival frame when the step is clamped short, and stays correct while a
    // conversation has movement suspended. The Anim Blueprint's idle/walk transition
    // hangs off this, so a value that lied would show up as a walk cycle playing in place.
    if (DeltaSeconds > SMALL_NUMBER)
    {
        const FVector CurrentLocation = GetActorLocation();
        CurrentSpeed = FVector::Dist2D(CurrentLocation, PreviousLocation) / DeltaSeconds;
        PreviousLocation = CurrentLocation;
    }

    if (Activity == EDwmNpcActivity::Talking)
    {
        // Turn to face whoever is talking to him. This is a response to an explicit E
        // press, not proximity-driven behavior -- he does not react to the player merely
        // standing nearby, which is the scope line SCOPE.md draws.
        if (CurrentListener)
        {
            FVector ToListener = CurrentListener->GetActorLocation() - GetActorLocation();
            ToListener.Z = 0.0f;
            FaceDirection(ToListener, DeltaSeconds);
        }
        return;
    }

    // The seated states are handled HERE rather than in TickMovement because these
    // NPCs run with bEnableScriptedMovement off -- ConfigureProfileFromSource sets
    // it -- so TickMovement never reaches them. Talking sits above for the same
    // reason.
    if (Activity == EDwmNpcActivity::Seated)
    {
        TickSeatedGreetCheck(DeltaSeconds);
        return;
    }

    if (Activity == EDwmNpcActivity::StandingUp)
    {
        if (APawn* Greeter = GreetTarget.Get())
        {
            FVector ToGreeter = Greeter->GetActorLocation() - GetActorLocation();
            ToGreeter.Z = 0.0f;
            FaceDirection(ToGreeter, DeltaSeconds);
        }

        if (GetWorld() && GetWorld()->GetTimeSeconds() >= StandUpEndTime)
        {
            EnterActivity(EDwmNpcActivity::IdleAtMarker);
        }
        return;
    }

    // Keep facing whoever he stood up for until they leave. Without this he would
    // get up and immediately turn back to his placed facing, which reads as
    // ignoring the person who just walked in.
    if (APawn* Greeter = GreetTarget.Get())
    {
        FVector ToGreeter = Greeter->GetActorLocation() - GetActorLocation();
        ToGreeter.Z = 0.0f;
        FaceDirection(ToGreeter, DeltaSeconds);
        return;
    }

    if (bEnableScriptedMovement)
    {
        TickMovement(DeltaSeconds);
    }
}

namespace
{
    /** Live override for the seat facing.

        These NPCs are SPAWNED AT RUNTIME by the game mode, so they have no placed
        instance in the level whose EditAnywhere properties can be tweaked -- the
        "just change it in the editor" escape hatch does not exist for them, and
        settling which way a sofa faces should not cost a rebuild.

        Set DWM.Seat.YawOffset to -90 (or any angle) and restart PIE. The sentinel
        means "use the actor's own SeatYawOffset". */
    static TAutoConsoleVariable<float> CVarSeatYawOffset(
        TEXT("DWM.Seat.YawOffset"), -1000.0f,
        TEXT("Overrides the seated NPC yaw offset in degrees. -1000 = use the actor value."),
        ECVF_Default);

    /** Whether a clip authored for one skeleton will pose correctly on another.

        Neither pointer equality NOR an identical bone list is the right test, and this
        went through both before landing here.

        Asset packs each ship their own copy of the stock UE4 Mannequin skeleton, and the
        copies are not identical: Office_Desk's has 68 bones, Scanned3DPeoplePack's has 95
        -- the same mannequin plus extra bones. Both earlier tests rejected that pairing,
        which is what kept the Hillside NPCs standing.

        UE 5.3 handles this itself. UAnimSequence evaluation asks
        FSkeletonRemappingRegistry::GetRemapping for a source-to-target mapping, which is
        built on demand FOR ANY PAIR -- no compatibility registration needed -- and maps
        bones by NAME. The extra 27 bones simply go undriven.

        So the question that actually matters is whether every bone the clip drives EXISTS
        by name in the target. If one is missing the remap would drop it and the pose would
        be wrong, which is the case worth rejecting. */
    bool AreSkeletonsPoseCompatible(const USkeleton* AnimSkeleton, const USkeleton* MeshSkeleton)
    {
        if (!AnimSkeleton || !MeshSkeleton)
        {
            return false;
        }
        if (AnimSkeleton == MeshSkeleton)
        {
            return true;
        }

        const FReferenceSkeleton& AnimBones = AnimSkeleton->GetReferenceSkeleton();
        const FReferenceSkeleton& MeshBones = MeshSkeleton->GetReferenceSkeleton();
        if (AnimBones.GetNum() == 0)
        {
            return false;
        }

        int32 MissingCount = 0;
        FName FirstMissing = NAME_None;
        for (int32 Index = 0; Index < AnimBones.GetNum(); ++Index)
        {
            const FName BoneName = AnimBones.GetBoneName(Index);
            if (MeshBones.FindBoneIndex(BoneName) == INDEX_NONE)
            {
                if (MissingCount == 0)
                {
                    FirstMissing = BoneName;
                }
                ++MissingCount;
            }
        }

        if (MissingCount > 0)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[DWM NPC] '%s' drives %d bone(s) absent from '%s' (first: '%s'); ")
                TEXT("the remap would drop them, so the clip is rejected."),
                *GetNameSafe(AnimSkeleton), MissingCount,
                *GetNameSafe(MeshSkeleton), *FirstMissing.ToString());
            return false;
        }

        UE_LOG(LogTemp, Log,
            TEXT("[DWM NPC] '%s' (%d bones) remaps by name onto '%s' (%d bones)."),
            *GetNameSafe(AnimSkeleton), AnimBones.GetNum(),
            *GetNameSafe(MeshSkeleton), MeshBones.GetNum());
        return true;
    }
}

UAnimSequence* ADwmNpcActor::PickVariedSitIdle() const
{
    const USkeletalMesh* Mesh = NpcMesh ? NpcMesh->GetSkeletalMeshAsset() : nullptr;
    if (!Mesh)
    {
        return nullptr;
    }

    // Seated idles that read plausibly on a couch. The desk-bound clips in this pack
    // (Writing, Signing_Papers, Laptop, Computer_Idle) are deliberately left out --
    // they mime working at a surface that is not there.
    static const TCHAR* const Candidates[] = {
        TEXT("/Game/Office_Desk/Animation/Root_Motion/Office_Desk_Sit_Idle.Office_Desk_Sit_Idle"),
        TEXT("/Game/Office_Desk/Animation/Root_Motion/Office_Desk_Bored_Idle.Office_Desk_Bored_Idle"),
        TEXT("/Game/Office_Desk/Animation/Root_Motion/Office_Desk_Cellphone.Office_Desk_Cellphone")
    };

    TArray<UAnimSequence*> Usable;
    for (const TCHAR* Path : Candidates)
    {
        if (UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, Path))
        {
            if (AreSkeletonsPoseCompatible(Clip->GetSkeleton(), Mesh->GetSkeleton()))
            {
                Usable.Add(Clip);
            }
        }
    }

    return Usable.Num() > 0 ? Usable[FMath::RandRange(0, Usable.Num() - 1)] : nullptr;
}

AActor* ADwmNpcActor::FindSeatActor() const
{
    if (SeatMeshNameFilters.Num() == 0 || !GetWorld())
    {
        return nullptr;
    }

    // Same shape as DwmValleyLifeDirector's search for Maria's SM_RockingChair: match the
    // static mesh ASSET path rather than the actor name, because level actors get
    // auto-generated names while the asset they reference does not.
    AActor* Best = nullptr;
    float BestDistSq = SeatSearchRadius * SeatSearchRadius;

    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        AActor* Candidate = *It;
        if (!Candidate || Candidate == this)
        {
            continue;
        }

        const UStaticMeshComponent* MeshComp = Candidate->FindComponentByClass<UStaticMeshComponent>();
        const UStaticMesh* Asset = MeshComp ? MeshComp->GetStaticMesh() : nullptr;
        if (!Asset)
        {
            continue;
        }

        // Match the actor name TOO, not just the asset path: a level can rename the
        // placed instance (SM_Sofa_2) independently of the asset it references.
        const FString AssetPath = Asset->GetPathName();
        const FString ActorName = Candidate->GetName();
        bool bMatches = false;
        for (const FString& Filter : SeatMeshNameFilters)
        {
            if (!Filter.IsEmpty()
                && (AssetPath.Contains(Filter, ESearchCase::IgnoreCase)
                    || ActorName.Contains(Filter, ESearchCase::IgnoreCase)))
            {
                bMatches = true;
                break;
            }
        }
        if (!bMatches)
        {
            continue;
        }

        const float DistSq = FVector::DistSquared(Candidate->GetActorLocation(), GetActorLocation());
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            Best = Candidate;
        }
    }

    return Best;
}

void ADwmNpcActor::BeginSeated()
{
    const USkeletalMesh* Mesh = NpcMesh ? NpcMesh->GetSkeletalMeshAsset() : nullptr;
    if (!Mesh || !Mesh->GetSkeleton())
    {
        return;
    }

    if (!SitIdleAnimation)
    {
        // A random compatible seated idle per actor. Offsetting the start time was
        // not enough on its own: both NPCs were playing the same subtle motion.
        SitIdleAnimation = PickVariedSitIdle();
    }
    if (!SitIdleAnimation)
    {
        SitIdleAnimation = LoadObject<UAnimSequence>(nullptr,
            TEXT("/Game/Office_Desk/Animation/Root_Motion/"
                 "Office_Desk_Sit_Idle.Office_Desk_Sit_Idle"));
    }
    if (!SitToStandAnimation)
    {
        SitToStandAnimation = LoadObject<UAnimSequence>(nullptr,
            TEXT("/Game/Office_Desk/Animation/Root_Motion/"
                 "Office_Desk_Sit_To_Stand.Office_Desk_Sit_To_Stand"));
    }

    // Two ways this can fail, and BOTH must be caught BEFORE the seat offsets are
    // applied -- an NPC moved onto the cushion while still standing looks like he is
    // floating, which is worse than simply not sitting.
    //
    //   1. A clip from another skeleton would not sit him down, it would break his pose.
    //   2. An Anim Blueprint owns the pose every frame, and RefreshLocomotionAnimation
    //      deliberately refuses to fight it (see its early-out). Seating such an NPC
    //      would leave him in the Seated state wearing a standing pose.
    //
    // Staying standing is the honest failure in both cases. Seating an anim-BP NPC is a
    // Blueprint state-machine change, not something this can force from C++.
    bSeatedAnimationsUsable = !bUsingAnimBlueprint && SitIdleAnimation
        && AreSkeletonsPoseCompatible(SitIdleAnimation->GetSkeleton(), Mesh->GetSkeleton());

    if (!bSeatedAnimationsUsable)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[DWM NPC] '%s' cannot be seated (%s); leaving him standing rather ")
            TEXT("than posing him wrongly."),
            *GetNameSafe(this),
            bUsingAnimBlueprint ? TEXT("an Anim Blueprint owns his pose")
                                : TEXT("the sit clips target a different skeleton"));
        return;
    }

    // Onto the furniture. Sitting at the PLACED spot would seat him wherever the level
    // author happened to stand him -- mid-room and mid-air -- so the couch is located
    // first and he is moved onto it, exactly as Maria is moved onto her chair.
    FVector SeatOrigin = StandingLocation;
    FRotator SeatRotation = GetActorRotation();
    float AppliedYaw = 0.0f;

    if (const AActor* Seat = FindSeatActor())
    {
        SeatOrigin = Seat->GetActorLocation();
        const float CVarYaw = CVarSeatYawOffset.GetValueOnGameThread();
        AppliedYaw = (CVarYaw > -999.0f) ? CVarYaw : SeatYawOffset;
        SeatRotation = Seat->GetActorRotation() + FRotator(0.0f, AppliedYaw, 0.0f);

        UE_LOG(LogTemp, Log,
            TEXT("[DWM NPC] '%s' is sitting on '%s' (seat yaw %.0f + offset %.0f)."),
            *GetNameSafe(this), *GetNameSafe(Seat),
            Seat->GetActorRotation().Yaw, AppliedYaw);

        // Where he ends up once he stands: in front of the couch rather than inside it,
        // at the floor height he was originally placed at.
        //
        // The lateral offset has to carry through here as well. Two people who share
        // a couch and BOTH get up would otherwise rise into the same spot in front of
        // it, having been correctly separated while seated.
        const FVector StandForward = SeatRotation.Vector();
        const FVector StandRight = FRotationMatrix(SeatRotation).GetUnitAxis(EAxis::Y);
        StandingLocation = SeatOrigin
            + StandForward * StandClearance
            + StandRight * SeatLateralOffset;
        StandingLocation.Z = GetActorLocation().Z;
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[DWM NPC] '%s' found no [%s] within %.0f units; sitting where placed."),
            *GetNameSafe(this), *FString::Join(SeatMeshNameFilters, TEXT(", ")),
            SeatSearchRadius);
    }

    // The clips are authored for an office chair, so these offsets are how a couch gets
    // accounted for. SeatLateralOffset is what keeps two people sharing one couch out of
    // the same cushion.
    const FVector SeatForward = SeatRotation.Vector();
    const FVector SeatRight = FRotationMatrix(SeatRotation).GetUnitAxis(EAxis::Y);

    // Stored on the actor rather than in a local, so ReturnToSeat can put him back on
    // the same cushion when the player leaves.
    SeatedLocation = SeatOrigin
        - SeatForward * SeatedForwardOffset
        + SeatRight * SeatLateralOffset;
    SeatedLocation.Z += SeatedHeightOffset;
    SeatedRotation = FRotator(0.0f, SeatRotation.Yaw - MeshFacingYawOffset, 0.0f);

    SetActorLocation(SeatedLocation, false, nullptr, ETeleportType::TeleportPhysics);

    // SeatRotation is used for GEOMETRY above (which way is forward, which way is
    // along the cushion), but the actor's yaw is not where the character visually
    // looks -- MeshFacingYawOffset is the gap, and it is a quarter turn on every
    // copied mesh. Setting the actor to the couch's rotation raw would seat him
    // facing sideways out of it. This is the same correction FaceDirection applies,
    // and the same mistake issue #10 was.
    SetActorRotation(FRotator(0.0f, SeatRotation.Yaw - MeshFacingYawOffset, 0.0f));

    EnterActivity(EDwmNpcActivity::Seated);
}

bool ADwmNpcActor::ShouldStandForPlayer(const APawn* Player) const
{
    if (!Player || !GetWorld())
    {
        return false;
    }

    const FVector PlayerLocation = Player->GetActorLocation();
    if (FVector::DistSquared(PlayerLocation, GetActorLocation()) > FMath::Square(GreetRadius))
    {
        return false;
    }

    if (!bGreetRequiresLineOfSight)
    {
        return true;
    }

    // Trace between chest heights, not actor origins: a seated NPC's origin sits near
    // the cushion, and a trace from there would clip the couch or the floor and report
    // the player as hidden while he is standing right in front of them.
    const FVector From = GetActorLocation() + FVector(0.0f, 0.0f, 60.0f);
    const FVector To = PlayerLocation + FVector(0.0f, 0.0f, 40.0f);

    FCollisionQueryParams Params(SCENE_QUERY_STAT(DwmNpcGreetTrace), /*bTraceComplex=*/false);
    Params.AddIgnoredActor(this);
    Params.AddIgnoredActor(Player);

    // A BLOCKED trace means a wall is in the way -- stay seated.
    return !GetWorld()->LineTraceTestByChannel(From, To, ECC_Visibility, Params);
}

void ADwmNpcActor::TickSeatedGreetCheck(float DeltaSeconds)
{
    if (!bStandsForPlayer)
    {
        return;
    }

    // The stagger countdown runs EVERY frame, ahead of the poll throttle -- counting
    // it down inside the throttled section would quantise the delay to the poll
    // interval, making a 0.7s stagger land at 0.75s or 1.0s depending on phase.
    if (PendingStandTimer >= 0.0f)
    {
        PendingStandTimer -= DeltaSeconds;
        if (PendingStandTimer <= 0.0f)
        {
            PendingStandTimer = -1.0f;
            if (APawn* Waiting = PendingStandTarget.Get())
            {
                StandUpForPlayer(Waiting);
            }
        }
        return;
    }

    GreetCheckTimer -= DeltaSeconds;
    if (GreetCheckTimer > 0.0f)
    {
        return;
    }
    GreetCheckTimer = FMath::Max(0.05f, GreetCheckInterval);

    // Polled rather than driven by the interaction sphere's overlap. Overlap fires ONCE,
    // on entry, and at that instant the player is usually still behind a wall -- so a
    // visibility test there would fail and never be retried. Polling lets him stand the
    // moment he can actually see you.
    if (APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
    {
        if (ShouldStandForPlayer(Player))
        {
            if (StandDelay > 0.0f)
            {
                PendingStandTarget = Player;
                PendingStandTimer = StandDelay;
            }
            else
            {
                StandUpForPlayer(Player);
            }
        }
    }
}

void ADwmNpcActor::ReturnToSeat()
{
    if (!bStartsSeated || !bSeatedAnimationsUsable || Activity == EDwmNpcActivity::Seated)
    {
        return;
    }

    SetActorLocation(SeatedLocation, false, nullptr, ETeleportType::TeleportPhysics);
    SetActorRotation(SeatedRotation);

    // Disarm, so the next approach starts the stagger fresh rather than firing the
    // instant he sits back down.
    PendingStandTimer = -1.0f;
    PendingStandTarget = nullptr;

    EnterActivity(EDwmNpcActivity::Seated);
}

void ADwmNpcActor::StandUpForPlayer(APawn* Greeter)
{
    if (Activity != EDwmNpcActivity::Seated || !bStandsForPlayer || !Greeter)
    {
        return;
    }

    GreetTarget = Greeter;

    // Back to where he was placed: the seat offsets put him on the cushion, and the
    // get-up clip should end with him standing where the level author intended.
    SetActorLocation(StandingLocation, false, nullptr, ETeleportType::TeleportPhysics);

    const USkeletalMesh* StandMesh = NpcMesh ? NpcMesh->GetSkeletalMeshAsset() : nullptr;
    const bool bCanPlay = SitToStandAnimation && StandMesh
        && AreSkeletonsPoseCompatible(SitToStandAnimation->GetSkeleton(), StandMesh->GetSkeleton());

    if (bCanPlay && !bUsingAnimBlueprint)
    {
        StandUpEndTime = GetWorld()
            ? GetWorld()->GetTimeSeconds() + FMath::Max(0.1f, SitToStandAnimation->GetPlayLength())
            : 0.0f;
        EnterActivity(EDwmNpcActivity::StandingUp);
        NpcMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        NpcMesh->PlayAnimation(SitToStandAnimation, /*bLooping=*/false);
        return;
    }

    // No usable get-up clip: stand immediately rather than stay stuck sitting.
    EnterActivity(EDwmNpcActivity::IdleAtMarker);
}

// ---------------------------------------------------------------------------
// Scripted movement loop
// ---------------------------------------------------------------------------

void ADwmNpcActor::TickMovement(float DeltaSeconds)
{
    switch (Activity)
    {
    case EDwmNpcActivity::IdleAtMarker:
    {
        // Settle back to the placed facing while waiting, WITH FOOTWORK.
        //
        // This used to call SetActorRotation directly, which pivoted the body while the
        // idle clip kept playing -- issue #8, "rotates instead of animating steps". The
        // walk path already had the answer: StepTowardTarget computes the signed facing
        // error, calls SetTurningInPlace so RefreshLocomotionAnimation swaps in the
        // turn-left/right mocap loop, and only then moves. Arriving needs the same three
        // steps; it simply never got them.
        //
        // FaceDirection takes a world DIRECTION, not a rotation, so the placed facing has
        // to be handed over as its forward vector. There is no target position here to
        // derive a direction from the way the walk path does.
        const float SettleFacingError = FMath::FindDeltaAngleDegrees(
            GetActorRotation().Yaw,
            HomeRotation.Yaw);

        const bool bSettling = FMath::Abs(SettleFacingError) > MovementFacingToleranceDegrees;
        SetTurningInPlace(bSettling, SettleFacingError < 0.0f);
        if (bSettling)
        {
            FaceDirection(HomeRotation.Vector(), DeltaSeconds);
        }

        ActivityTimer -= DeltaSeconds;
        if (ActivityTimer <= 0.0f)
        {
            ChooseNextTrip();
        }
        break;
    }

    case EDwmNpcActivity::WalkingToTurbine:
        if (StepTowardTarget(DeltaSeconds))
        {
            EnterActivity(EDwmNpcActivity::WatchingTurbine);
        }
        break;

    case EDwmNpcActivity::WatchingTurbine:
    {
        // Face the turbine while looking at it, if one was assigned.
        if (TurbineActor)
        {
            FVector ToTurbine = TurbineActor->GetActorLocation() - GetActorLocation();
            ToTurbine.Z = 0.0f;
            FaceDirection(ToTurbine, DeltaSeconds);
        }

        ActivityTimer -= DeltaSeconds;
        if (ActivityTimer <= 0.0f)
        {
            EnterActivity(EDwmNpcActivity::ReturningHome);
        }
        break;
    }

    case EDwmNpcActivity::Wandering:
        if (StepTowardTarget(DeltaSeconds))
        {
            EnterActivity(EDwmNpcActivity::ReturningHome);
        }
        break;

    case EDwmNpcActivity::ReturningHome:
        if (StepTowardTarget(DeltaSeconds))
        {
            EnterActivity(EDwmNpcActivity::IdleAtMarker);
        }
        break;

    default:
        break;
    }
}

bool ADwmNpcActor::StepTowardTarget(float DeltaSeconds)
{
    const FVector Current = GetActorLocation();

    // Horizontal only: the ground trace owns Z, so a target sitting slightly above or
    // below him must not count as "distance still to cover" or he'd never arrive.
    FVector ToTarget = MoveTarget - Current;
    ToTarget.Z = 0.0f;
    const float Distance = ToTarget.Size();

    if (Distance <= ArrivalTolerance)
    {
        SetTurningInPlace(false);
        return true;
    }

    const FVector Direction = ToTarget / Distance;

    const float SignedFacingError = FMath::FindDeltaAngleDegrees(
        GetActorRotation().Yaw,
        Direction.Rotation().Yaw);

    // Turn before translating. Moving on the same frame that a return trip begins makes
    // the actor travel backward/sideways while RInterp catches up, which reads as
    // moonwalking. While aligning, play the matching mocap turn-in-place loop instead
    // of the forward walk cycle.
    const bool bNeedsTurn = FMath::Abs(SignedFacingError) > MovementFacingToleranceDegrees;
    SetTurningInPlace(bNeedsTurn, SignedFacingError < 0.0f);
    FaceDirection(Direction, DeltaSeconds);
    if (bNeedsTurn)
    {
        return false;
    }

    // Never overshoot on a long frame.
    const float StepDistance = FMath::Min(WalkSpeed * DeltaSeconds, Distance);

    FVector NextLocation = Current + (Direction * StepDistance);
    if (bSnapToGround)
    {
        NextLocation = ProjectToGround(NextLocation);
    }

    SetActorLocation(NextLocation);
    return false;
}

void ADwmNpcActor::SetTurningInPlace(bool bNewTurningInPlace, bool bNewTurningLeft)
{
    const bool bDirectionChanged = bNewTurningInPlace && bTurningLeft != bNewTurningLeft;
    if (bTurningInPlace == bNewTurningInPlace && !bDirectionChanged)
    {
        return;
    }

    bTurningInPlace = bNewTurningInPlace;
    bTurningLeft = bNewTurningLeft;
    RefreshLocomotionAnimation();
}

void ADwmNpcActor::FaceDirection(const FVector& WorldDirection, float DeltaSeconds)
{
    FVector Flat = WorldDirection;
    Flat.Z = 0.0f;
    if (Flat.IsNearlyZero())
    {
        return;
    }

    // Yaw only -- a walking NPC that pitches or rolls to face a target reads as broken.
    //
    // MeshFacingYawOffset is the gap between the actor's +X and where the character
    // actually looks. It is zero for the native setup, whose mesh the constructor
    // already corrects, and non-zero for a copied mesh that never got that correction.
    // Subtracting it aims the CHARACTER at the target instead of the actor's forward.
    const FRotator Target(0.0f, Flat.Rotation().Yaw - MeshFacingYawOffset, 0.0f);
    const FRotator Current = GetActorRotation();
    SetActorRotation(FMath::RInterpConstantTo(Current, Target, DeltaSeconds, TurnSpeed));
}

FVector ADwmNpcActor::ProjectToGround(const FVector& Point) const
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        return Point;
    }

    const FVector TraceStart = Point + FVector(0.0f, 0.0f, 200.0f);
    const FVector TraceEnd = Point - FVector(0.0f, 0.0f, 1000.0f);

    FCollisionQueryParams Params(SCENE_QUERY_STAT(DwmNpcGroundTrace), /*bTraceComplex=*/false);
    Params.AddIgnoredActor(this);

    FHitResult Hit;
    if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
    {
        return FVector(Point.X, Point.Y, Hit.Location.Z + GroundOffset);
    }

    // No ground found (hole in the terrain, trace too short) -- keep the existing height
    // rather than teleporting him into the void.
    return Point;
}

void ADwmNpcActor::ChooseNextTrip()
{
    const bool bCanWander = (WanderRadius > 0.0f);
    const bool bWanderThisTime = bCanWander && (FMath::FRand() < WanderChance);

    EnterActivity(bWanderThisTime ? EDwmNpcActivity::Wandering : EDwmNpcActivity::WalkingToTurbine);
}

FVector ADwmNpcActor::PickWanderPoint() const
{
    const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
    // sqrt keeps points evenly spread over the disc instead of clustering at the centre.
    const float Radius = WanderRadius * FMath::Sqrt(FMath::FRand());

    const FVector Point = HomeLocation
        + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);

    return bSnapToGround ? ProjectToGround(Point) : Point;
}

void ADwmNpcActor::EnterActivity(EDwmNpcActivity NewActivity)
{
    bTurningInPlace = false;
    Activity = NewActivity;

    switch (NewActivity)
    {
    case EDwmNpcActivity::IdleAtMarker:
        ActivityTimer = FMath::FRandRange(MarkerDwellSeconds.X, MarkerDwellSeconds.Y);
        break;

    case EDwmNpcActivity::WalkingToTurbine:
    {
        // Offset is in the actor's PLACED local space, not its current rotation -- he
        // turns while walking, and a target that rotated with him would drift away.
        const FVector WorldOffset = HomeRotation.RotateVector(TurbineWatchOffset);
        MoveTarget = HomeLocation + WorldOffset;
        if (bSnapToGround)
        {
            MoveTarget = ProjectToGround(MoveTarget);
        }
        break;
    }

    case EDwmNpcActivity::WatchingTurbine:
    {
        float WatchTime = FMath::FRandRange(TurbineWatchSeconds.X, TurbineWatchSeconds.Y);

        // Don't start walking away part-way through the gesture, whichever asset type is
        // actually driving it in the current mode.
        if (bUsingAnimBlueprint && GestureAtTurbineMontage)
        {
            WatchTime = FMath::Max(WatchTime, GestureAtTurbineMontage->GetPlayLength());
        }
        else if (!bUsingAnimBlueprint && GestureAtTurbineAnimation)
        {
            WatchTime = FMath::Max(WatchTime, GestureAtTurbineAnimation->GetPlayLength());
        }

        PlayOneShot(GestureAtTurbineMontage, GestureAtTurbineAnimation);
        ActivityTimer = WatchTime;
        break;
    }

    case EDwmNpcActivity::Wandering:
        MoveTarget = PickWanderPoint();
        break;

    case EDwmNpcActivity::ReturningHome:
        MoveTarget = bSnapToGround ? ProjectToGround(HomeLocation) : HomeLocation;
        break;

    case EDwmNpcActivity::Talking:
        break;
    }

    // In single-node mode, starting the activity's looping clip here would immediately
    // overwrite the one-shot WatchingTurbine just started. In Anim Blueprint mode this
    // whole call is a no-op (the montage layers over the state machine instead), so the
    // guard only has to consider the single-node case.
    const bool bJustStartedSingleNodeOneShot =
        !bUsingAnimBlueprint
        && NewActivity == EDwmNpcActivity::WatchingTurbine
        && GestureAtTurbineAnimation != nullptr;

    if (!bJustStartedSingleNodeOneShot)
    {
        RefreshLocomotionAnimation();
    }
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------

void ADwmNpcActor::RefreshLocomotionAnimation()
{
    if (!NpcMesh)
    {
        return;
    }

    if (bUsingAnimBlueprint)
    {
        // The state machine picks its own pose from GroundSpeed/Activity every frame.
        // Calling PlayAnimation here would force the mesh back into single-node mode and
        // silently kill the Anim Blueprint.
        return;
    }

    // The get-up clip is a one-shot started by StandUpForPlayer; re-selecting a
    // looping clip here would cut it off on its first frame.
    if (Activity == EDwmNpcActivity::StandingUp)
    {
        return;
    }

    if (Activity == EDwmNpcActivity::Seated && SitIdleAnimation && bSeatedAnimationsUsable)
    {
        NpcMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        NpcMesh->PlayAnimation(SitIdleAnimation, /*bLooping=*/true);

        // Two people on one couch breathing in perfect lockstep reads as a glitch. Same
        // desync issue #9 fixed for the standing idle, needed again because this branch
        // returns BEFORE that code runs.
        // Its OWN flag. bIdleStartRandomised belongs to the STANDING idle, and sharing
        // it meant whichever clip ran first consumed the one chance to offset -- so
        // the seated clip often started at frame zero on every actor.
        if (!bSeatedIdleRandomised && SitIdleAnimation->GetPlayLength() > 0.0f)
        {
            NpcMesh->SetPosition(
                FMath::FRandRange(0.0f, SitIdleAnimation->GetPlayLength()),
                /*bFireNotifies=*/false);
            bSeatedIdleRandomised = true;
        }
        return;
    }

    const bool bMoving =
        Activity == EDwmNpcActivity::WalkingToTurbine ||
        Activity == EDwmNpcActivity::Wandering ||
        Activity == EDwmNpcActivity::ReturningHome;

    UAnimSequence* Clip = IdleAnimation;
    if (bTurningInPlace)
    {
        Clip = bTurningLeft ? TurnLeftAnimation : TurnRightAnimation;
        if (!Clip)
        {
            Clip = IdleAnimation;
        }
    }
    else if (bMoving && WalkAnimation)
    {
        Clip = WalkAnimation;
    }
    if (!Clip)
    {
        return;
    }

    NpcMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    NpcMesh->PlayAnimation(Clip, /*bLooping=*/true);

    // Break the lockstep (issue #9). Several NPCs bootstrapped in the same pass
    // otherwise start the same loop on the same frame and breathe in unison.
    //
    // ONCE PER ACTOR, and only on the idle. This function also runs on turns,
    // activity changes and dialogue start/end; jumping to a random frame on those
    // would snap the pose mid-scene, which is worse than the bug being fixed.
    if (!bIdleStartRandomised && Clip == IdleAnimation && Clip->GetPlayLength() > 0.0f)
    {
        NpcMesh->SetPosition(FMath::FRandRange(0.0f, Clip->GetPlayLength()), /*bFireNotifies=*/false);
        bIdleStartRandomised = true;
    }
}

UAnimSequence* ADwmNpcActor::PickVariedIdleAnimation() const
{
    const USkeletalMesh* Mesh = NpcMesh ? NpcMesh->GetSkeletalMeshAsset() : nullptr;
    if (!Mesh || !Mesh->GetSkeleton())
    {
        return nullptr;
    }

    // The Scanned3DPeoplePack ships exactly one idle, so variety has to come from the
    // City Sample crowd set. Those are a DIFFERENT SKELETON, so every candidate is
    // checked against this mesh before it can be chosen -- forcing an incompatible
    // clip is the failure mode that produces broken-looking poses, and it is exactly
    // what DwmValleyLifeDirector's CanPlayOnMaria guards against for the same reason.
    static const TCHAR* const CandidatePaths[] =
    {
        TEXT("/Game/CitySampleCrowd/Character/Anims/Loco/MTN_Set/MTN_N_Idle.MTN_N_Idle"),
        TEXT("/Game/CitySampleCrowd/Character/Anims/Loco/MTN_Set/MTN_N_Idle_B.MTN_N_Idle_B"),
        TEXT("/Game/CitySampleCrowd/Character/Anims/Loco/MTN_Set/MTN_N_Idle_C.MTN_N_Idle_C"),
        TEXT("/Game/CitySampleCrowd/Character/Anims/Loco/MTN_Set/MTN_N_Idle_D.MTN_N_Idle_D"),
        TEXT("/Game/CitySampleCrowd/Character/Anims/Loco/MTN_Set/MTN_N_Idle_E.MTN_N_Idle_E")
    };

    TArray<UAnimSequence*> Compatible;
    for (const TCHAR* Path : CandidatePaths)
    {
        UAnimSequence* Candidate = LoadObject<UAnimSequence>(nullptr, Path);
        if (Candidate && Candidate->GetSkeleton() == Mesh->GetSkeleton())
        {
            Compatible.Add(Candidate);
        }
    }

    if (Compatible.Num() == 0)
    {
        return nullptr;
    }

    return Compatible[FMath::RandHelper(Compatible.Num())];
}

void ADwmNpcActor::PlayOneShot(UAnimMontage* Montage, UAnimSequence* Sequence)
{
    if (!NpcMesh)
    {
        return;
    }

    if (bUsingAnimBlueprint)
    {
        // Montages layer over the running state machine, so the walk/idle blend keeps
        // going underneath and the montage ends on its own -- no timer needed, and no
        // risk of knocking the mesh out of Anim Blueprint mode.
        if (Montage)
        {
            if (UAnimInstance* AnimInstance = NpcMesh->GetAnimInstance())
            {
                AnimInstance->Montage_Play(Montage);
            }
        }
        return;
    }

    if (!Sequence)
    {
        return;
    }

    NpcMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    NpcMesh->PlayAnimation(Sequence, /*bLooping=*/false);

    // Single-node mode has no state machine to fall back to, so return to the activity's
    // looping clip manually once the one-shot has played out.
    if (UWorld* World = GetWorld())
    {
        const float Length = Sequence->GetPlayLength();
        World->GetTimerManager().ClearTimer(OneShotAnimationTimer);
        if (Length > 0.0f)
        {
            World->GetTimerManager().SetTimer(OneShotAnimationTimer, this,
                &ADwmNpcActor::RefreshLocomotionAnimation, Length, /*bLoop=*/false);
        }
    }
}

// ---------------------------------------------------------------------------
// Overlap -- registers/unregisters this NPC as the character's current interaction
// target. Nothing here starts a conversation or changes his behavior; only an explicit
// E press does.
// ---------------------------------------------------------------------------

void ADwmNpcActor::OnInteractionSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
    bool bFromSweep, const FHitResult& SweepResult)
{
    if (APawn* Pawn = Cast<APawn>(OtherActor))
    {
        if (!Pawn->IsLocallyControlled())
        {
            return;
        }

        if (ADWM_DevCharacter* Character = Cast<ADWM_DevCharacter>(Pawn))
        {
            Character->SetActiveNpc(this);
        }
        else if (ADWM_DevPlayerController* Controller = Cast<ADWM_DevPlayerController>(Pawn->GetController()))
        {
            Controller->SetActiveNpc(this);
        }
        // NOT the stand-up trigger. Standing is driven by TickSeatedGreetCheck, which
        // also requires line of sight; this sphere fires once on entry, through walls,
        // and 250 units is close enough that he would rise as you arrive rather than as
        // you come through the door.

        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(NpcPromptMessageKey, -1.0f, FColor::White,
                FString::Printf(TEXT("Press E: Talk to %s"), *DisplayName.ToString()));
        }
    }
}

void ADwmNpcActor::OnInteractionSphereEndOverlap(UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
    if (APawn* Pawn = Cast<APawn>(OtherActor))
    {
        // Walking away closes the panel -- otherwise it would hang on screen with no way
        // to advance it, since E would no longer route back to this NPC.
        if (CurrentListener == Pawn)
        {
            EndDialogue();
        }

        // Stop tracking them once they leave, so he settles back to his placed
        // facing instead of staring after them.
        if (GreetTarget.Get() == Pawn)
        {
            GreetTarget = nullptr;

            // Back to the couch. Standing up is a REACTION to the player, so it should
            // not be permanent -- otherwise one pass through the room leaves them on
            // their feet for the rest of the game.
            ReturnToSeat();
        }
        if (ADWM_DevCharacter* Character = Cast<ADWM_DevCharacter>(Pawn))
        {
            Character->ClearActiveNpc(this);
        }
        else if (ADWM_DevPlayerController* Controller = Cast<ADWM_DevPlayerController>(Pawn->GetController()))
        {
            Controller->ClearActiveNpc(this);
        }
        if (GEngine)
        {
            GEngine->RemoveOnScreenDebugMessage(NpcPromptMessageKey);
        }
    }
}

// ---------------------------------------------------------------------------
// Dialogue state selection
// ---------------------------------------------------------------------------

EDwmDialogueState ADwmNpcActor::SelectStateForThisInteraction() const
{
    if (!IsHankProfile())
    {
        if (!bHasDeliveredOpening)
        {
            return EDwmDialogueState::Approach;
        }

        const FDwmDialogueSequence* AmbientSequence = DialogueByState.Find(EDwmDialogueState::Ambient);
        return (AmbientSequence && AmbientSequence->Lines.Num() > 0)
            ? EDwmDialogueState::Ambient
            : EDwmDialogueState::Approach;
    }

    // Act 3 payoff wins outright once the turbine beat has fired.
    if (bFarewellUnlocked)
    {
        return EDwmDialogueState::Farewell;
    }

    // First contact: the brief, then the follow-up prompt chains into QuestDetails.
    if (!bHasDeliveredOpening)
    {
        return EDwmDialogueState::Approach;
    }

    if (const UDwmGameInstance* GameInstance = GetGameInstance<UDwmGameInstance>())
    {
        TArray<FString> Partners;
        if (GameInstance->GetCompletedTradePartners(BuyerCommunityId, Partners))
        {
            bool bAllComplete = true;
            for (const FString& Required : RequiredSellerCommunityIds)
            {
                if (!Partners.Contains(Required))
                {
                    bAllComplete = false;
                    break;
                }
            }
            if (bAllComplete && RequiredSellerCommunityIds.Num() > 0)
            {
                return EDwmDialogueState::ReturnAllTradesComplete;
            }
        }
        // A failed query falls through to ReturnInProgress rather than claiming
        // completion -- the safe direction to be wrong in, since it can't skip content.
    }

    // First time back gets the real "how's it going" line; after that, flavor.
    if (ReturnVisitCount == 0)
    {
        return EDwmDialogueState::ReturnInProgress;
    }

    const FDwmDialogueSequence* AmbientSequence = DialogueByState.Find(EDwmDialogueState::Ambient);
    return (AmbientSequence && AmbientSequence->Lines.Num() > 0)
        ? EDwmDialogueState::Ambient
        : EDwmDialogueState::ReturnInProgress;
}

// ---------------------------------------------------------------------------
// Dialogue flow
// ---------------------------------------------------------------------------

void ADwmNpcActor::BeginDialogue(APawn* InteractingPawn)
{
    if (IsDialogueOpen() || !InteractingPawn)
    {
        return;
    }

    if (!DialogueWidgetClass)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DWM NPC] '%s' has no DialogueWidgetClass assigned -- nothing to display."),
            *GetNameSafe(this));
        return;
    }

    APlayerController* PlayerController = Cast<APlayerController>(InteractingPawn->GetController());
    if (!PlayerController)
    {
        return;
    }

    ActiveWidget = CreateWidget<UDwmDialogueWidget>(PlayerController, DialogueWidgetClass);
    if (!ActiveWidget)
    {
        return;
    }

    CurrentListener = InteractingPawn;
    CurrentState = SelectStateForThisInteraction();
    CurrentLineIndex = 0;

    if (IsHankProfile() && CurrentState != EDwmDialogueState::Approach)
    {
        ++ReturnVisitCount;
        if (UDwmGameInstance* GameInstance = GetGameInstance<UDwmGameInstance>())
        {
            GameInstance->IncrementHankReturnVisitCount();
        }
    }

    // Stop mid-loop and hold position for the conversation. Remembering what he was doing
    // means a two-line exchange doesn't reset his whole routine to the top.
    ActivityBeforeDialogue = Activity;
    Activity = EDwmNpcActivity::Talking;
    RefreshLocomotionAnimation();

    ActiveWidget->AddToViewport();
    ActiveWidget->SetOwningNpc(this);
    ShowCurrentLine();
}

void ADwmNpcActor::AdvanceDialogue()
{
    if (!IsDialogueOpen())
    {
        return;
    }

    ++CurrentLineIndex;

    const FDwmDialogueSequence* Sequence = DialogueByState.Find(CurrentState);
    const int32 LineCount = Sequence ? Sequence->Lines.Num() : 0;

    if (CurrentLineIndex < LineCount)
    {
        ShowCurrentLine();
        return;
    }

    // End of this state's lines. Approach is the one state that chains: its player prompt
    // ("What exactly do we need?") leads straight into the QuestDetails brief rather than
    // closing the panel.
    if (CurrentState == EDwmDialogueState::Approach)
    {
        const FDwmDialogueSequence* Details = DialogueByState.Find(EDwmDialogueState::QuestDetails);
        if (Details && Details->Lines.Num() > 0)
        {
            CurrentState = EDwmDialogueState::QuestDetails;
            CurrentLineIndex = 0;
            ShowCurrentLine();
            return;
        }

        bHasDeliveredOpening = true;
        EndDialogue();
        return;
    }

    if (CurrentState == EDwmDialogueState::QuestDetails)
    {
        // The opening brief is done; every later visit uses the return-visit states.
        bHasDeliveredOpening = true;
        if (IsHankProfile())
        {
            if (UDwmGameInstance* GameInstance = GetGameInstance<UDwmGameInstance>())
            {
                GameInstance->MarkHankOpeningDelivered();
            }
        }
    }

    if (CurrentState == EDwmDialogueState::Ambient)
    {
        // Advance the cursor so the next repeat visit starts on a different flavor line.
        const FDwmDialogueSequence* AmbientSequence = DialogueByState.Find(EDwmDialogueState::Ambient);
        if (AmbientSequence && AmbientSequence->Lines.Num() > 0)
        {
            AmbientCursor = (AmbientCursor + 1) % AmbientSequence->Lines.Num();
            if (IsHankProfile())
            {
                if (UDwmGameInstance* GameInstance = GetGameInstance<UDwmGameInstance>())
                {
                    GameInstance->SetHankAmbientCursor(AmbientCursor);
                }
            }
        }
    }

    if (CurrentState == EDwmDialogueState::ReturnAllTradesComplete)
    {
        // THE ACT 3 TRIGGER. Fires here -- when the player has read all the way through
        // the all-trades-complete conversation and clicked past its last line -- and
        // nowhere else. Deliberately NOT the moment the last trade lands (that is
        // DwmGameInstance's own bookkeeping, with no idea a conversation is even
        // happening) and NOT on arrival at Mountain (the player would see it start
        // without Hank having said anything). Walking away mid-conversation does not
        // reach this branch either: OnInteractionSphereEndOverlap calls EndDialogue()
        // directly, never AdvanceDialogue(), so leaving early cannot trigger the payoff.
        //
        // The two calls are paired at this one site rather than left for either to infer
        // the other from trade state, matching UnlockFarewell's own header comment ("call
        // this from whatever drives the turbine payoff") -- so the Farewell line can
        // never appear before the turbine has actually begun turning.
        if (IsHankProfile())
        {
            if (UDwmGameInstance* GameInstance = GetGameInstance<UDwmGameInstance>())
            {
                GameInstance->StartTurbineActThreePayoff();
            }
            UnlockFarewell();
        }
    }

    EndDialogue();
}

void ADwmNpcActor::ShowCurrentLine()
{
    const FDwmDialogueSequence* Sequence = DialogueByState.Find(CurrentState);
    if (!Sequence || Sequence->Lines.Num() == 0)
    {
        EndDialogue();
        return;
    }

    // Ambient is the only state that doesn't start at line 0 -- it resumes wherever the
    // last repeat visit left off, and shows exactly one line per visit.
    int32 EffectiveIndex = CurrentLineIndex;
    if (CurrentState == EDwmDialogueState::Ambient)
    {
        if (CurrentLineIndex > 0)
        {
            EndDialogue();
            return;
        }
        EffectiveIndex = AmbientCursor % Sequence->Lines.Num();
    }

    if (!Sequence->Lines.IsValidIndex(EffectiveIndex))
    {
        EndDialogue();
        return;
    }

    const FDwmDialogueLine& Line = Sequence->Lines[EffectiveIndex];
    const FDwmDialogueSequence* Details = DialogueByState.Find(EDwmDialogueState::QuestDetails);
    const bool bApproachChains = CurrentState == EDwmDialogueState::Approach
        && Details && Details->Lines.Num() > 0;
    const bool bHasNext = bApproachChains || Sequence->Lines.IsValidIndex(EffectiveIndex + 1);

    if (ActiveWidget)
    {
        ActiveWidget->ShowLine(Line, bHasNext);
    }

    PlayOneShot(TalkMontage, TalkAnimation);
}

void ADwmNpcActor::EndDialogue()
{
    if (ActiveWidget)
    {
        ActiveWidget->RemoveFromParent();
        ActiveWidget = nullptr;
    }
    CurrentListener = nullptr;
    CurrentLineIndex = 0;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(OneShotAnimationTimer);
    }

    if (Activity == EDwmNpcActivity::Talking)
    {
        // Resume the interrupted trip. Re-entering the activity recomputes its waypoint,
        // which matters if the conversation happened to start mid-walk.
        EnterActivity(ActivityBeforeDialogue == EDwmNpcActivity::Talking
            ? EDwmNpcActivity::IdleAtMarker
            : ActivityBeforeDialogue);
    }
    else
    {
        RefreshLocomotionAnimation();
    }

    if (!bEnableScriptedMovement)
    {
        SetActorRotation(HomeRotation);
    }
}

void ADwmNpcActor::UnlockFarewell()
{
    bFarewellUnlocked = true;
    if (IsHankProfile())
    {
        if (UDwmGameInstance* GameInstance = GetGameInstance<UDwmGameInstance>())
        {
            GameInstance->SetHankFarewellUnlocked();
        }
    }
}

#undef LOCTEXT_NAMESPACE
