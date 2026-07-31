// Reusable, F-interactable swing door for any DWM level.
// Create a Blueprint child of this actor for each visual door style, then assign its mesh and
// place the Hinge component at the real hinge edge. Do not modify Unreal's AStaticMeshActor.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DwmInteractiveDoor.generated.h"

class APawn;
class APlayerController;
class UPrimitiveComponent;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;

UCLASS(BlueprintType, Blueprintable)
class DWM_DEV_API ADwmInteractiveDoor : public AActor
{
	GENERATED_BODY()

public:
	ADwmInteractiveDoor();

	/** Opens a closed door or closes an open one. Called by the player's F interaction. */
	UFUNCTION(BlueprintCallable, Category = "DWM|Door")
	void ToggleDoor(APawn* InteractingPawn);

	UFUNCTION(BlueprintPure, Category = "DWM|Door")
	bool IsOpen() const { return bIsOpen; }

	/** The pivot for the swing. Move this component to the physical hinge in a Blueprint child. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWM|Door")
	USceneComponent* Hinge;

	/** Assign a door-only static mesh here. Keep the frame/wall as a separate static mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWM|Door")
	UStaticMeshComponent* DoorMesh;

	/** Invisible range used to show the E prompt and enable interaction. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWM|Door")
	USphereComponent* InteractionSphere;

	/** Degrees the door swings around its local Z axis. Use a negative value for the other side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Door", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float OpenYawDegrees = 90.0f;

	/** Door rotation speed in degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Door", meta = (ClampMin = "1.0"))
	float OpenSpeedDegreesPerSecond = 180.0f;

	/** Distance at which the player can use this door. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Door", meta = (ClampMin = "50.0"))
	float InteractionRadius = 175.0f;

	/** Prevents the player from changing this door's state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Door")
	bool bLocked = false;

	/** Text displayed when the player enters the interaction range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Door")
	FText DoorName;

	/** Existing level actors that should move with this door, such as a separate glass pane,
	 * handle, or logo decal. Set these on the placed door instance. Their current placement is
	 * preserved, then they are attached to Hinge when play begins. */
	UPROPERTY(EditInstanceOnly, Category = "DWM|Door")
	TArray<TObjectPtr<AActor>> AttachedVisualActors;

	/** Alternative to direct actor references for World Partition levels. Give the separate glass,
	 * decals, and hardware this Actor Tag; every matching actor keeps its placement and follows
	 * this door's hinge when play begins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Door")
	FName VisualAttachmentTag;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UFUNCTION()
	void OnInteractionSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnInteractionSphereEndOverlap(UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** Fallback for pawns whose controller is not DWM_DevPlayerController. */
	void ToggleDoorFromFallbackInput();

	FString BuildPrompt() const;
	uint64 GetPromptMessageKey() const;

	FRotator ClosedHingeRotation;
	FRotator TargetHingeRotation;
	bool bIsOpen = false;
	bool bFallbackInputBound = false;
	TWeakObjectPtr<APawn> FallbackInteractingPawn;
	TWeakObjectPtr<APlayerController> FallbackInputController;
};
