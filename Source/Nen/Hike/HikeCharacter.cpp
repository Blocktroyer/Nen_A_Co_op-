// Nen - A Short Hike style character body

#include "HikeCharacter.h"
#include "HikeMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/Engine.h"

AHikeCharacter::AHikeCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UHikeMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);

	// the body faces where it moves; the controller only steers the camera
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// placeholder follow camera (replaced in the camera pass)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 650.f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

UHikeMovementComponent* AHikeCharacter::GetHikeMovement() const
{
	return Cast<UHikeMovementComponent>(GetCharacterMovement());
}

void AHikeCharacter::HikeJumpPressed()
{
	if (UHikeMovementComponent* Move = GetHikeMovement())
	{
		Move->SetJumpInput(true);
	}
}

void AHikeCharacter::HikeJumpReleased()
{
	if (UHikeMovementComponent* Move = GetHikeMovement())
	{
		Move->SetJumpInput(false);
	}
}

void AHikeCharacter::SetWantsToRun(bool bRun)
{
	if (UHikeMovementComponent* Move = GetHikeMovement())
	{
		Move->SetRunInput(bRun);
	}
}

void AHikeCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bShowMovementDebug && IsLocallyControlled() && GEngine)
	{
		if (const UHikeMovementComponent* Move = GetHikeMovement())
		{
			const FVector Vel = Move->Velocity;
			const FString State = Move->IsGliding() ? TEXT("GLIDE")
				: Move->IsFalling() ? TEXT("AIR")
				: Move->IsRunning() ? TEXT("RUN") : TEXT("WALK");

			GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.f, FColor::Cyan,
				FString::Printf(TEXT("[%s] %s  speed %.0f  velZ %.0f  feathers %d/%d  coyote %.2f"),
					*GetName(), *State, Vel.Size2D(), Vel.Z,
					Move->GetFlapsRemaining(), Move->GoldenFeathers, Move->GetCoyoteTimeRemaining()));
		}
	}
}
