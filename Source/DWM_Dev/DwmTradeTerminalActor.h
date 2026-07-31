// Day 18: an explicit, repeatable in-world trigger for the economy round-trip.
// Day 20: generalized -- the trade this terminal executes (resource, amount, buyer/seller
// community) is now a configurable per-instance property set, not hardcoded in C++. Each
// storyline stop places its own instance of this same class with its own trade configured.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DwmTradeTerminalActor.generated.h"

class APawn;
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

    /** Invoked only after the player deliberately presses E in range. */
    void ExecuteTrade(APawn* InteractingPawn);

    // ------------------------------------------------------------------
    // Day 20: per-instance trade configuration. Set these in the Details panel for each
    // placed terminal -- e.g. Hillside's terminal gets SellerCommunityId="hillside",
    // BuyerCommunityId="mountain", ResourceId="timber". Defaults reproduce the original Day
    // 18 Mountain-buys-Grain-from-Valley demo trade exactly, so an unconfigured/default
    // instance still behaves the same way it always did.
    //
    // DELIBERATELY NAMED "Buyer"/"Seller", NOT "From"/"To": FDwmEconomyWriter::WriteTrade's
    // own FromCommunityId/ToCommunityId parameters mean the OPPOSITE of what those same words
    // would suggest here (WriteTrade's From = whoever PAYS Stone; its To = whoever RECEIVES
    // Stone). Reusing "From"/"To" as this actor's property names, with this actor's From
    // meaning "provides the resource" (the seller), would silently collide with WriteTrade's
    // own convention where From means the payer -- an easy, hard-to-notice direction bug.
    // Buyer/Seller has no such collision and matches how the storyline itself is phrased
    // ("Hillside -> Mountain: Timber" reads naturally as seller -> buyer).
    // ------------------------------------------------------------------

    /** Community paying Stone and receiving the resource (e.g. "mountain"). Passed as
        WriteTrade's FromCommunityId (the payer). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Trade")
    FString BuyerCommunityId = TEXT("mountain");

    /** Community receiving Stone and providing the resource (e.g. "valley"). Passed as
        WriteTrade's ToCommunityId (the receiver). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Trade")
    FString SellerCommunityId = TEXT("valley");

    /** Resource id being traded (must match a seeded Resources.ResourceId, e.g. "grain"). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Trade")
    FString ResourceId = TEXT("grain");

    /** Stone amount BuyerCommunityId pays. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Trade")
    double Amount = 20.0;

    /** Quantity of ResourceId exchanged. Defaults to a 1:1 ratio with Amount, matching the
        only precedent that exists (the original 20 Stone for 20 Grain trade) -- not derived
        from any real balancing data, override per-instance if a different ratio is wanted. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Trade")
    double Quantity = 20.0;

    /** Optional flavor description shown in the "Press E" prompt and terminal label, e.g.
        "Trade 20 Stone for Timber". Leave empty to auto-generate a generic description from
        the fields above instead. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWM|Trade")
    FString TradeDescription;

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

    FString BuildTradeDescription() const;

    UPROPERTY(VisibleAnywhere, Category = "DWM|Economy")
    USphereComponent* InteractionSphere;

    UPROPERTY(VisibleAnywhere, Category = "DWM|Economy")
    UStaticMeshComponent* TerminalMesh;

    UPROPERTY(VisibleAnywhere, Category = "DWM|Economy")
    UTextRenderComponent* TerminalLabel;
};
