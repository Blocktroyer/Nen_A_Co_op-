// NenInteractableComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "NenInteractableComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNenInteracted, AActor*, Interactor);

/**
 * Makes any actor usable by the player (mailbox, checkpoint, summit gate...).
 * The sphere is the interaction range: the character uses the nearest enabled
 * one it's standing inside. Listen to OnInteracted to react.
 */
UCLASS(ClassGroup = (Nen), meta = (BlueprintSpawnableComponent))
class NEN_API UNenInteractableComponent : public USphereComponent
{
	GENERATED_BODY()

public:
	UNenInteractableComponent();

	// Text for the on-screen prompt, e.g. "Open mailbox"
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FText Prompt = NSLOCTEXT("Nen", "Use", "Use");

	// Turn off to make this temporarily unusable
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bEnabled = true;

	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnNenInteracted OnInteracted;

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void Interact(AActor* Interactor);
};
