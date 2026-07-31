// DwmNpcAnimInstance.h
// Anim Blueprint backing class for ADwmNpcActor.
//
// Why this exists: the actor's first pass drove animation in single-node mode
// (PlayAnimation on the mesh), which works but CUTS between clips -- idle snaps to walk
// with no transition. This class exposes the actor's movement state to an Anim Blueprint
// state machine so idle/walk can blend properly instead.
//
// The C++ side only publishes state. It deliberately does NOT decide which animation
// plays -- that's the state machine's job, and keeping the decision in the graph is what
// lets the clips, blend times, and transition rules be retuned without a recompile.
//
// This is OPTIONAL. ADwmNpcActor detects at BeginPlay whether an Anim Blueprint was
// assigned to its mesh: if one was, it hands over to this; if not, it falls back to the
// original single-node behavior. Both paths work, so the AnimBP can be built (or dropped)
// without touching code.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "DwmNpcActor.h"
#include "DwmNpcAnimInstance.generated.h"

UCLASS()
class DWM_DEV_API UDwmNpcAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    /** Horizontal speed in cm/s. Drive a 1D blend space with this, or just threshold it
        for a plain idle/walk state machine. */
    UPROPERTY(BlueprintReadOnly, Category = "DWM|Animation")
    float GroundSpeed = 0.0f;

    /** GroundSpeed above MovingSpeedThreshold. The simplest thing to drive an
        idle <-> walk transition off. */
    UPROPERTY(BlueprintReadOnly, Category = "DWM|Animation")
    bool bIsMoving = false;

    /** True while a conversation panel is open. Useful if you want a distinct listening
        pose rather than the plain idle. */
    UPROPERTY(BlueprintReadOnly, Category = "DWM|Animation")
    bool bIsTalking = false;

    /** The actor's full activity state, if the graph wants to distinguish (say) watching
        the turbine from standing at the marker. */
    UPROPERTY(BlueprintReadOnly, Category = "DWM|Animation")
    EDwmNpcActivity Activity = EDwmNpcActivity::IdleAtMarker;

    /** Speed above which bIsMoving flips true. Slightly above zero so a frame of
        numerical noise while standing still doesn't twitch the state machine. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DWM|Animation")
    float MovingSpeedThreshold = 3.0f;

    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
    UPROPERTY()
    ADwmNpcActor* OwningNpc = nullptr;
};
