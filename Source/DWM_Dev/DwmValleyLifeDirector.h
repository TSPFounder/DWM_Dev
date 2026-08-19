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

    static constexpr float MariaWalkSpeed = 125.0f;
    static constexpr float MariaTurnSpeed = 180.0f;
    static constexpr float MariaStartDelay = 1.25f;
};
