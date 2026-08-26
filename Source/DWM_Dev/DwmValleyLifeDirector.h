// DwmValleyLifeDirector.h
// Valley-specific set-piece: keeps the placed BP_Morphpose_Maria actor intact, walks
// her to the nearest porch rocking chair, seats her, supplies an interaction-only
// dialogue proxy, removes weapon visuals, and gives the placed chickens unsynchronised
// ambient animation.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DwmValleyLifeDirector.generated.h"

class ADwmNpcActor;
class UAnimSequence;
class USkeletalMeshComponent;

UENUM()
enum class EDwmMariaRoutinePhase : uint8
{
    Waiting,
    WalkingToPorch,
    SittingDown,
    Seated
};

UCLASS()
class DWM_DEV_API ADwmValleyLifeDirector : public AActor
{
    GENERATED_BODY()

public:
    ADwmValleyLifeDirector();

    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void BeginPlay() override;

private:
    void DiscoverValleyActors();
    void StartMariaWalk();
    void TickMariaRoutine(float DeltaSeconds);
    void SeatMaria();
    void SuppressMariaWeapons();
    void UpdateDialogueProxy();
    void UpdateChickenAnimations();
    void PlayRandomChickenAnimation(int32 ChickenIndex, float NowSeconds);
    bool CanPlayOnMaria(const UAnimSequence* Animation) const;

    UPROPERTY()
    AActor* MariaActor = nullptr;

    UPROPERTY()
    USkeletalMeshComponent* MariaMesh = nullptr;

    UPROPERTY()
    AActor* RockingChairActor = nullptr;

    UPROPERTY()
    ADwmNpcActor* DialogueProxy = nullptr;

    UPROPERTY()
    TArray<USkeletalMeshComponent*> ChickenMeshes;

    TArray<float> NextChickenAnimationTimes;

    UPROPERTY()
    UAnimSequence* MariaWalkAnimation = nullptr;

    UPROPERTY()
    UAnimSequence* MariaRelaxedIdleAnimation = nullptr;

    UPROPERTY()
    UAnimSequence* MariaStandToSitAnimation = nullptr;

    UPROPERTY()
    UAnimSequence* MariaSitIdleAnimation = nullptr;

    UPROPERTY()
    TArray<UAnimSequence*> ChickenAnimations;

    EDwmMariaRoutinePhase MariaPhase = EDwmMariaRoutinePhase::Waiting;
    FVector MariaApproachLocation = FVector::ZeroVector;
    FVector MariaSeatLocation = FVector::ZeroVector;
    FRotator MariaSeatRotation = FRotator::ZeroRotator;
    float RoutineStartTime = 0.0f;
    float SitTransitionEndTime = 0.0f;
    float NextWeaponSuppressionTime = 0.0f;

    /** Seat fit against the chair, exposed so it can be dialled in against
        SM_RockingChair in the editor instead of needing a recompile per attempt.
        The defaults are the values that were previously hardcoded here, tuned by
        hand against an office chair -- a rocking chair will likely want different
        ones, which is the point of making them editable. */
    UPROPERTY(EditAnywhere, Category = "DWM|Valley")
    float MariaSeatForwardOffset = 8.0f;

    /** BP_Morphpose_Maria's mesh is authored 90cm below its Character origin, so a
        seat position has to be raised by that much to land the mesh on the seat. */
    UPROPERTY(EditAnywhere, Category = "DWM|Valley")
    float MariaSeatHeightOffset = 90.0f;

    /** How far in front of the seat she stops before the sit transition begins.
        Wants to match roughly how far the stand-to-sit clip travels backward, or
        the slide onto the seat will not line up with the animation. */
    UPROPERTY(EditAnywhere, Category = "DWM|Valley")
    float MariaApproachDistance = 95.0f;

    /** Where the sit transition started, so she can be moved onto the seat across
        the clip rather than teleported before it. */
    FVector MariaSitStartLocation = FVector::ZeroVector;
    FRotator MariaSitStartRotation = FRotator::ZeroRotator;
    float SitTransitionStartTime = 0.0f;

    static constexpr float MariaWalkSpeed = 125.0f;
    static constexpr float MariaTurnSpeed = 180.0f;
    static constexpr float MariaStartDelay = 1.25f;
};
