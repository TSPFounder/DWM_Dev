#include "DwmNpcAnimInstance.h"

void UDwmNpcAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();

    // Cached rather than resolved every frame. TryGetPawnOwner() is not usable here --
    // ADwmNpcActor is a plain AActor, not a Pawn, deliberately (it has no controller and
    // no movement component; see DwmNpcActor.h's note on why AI Move To was not used).
    OwningNpc = Cast<ADwmNpcActor>(GetOwningActor());
}

void UDwmNpcAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    if (!OwningNpc)
    {
        // Can happen for the preview instance in the Anim Blueprint editor, which has no
        // owning actor. Falling back to zeroed state keeps the preview on the idle branch
        // instead of leaving stale values from the last real tick.
        OwningNpc = Cast<ADwmNpcActor>(GetOwningActor());
        if (!OwningNpc)
        {
            GroundSpeed = 0.0f;
            bIsMoving = false;
            bIsTalking = false;
            Activity = EDwmNpcActivity::IdleAtMarker;
            return;
        }
    }

    GroundSpeed = OwningNpc->GetCurrentSpeed();
    bIsMoving = GroundSpeed > MovingSpeedThreshold;
    Activity = OwningNpc->GetCurrentActivity();
    bIsTalking = (Activity == EDwmNpcActivity::Talking);
}
