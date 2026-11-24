// NenInteractableComponent.cpp
#include "NenInteractableComponent.h"

UNenInteractableComponent::UNenInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Overlap-only range sphere: never blocks movement, never something to stand on
	InitSphereRadius(150.f);
	SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	SetGenerateOverlapEvents(true);
	CanCharacterStepUpOn = ECB_No;
	SetHiddenInGame(true);
}

void UNenInteractableComponent::Interact(AActor* Interactor)
{
	if (bEnabled)
	{
		OnInteracted.Broadcast(Interactor);
	}
}
