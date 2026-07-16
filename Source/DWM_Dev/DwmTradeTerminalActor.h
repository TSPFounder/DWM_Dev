// Day 18: an explicit, repeatable in-world trigger for the fixed economy round-trip demo.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DwmTradeTerminalActor.generated.h"

class ADWM_DevCharacter;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS()
class DWM_DEV_API ADwmTradeTerminalActor : public AActor
{
    GENERATED_BODY()

public:
    ADwmTradeTerminalActor();

    /** Invoked by ADWM_DevCharacter only after the player deliberately presses E in range. */
    void ExecuteDemoTrade(ADWM_DevCharacter* InteractingCharacter);

protected:
    virtual void BeginPlay() override;

private:
    UFUNCTION()
    void OnInteractionSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
        bool bFromSweep, const FHitResult& SweepResult);

    UFUNCTION()
    void OnInteractionSphereEndOverlap(UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    UPROPERTY(VisibleAnywhere, Category = "DWM|Economy")
    USphereComponent* InteractionSphere;

    UPROPERTY(VisibleAnywhere, Category = "DWM|Economy")
    UStaticMeshComponent* TerminalMesh;

    UPROPERTY(VisibleAnywhere, Category = "DWM|Economy")
    UTextRenderComponent* TerminalLabel;
};
