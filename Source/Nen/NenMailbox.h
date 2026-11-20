// NenMailbox.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NenMailbox.generated.h"

class UStaticMeshComponent;
class UNenInteractableComponent;
class UNenBackendSubsystem;

/**
 * Base for every mailbox in the world. Holds no mail itself: the inbox lives in
 * UNenBackendSubsystem, and every mailbox is just a way to open it.
 * Make Blueprint children (BP_Mailbox_Forest...) for the mesh, flag animation and widget.
 */
UCLASS(Abstract)
class NEN_API ANenMailbox : public AActor
{
	GENERATED_BODY()

public:
	ANenMailbox();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mailbox")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mailbox")
	TObjectPtr<UNenInteractableComponent> Interactable;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Raise or lower the flag. Called on BeginPlay and whenever the inbox changes.
	UFUNCTION(BlueprintImplementableEvent, Category = "Mailbox")
	void OnUnreadChanged(bool bHasUnread);

	// The player used the mailbox: open the mail widget here.
	// The widget reads the subsystem's Inbox, and calls MarkLettersRead / SendPigeonBP.
	UFUNCTION(BlueprintImplementableEvent, Category = "Mailbox")
	void OnOpened(AActor* Interactor);

	// This player's backend (inbox, sending), or null outside of play
	UFUNCTION(BlueprintPure, Category = "Mailbox")
	UNenBackendSubsystem* GetBackend() const;

private:
	UFUNCTION()
	void HandleInteracted(AActor* Interactor);

	UFUNCTION()
	void HandleInboxChanged();
};
