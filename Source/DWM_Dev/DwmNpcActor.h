// DwmNpcActor.h
// A simply-animated dialogue NPC with a scripted movement loop. Built for Hank (Mountain)
// as the proof-of-concept; the class itself is NPC-agnostic so the other six named NPCs
// can reuse it later with different data.
//
// MOVEMENT SCOPE -- this implements DWM_MVP_Storyline.md's "Movement (decided
// 2026-07-14)" section: the turbine-check loop (walk toward the turbine, pause, glance up
// at it, walk back) plus a small wander radius around his marker. That section was
// deferred to Week 9 by DWM_Coordination_Note.md and is being pulled forward here by
// explicit request.
//
// What is still deliberately NOT here, because it's the line SCOPE.md draws between
// "simply-animated dressing" (allowed) and "Autonomous NPC trade AI" (out of scope):
//   * No reaction to the player's position, no proximity-triggered behavior changes, no
//     decision-making. Hank runs the same fixed loop regardless of where the player is or
//     what they do.
//   * The one exception is pausing mid-loop while the player is actually TALKING to him,
//     which is a response to an explicit E press, not to proximity -- and the alternative
//     (wandering off mid-sentence) is obviously wrong.
//
// IMPLEMENTATION NOTE -- the storyline doc suggests "a Blueprint Timeline or a simple
// AI Move To" for the loop and "a Behavior Tree + Blackboard patrol-radius volume" for the
// wander layer. This uses neither: it's direct interpolation between waypoints in Tick.
// Reasons: the doc's own constraint is "Two hardcoded points, no pathfinding complexity",
// and AI Move To / Behavior Trees drag in an AIController, a NavMesh bake on every
// community level, and a capsule that can collide with the player -- three new failure
// modes for a walk between two known spots. If real pathfinding is ever needed (obstacles,
// multi-floor), swap this for the BT approach then, not now.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DwmDialogueTypes.h"
#include "DwmNpcActor.generated.h"

class ADWM_DevCharacter;
class APawn;
class UAnimMontage;
class UAnimSequence;
class UDwmDialogueWidget;
class UPrimitiveComponent;
class USceneComponent;
class USkeletalMeshComponent;
class USphereComponent;

/** Named DWM NPC configurations supported by this actor. */
UENUM(BlueprintType)
enum class EDwmNpcProfile : uint8
{
    Hank UMETA(DisplayName = "Hank"),
    Sophia UMETA(DisplayName = "Sophia"),
    Owen UMETA(DisplayName = "Owen"),
    Nathan UMETA(DisplayName = "Nathan"),
    Maria UMETA(DisplayName = "Maria"),
    DeShawn UMETA(DisplayName = "DeShawn"),
    Mike UMETA(DisplayName = "Mike"),
    Kai UMETA(DisplayName = "Kai")
};

/** What the NPC is doing right now. Drives both movement and which animation loops. */
UENUM(BlueprintType)
enum class EDwmNpcActivity : uint8
{
    /** Standing at the marker, waiting out the idle dwell before the next trip. */
    IdleAtMarker    UMETA(DisplayName = "Idle At Marker"),

    /** Walking out to the turbine viewpoint. */
    WalkingToTurbine UMETA(DisplayName = "Walking To Turbine"),

    /** Stopped at the viewpoint, facing the turbine, playing the glance-up gesture. */
    WatchingTurbine UMETA(DisplayName = "Watching Turbine"),

    /** Walking to a random point inside the wander radius. */
    Wandering       UMETA(DisplayName = "Wandering"),

    /** Walking back to the marker. */
    ReturningHome   UMETA(DisplayName = "Returning Home"),

    /** Paused for a conversation. Movement is suspended and he faces the player. */
    Talking         UMETA(DisplayName = "Talking")
};

UCLASS()
class DWM_DEV_API ADwmNpcActor : public AActor
{
    GENERATED_BODY()

public:
    ADwmNpcActor();

    virtual void Tick(float DeltaSeconds) override;

    // ------------------------------------------------------------------
    // Interaction -- mirrors ADwmTradeTerminalActor's proven overlap + explicit E press
    // pattern exactly. Nothing happens on overlap alone.
    // ------------------------------------------------------------------

    /** Called by ADWM_DevCharacter when the player presses E with this NPC in range and
        no dialogue already open. Opens the panel at whichever state fits current progress
        and suspends the movement loop. */
    void BeginDialogue(APawn* InteractingPawn);

    /** Called by ADWM_DevCharacter when the player presses E while this NPC's panel is
        already open. Shows the next line, or closes the panel if that was the last one. */
    void AdvanceDialogue();

    /** Closes the panel and resumes the movement loop. Safe to call when nothing is open. */
    UFUNCTION(BlueprintCallable, Category = "DWM|Dialogue")
    void EndDialogue();

    UFUNCTION(BlueprintPure, Category = "DWM|Dialogue")
    bool IsDialogueOpen() const { return ActiveWidget != nullptr; }

    /** Unlocks the Farewell state (the Act 3 "There it goes" line). Call this from
        whatever drives the turbine payoff -- it is deliberately NOT inferred from trade
        state, because completing the trades and the turbine actually spinning are two
        different beats in the storyline. */
    UFUNCTION(BlueprintCallable, Category = "DWM|Dialogue")
    void UnlockFarewell();

    UFUNCTION(BlueprintPure, Category = "DWM|NPC")
    EDwmNpcActivity GetCurrentActivity() const { return Activity; }

    /** Horizontal speed in cm/s, measured from actual movement rather than assumed from
        WalkSpeed -- so it reads zero while standing, ramps correctly on the arrival
        frame, and stays honest if movement is ever paused mid-step. Consumed by
        UDwmNpcAnimInstance. */
    UFUNCTION(BlueprintPure, Category = "DWM|NPC")
    float GetCurrentSpeed() const { return CurrentSpeed; }

    /** Applies a named dialogue/animation profile and copies the already-placed visual
        mesh. Called before FinishSpawningActor by the level bootstrap. */
    void ConfigureProfileFromSource(EDwmNpcProfile NewProfile, USkeletalMeshComponent* SourceMesh);

    /** Configures an invisible interaction-only copy of an NPC. This is used by Maria's
        Valley life director so the already-placed morph-pose Blueprint remains the one
        the player sees while the proven DWM dialogue system follows it. */
    void ConfigureDialogueProxy(EDwmNpcProfile NewProfile);

    UFUNCTION(BlueprintPure, Category = "DWM|NPC")
    EDwmNpcProfile GetNpcProfile() const { return NpcProfile; }

    // ------------------------------------------------------------------
    // Animation -- TWO SUPPORTED MODES, chosen automatically at BeginPlay.
    //
    // A skeletal mesh with no animation asset and no Anim Blueprint renders in its
    // reference pose, which for this asset is the T-pose. Either mode below fixes that.
    //
    //   * ANIM BLUEPRINT MODE (preferred) -- used when the mesh has an Anim Blueprint
    //     assigned whose parent class is UDwmNpcAnimInstance. The state machine owns
    //     idle/walk, so transitions BLEND. One-shots play as montages.
    //
    //   * SINGLE-NODE MODE (fallback) -- used when no Anim Blueprint is assigned. The
    //     sequence properties below are played directly. Simpler to set up, but it CUTS
    //     between clips with no blend. Fine for a distant background NPC, visibly rough
    //     up close.
    //
    // Both paths work, so the Anim Blueprint can be built or dropped without code
    // changes. Detection is on the mesh's assigned anim class, not a flag, so there's no
    // way to set the toggle and the asset inconsistently.
    //
    // Pick every clip from the character pack's existing mocap set (59 animations,
    // 31 unique per the storyline doc) -- no new animation content is needed.
    // ------------------------------------------------------------------

    /** Looping idle, used whenever he's standing still. REQUIRED -- if this is unset the
        NPC stays in its T-pose and BeginPlay logs an error saying so. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Animation")
    UAnimSequence* IdleAnimation = nullptr;

    /** Looping walk cycle, used while moving. If unset he slides along in the idle pose,
        so this is effectively required once scripted movement is enabled. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Animation")
    UAnimSequence* WalkAnimation = nullptr;

    /** Mocap turn-in-place loops used while Hank aligns with a new route. These keep
        a 90/180-degree reversal from looking like a walking clip sliding sideways. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Animation")
    UAnimSequence* TurnLeftAnimation = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Animation")
    UAnimSequence* TurnRightAnimation = nullptr;

    /** One-shot "glances up at it" gesture played at the turbine viewpoint. Optional --
        without it he just stands and looks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Animation")
    UAnimSequence* GestureAtTurbineAnimation = nullptr;

    /** Optional one-shot gesture played when a dialogue line appears, after which he
        returns to idle. Leave unset to stay on idle throughout. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Animation")
    UAnimSequence* TalkAnimation = nullptr;

    // --- Anim Blueprint mode only ---
    // Montages layer on top of a running state machine; raw sequences can't, which is why
    // these are separate properties rather than reusing the two above. Create each one
    // from its sequence (right-click the animation -> Create -> Create AnimMontage) and
    // put it on an upper-body slot if you want him to keep walking while gesturing.
    // Leave them empty to run the state machine with no one-shots at all.

    /** Montage for the "glances up at it" beat at the turbine. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Animation")
    UAnimMontage* GestureAtTurbineMontage = nullptr;

    /** Montage played per dialogue line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Animation")
    UAnimMontage* TalkMontage = nullptr;

    // ------------------------------------------------------------------
    // Scripted movement loop
    // ------------------------------------------------------------------

    /** Master switch. Turn off to get pure stationary set-dressing (idle only). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Movement")
    bool bEnableScriptedMovement = true;

    /** Where he stands to look at the turbine, as an offset from his placed location, in
        the actor's own local space. The default sends him 400cm forward -- point the
        actor's +X at the turbine when placing him and this needs no editing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Movement")
    FVector TurbineWatchOffset = FVector(400.0f, 0.0f, 0.0f);

    /** Optional: the placed turbine actor. If set, he turns to face it while watching
        (more reliable than assuming the offset direction points at it). If left empty he
        simply keeps facing the way he walked. */
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "DWM|Movement")
    AActor* TurbineActor = nullptr;

    /** Radius around the marker for the wander layer. Set to 0 to disable wandering and
        run only the marker <-> turbine loop. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Movement", meta = (ClampMin = "0.0"))
    float WanderRadius = 250.0f;

    /** Chance (0-1) that a given trip is a wander rather than a turbine check. Keeps the
        loop from being the identical two-point shuttle every time -- the repetitive tell
        the storyline doc specifically calls out. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float WanderChance = 0.5f;

    /** Walk speed, cm/s. TUNE THIS TO THE WALK CLIP: too fast and the feet skate, too
        slow and he moonwalks. Play in PIE, watch the feet, adjust. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Movement", meta = (ClampMin = "1.0"))
    float WalkSpeed = 120.0f;

    /** Turn rate in degrees/sec while walking or turning to face something. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Movement", meta = (ClampMin = "1.0"))
    float TurnSpeed = 180.0f;

    /** Hank turns in place until he is this close to the travel direction. Without this
        gate he starts translating during a 180-degree reversal and appears to moonwalk
        or slide sideways. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Movement", meta = (ClampMin = "0.0", ClampMax = "90.0"))
    float MovementFacingToleranceDegrees = 8.0f;

    /** How close counts as arrived, cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Movement", meta = (ClampMin = "1.0"))
    float ArrivalTolerance = 25.0f;

    /** Min/max seconds to stand at the marker between trips. Randomized so the loop isn't
        metronomic. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Movement")
    FVector2D MarkerDwellSeconds = FVector2D(6.0f, 12.0f);

    /** Min/max seconds spent looking at the turbine. If GestureAtTurbineAnimation is set
        and runs longer than the rolled value, the gesture's own length wins. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Movement")
    FVector2D TurbineWatchSeconds = FVector2D(3.0f, 6.0f);

    /** Trace down to keep his feet on the terrain rather than floating or sinking. Worth
        leaving on for Mountain's Snowy Peaks terrain, which is not flat. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Movement")
    bool bSnapToGround = true;

    /** Vertical offset applied after a ground trace -- raise this if his feet sink into
        the terrain, lower it if he hovers. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Movement")
    float GroundOffset = 0.0f;

    // ------------------------------------------------------------------
    // Dialogue data
    // ------------------------------------------------------------------

    /** Name shown on the interaction prompt, e.g. "Hank". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Dialogue")
    FText DisplayName;

    /** Per-state lines. Defaults are populated in the constructor with Hank's actual copy
        from DWM_MVP_Dialogue.md, so a placed instance works without retyping it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Dialogue")
    TMap<EDwmDialogueState, FDwmDialogueSequence> DialogueByState;

    /** The panel widget class. Assign a Blueprint subclass of UDwmDialogueWidget. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Dialogue")
    TSubclassOf<UDwmDialogueWidget> DialogueWidgetClass;

    /** Communities this NPC's quest requires a completed trade with, for choosing between
        ReturnInProgress and ReturnAllTradesComplete.
        Deliberately keyed on COMMUNITY, not resource: City sells two resources in one
        stop, and Hillside's resource is mid-rename (Timber -> engineering_services), so a
        resource-keyed list would break on that change while a community-keyed one does not. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Dialogue")
    TArray<FString> RequiredSellerCommunityIds;

    /** The community whose purchases count toward the above (the buyer Hank speaks for). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Dialogue")
    FString BuyerCommunityId = TEXT("mountain");

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void OnInteractionSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
        bool bFromSweep, const FHitResult& SweepResult);

    UFUNCTION()
    void OnInteractionSphereEndOverlap(UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    // --- Movement ---
    void TickMovement(float DeltaSeconds);
    /** Moves toward MoveTarget. Returns true on arrival. */
    bool StepTowardTarget(float DeltaSeconds);
    void FaceDirection(const FVector& WorldDirection, float DeltaSeconds);
    void SetTurningInPlace(bool bNewTurningInPlace, bool bNewTurningLeft = false);
    void EnterActivity(EDwmNpcActivity NewActivity);
    void ChooseNextTrip();
    FVector PickWanderPoint() const;
    /** Drops Point onto the terrain below it. Returns Point unchanged if nothing is hit. */
    FVector ProjectToGround(const FVector& Point) const;

    // --- Dialogue ---
    EDwmDialogueState SelectStateForThisInteraction() const;
    void ShowCurrentLine();
    void PopulateDefaultHankDialogue();
    void PopulateHillsideDialogue();
    bool IsHankProfile() const { return NpcProfile == EDwmNpcProfile::Hank; }

    // --- Animation ---
    /** Single-node mode: plays the looping clip matching the current activity.
        Anim Blueprint mode: no-op, since the state machine owns this. */
    void RefreshLocomotionAnimation();
    /** Plays a one-shot: the montage in Anim Blueprint mode, the sequence in single-node
        mode. Either argument may be null. */
    void PlayOneShot(UAnimMontage* Montage, UAnimSequence* Sequence);

    /** Actor-space root. Keeping the skeletal mesh below this root lets the actor face
        Unreal's +X travel direction while compensating for Hank's +Y-authored mesh. */
    UPROPERTY(VisibleAnywhere, Category = "DWM|NPC")
    USceneComponent* SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "DWM|NPC")
    USkeletalMeshComponent* NpcMesh;

    UPROPERTY(VisibleAnywhere, Category = "DWM|NPC")
    USphereComponent* InteractionSphere;

    UPROPERTY()
    UDwmDialogueWidget* ActiveWidget = nullptr;

    UPROPERTY()
    APawn* CurrentListener = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "DWM|NPC")
    EDwmNpcProfile NpcProfile = EDwmNpcProfile::Hank;

    // --- Movement state ---
    EDwmNpcActivity Activity = EDwmNpcActivity::IdleAtMarker;
    /** Where he was placed. Captured in BeginPlay; every waypoint is relative to it. */
    FVector HomeLocation = FVector::ZeroVector;
    FRotator HomeRotation = FRotator::ZeroRotator;
    FVector MoveTarget = FVector::ZeroVector;
    /** True only while rotating to face the current travel direction. */
    bool bTurningInPlace = false;
    bool bTurningLeft = false;
    /** Seconds left in a stationary activity (marker dwell / turbine watch). */
    float ActivityTimer = 0.0f;
    /** Activity to resume once dialogue ends. */
    EDwmNpcActivity ActivityBeforeDialogue = EDwmNpcActivity::IdleAtMarker;

    /** Measured horizontal speed, cm/s. Published to the Anim Blueprint. */
    float CurrentSpeed = 0.0f;
    FVector PreviousLocation = FVector::ZeroVector;

    /** True when the mesh had an Anim Blueprint assigned at BeginPlay. Decided once
        rather than per-frame -- swapping the anim class at runtime isn't supported here
        and there's no reason to. */
    bool bUsingAnimBlueprint = false;

    // --- Dialogue state ---
    EDwmDialogueState CurrentState = EDwmDialogueState::Approach;
    int32 CurrentLineIndex = 0;

    /** False until the player has finished the Approach/QuestDetails opening once, after
        which return-visit states apply instead. */
    bool bHasDeliveredOpening = false;

    /** Set by UnlockFarewell(); gates the Act 3 line. */
    bool bFarewellUnlocked = false;

    /** Rotating index so repeat Ambient visits cycle through the flavor lines rather than
        replaying the first one every time. */
    int32 AmbientCursor = 0;

    /** How many times the player has come back after the opening. First return gets the
        doc's ReturnInProgress line; later ones fall through to Ambient flavor. */
    int32 ReturnVisitCount = 0;

    FTimerHandle OneShotAnimationTimer;
};
