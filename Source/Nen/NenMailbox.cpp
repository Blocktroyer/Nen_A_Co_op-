// NenMailbox.cpp
#include "NenMailbox.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "NenBackendSubsystem.h"
#include "NenInteractableComponent.h"

ANenMailbox::ANenMailbox()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;

	Interactable = CreateDefaultSubobject<UNenInteractableComponent>(TEXT("Interactable"));
	Interactable->SetupAttachment(Mesh);
	Interactable->Prompt = NSLOCTEXT("Nen", "OpenMailbox", "Open mailbox");
}

UNenBackendSubsystem* ANenMailbox::GetBackend() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UNenBackendSubsystem>() : nullptr;
}

void ANenMailbox::BeginPlay()
{
	Super::BeginPlay();

	Interactable->OnInteracted.AddDynamic(this, &ANenMailbox::HandleInteracted);
	if (UNenBackendSubsystem* Backend = GetBackend())
	{
		Backend->OnInboxChanged.AddDynamic(this, &ANenMailbox::HandleInboxChanged);
	}
	HandleInboxChanged();   // show the current state straight away
}

void ANenMailbox::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The subsystem outlives this level, so stop listening before the mailbox goes away
	if (UNenBackendSubsystem* Backend = GetBackend())
	{
		Backend->OnInboxChanged.RemoveDynamic(this, &ANenMailbox::HandleInboxChanged);
	}
	Super::EndPlay(EndPlayReason);
}

void ANenMailbox::HandleInteracted(AActor* Interactor)
{
	OnOpened(Interactor);
}

void ANenMailbox::HandleInboxChanged()
{
	const UNenBackendSubsystem* Backend = GetBackend();
	OnUnreadChanged(Backend && Backend->HasUnreadMail());
}
