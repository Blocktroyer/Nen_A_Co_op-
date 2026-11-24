// Nen - A Short Hike style controls

#pragma once

#include "CoreMinimal.h"
#include "NenPlayerController.h"
#include "HikePlayerController.generated.h"

class UInputAction;
class UNenInteractableComponent;
struct FInputActionValue;

/**
 *  Turns player input into intent for an AHikeCharacter:
 *  camera-relative movement, look, jump (press/hold), run and interact.
 *  Mapping contexts are added by the parent ANenPlayerController.
 */
UCLASS()
class NEN_API AHikePlayerController : public ANenPlayerController
{
	GENERATED_BODY()

public:

	/** Uses the nearest interactable (mailbox etc.) the pawn is standing in range of */
	UFUNCTION(BlueprintCallable, Category="Input")
	void Interact();

	/** Nearest enabled interactable overlapping the pawn, or null. Use it to show the prompt. */
	UFUNCTION(BlueprintPure, Category="Interaction")
	UNenInteractableComponent* GetBestInteractable() const;

protected:

	UPROPERTY(EditAnywhere, Category="Input|Actions")
	UInputAction* MoveAction;

	UPROPERTY(EditAnywhere, Category="Input|Actions")
	UInputAction* LookAction;

	UPROPERTY(EditAnywhere, Category="Input|Actions")
	UInputAction* MouseLookAction;

	UPROPERTY(EditAnywhere, Category="Input|Actions")
	UInputAction* JumpAction;

	UPROPERTY(EditAnywhere, Category="Input|Actions")
	UInputAction* RunAction;

	UPROPERTY(EditAnywhere, Category="Input|Actions")
	UInputAction* InteractAction;

	/** Tap run to toggle instead of holding it */
	UPROPERTY(EditAnywhere, Category="Input|Options")
	bool bToggleRun = false;

	/**
	 *  Movement is relative to the camera that is actually rendering (Black Eye or otherwise).
	 *  While the stick is held, the movement basis turns toward the camera at most this fast,
	 *  so a camera blend doesn't yank your direction. Degrees per second; 0 = follow the camera instantly.
	 */
	UPROPERTY(EditAnywhere, Category="Input|Options", meta=(ClampMin=0))
	float MoveBasisTurnRate = 120.f;

	virtual void SetupInputComponent() override;
	virtual void OnUnPossess() override;

	void HandleMove(const FInputActionValue& Value);
	void HandleMoveReleased();
	void HandleLook(const FInputActionValue& Value);
	void HandleJumpPressed();
	void HandleJumpReleased();
	void HandleRunPressed();
	void HandleRunReleased();

private:

	class AHikeCharacter* GetHikeCharacter() const;

	bool bRunToggledOn = false;

	float MoveBasisYaw = 0.f;
	bool bMoveBasisValid = false;
};
