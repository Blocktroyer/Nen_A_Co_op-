// Nen - A Short Hike style controls

#include "HikePlayerController.h"
#include "HikeCharacter.h"
#include "NenInteractableComponent.h"
#include "EnhancedInputComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "InputActionValue.h"
#include "Nen.h"

void AHikePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		UE_LOG(LogNen, Error, TEXT("'%s' needs an Enhanced Input component."), *GetNameSafe(this));
		return;
	}

	if (MoveAction)
	{
		Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AHikePlayerController::HandleMove);
		Input->BindAction(MoveAction, ETriggerEvent::Completed, this, &AHikePlayerController::HandleMoveReleased);
		Input->BindAction(MoveAction, ETriggerEvent::Canceled, this, &AHikePlayerController::HandleMoveReleased);
	}
	if (LookAction)
	{
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AHikePlayerController::HandleLook);
	}
	if (MouseLookAction)
	{
		Input->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AHikePlayerController::HandleLook);
	}
	if (JumpAction)
	{
		Input->BindAction(JumpAction, ETriggerEvent::Started, this, &AHikePlayerController::HandleJumpPressed);
		Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &AHikePlayerController::HandleJumpReleased);
		Input->BindAction(JumpAction, ETriggerEvent::Canceled, this, &AHikePlayerController::HandleJumpReleased);
	}
	if (RunAction)
	{
		Input->BindAction(RunAction, ETriggerEvent::Started, this, &AHikePlayerController::HandleRunPressed);
		Input->BindAction(RunAction, ETriggerEvent::Completed, this, &AHikePlayerController::HandleRunReleased);
		Input->BindAction(RunAction, ETriggerEvent::Canceled, this, &AHikePlayerController::HandleRunReleased);
	}
	if (InteractAction)
	{
		Input->BindAction(InteractAction, ETriggerEvent::Started, this, &AHikePlayerController::Interact);
	}
}

void AHikePlayerController::Interact()
{
	if (UNenInteractableComponent* Interactable = GetBestInteractable())
	{
		Interactable->Interact(GetPawn());
	}
}

UNenInteractableComponent* AHikePlayerController::GetBestInteractable() const
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return nullptr;
	}

	TArray<UPrimitiveComponent*> Overlapping;
	ControlledPawn->GetOverlappingComponents(Overlapping);

	UNenInteractableComponent* Best = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();
	for (UPrimitiveComponent* Component : Overlapping)
	{
		UNenInteractableComponent* Interactable = Cast<UNenInteractableComponent>(Component);
		if (!Interactable || !Interactable->bEnabled)
		{
			continue;
		}
		const float DistanceSq = FVector::DistSquared(Interactable->GetComponentLocation(), ControlledPawn->GetActorLocation());
		if (DistanceSq < BestDistanceSq)
		{
			Best = Interactable;
			BestDistanceSq = DistanceSq;
		}
	}
	return Best;
}

void AHikePlayerController::OnUnPossess()
{
	// don't leave a stuck jump/run on the pawn we're leaving
	if (AHikeCharacter* Hike = GetHikeCharacter())
	{
		Hike->HikeJumpReleased();
		Hike->SetWantsToRun(false);
	}
	bRunToggledOn = false;

	Super::OnUnPossess();
}

AHikeCharacter* AHikePlayerController::GetHikeCharacter() const
{
	return Cast<AHikeCharacter>(GetPawn());
}

void AHikePlayerController::HandleMove(const FInputActionValue& Value)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	// camera-relative: up on the stick = away from the camera that is rendering.
	// Black Eye cameras don't have to drive the control rotation, so read the real view.
	const float CameraYaw = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation().Yaw : GetControlRotation().Yaw;

	if (!bMoveBasisValid || MoveBasisTurnRate <= 0.f)
	{
		MoveBasisYaw = CameraYaw;
		bMoveBasisValid = true;
	}
	else
	{
		const float MaxStep = MoveBasisTurnRate * GetWorld()->GetDeltaSeconds();
		MoveBasisYaw += FMath::Clamp(FMath::FindDeltaAngleDegrees(MoveBasisYaw, CameraYaw), -MaxStep, MaxStep);
	}

	const FVector2D Stick = Value.Get<FVector2D>();
	const FRotator YawOnly(0.f, MoveBasisYaw, 0.f);
	const FVector Forward = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y);

	ControlledPawn->AddMovementInput(Forward, Stick.Y);
	ControlledPawn->AddMovementInput(Right, Stick.X);
}

void AHikePlayerController::HandleMoveReleased()
{
	// next press snaps straight to the current camera
	bMoveBasisValid = false;
}

void AHikePlayerController::HandleLook(const FInputActionValue& Value)
{
	const FVector2D Look = Value.Get<FVector2D>();
	AddYawInput(Look.X);
	AddPitchInput(Look.Y);
}

void AHikePlayerController::HandleJumpPressed()
{
	if (AHikeCharacter* Hike = GetHikeCharacter())
	{
		Hike->HikeJumpPressed();
	}
}

void AHikePlayerController::HandleJumpReleased()
{
	if (AHikeCharacter* Hike = GetHikeCharacter())
	{
		Hike->HikeJumpReleased();
	}
}

void AHikePlayerController::HandleRunPressed()
{
	AHikeCharacter* Hike = GetHikeCharacter();
	if (!Hike)
	{
		return;
	}

	if (bToggleRun)
	{
		bRunToggledOn = !bRunToggledOn;
		Hike->SetWantsToRun(bRunToggledOn);
	}
	else
	{
		Hike->SetWantsToRun(true);
	}
}

void AHikePlayerController::HandleRunReleased()
{
	if (!bToggleRun)
	{
		if (AHikeCharacter* Hike = GetHikeCharacter())
		{
			Hike->SetWantsToRun(false);
		}
	}
}
