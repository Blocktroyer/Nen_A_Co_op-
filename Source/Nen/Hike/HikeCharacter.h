// Nen - A Short Hike style character body

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "HikeCharacter.generated.h"

class UHikeMovementComponent;
class USpringArmComponent;
class UCameraComponent;

/**
 *  The pawn the Hike player controller drives.
 *  Movement feel lives in UHikeMovementComponent; input lives in AHikePlayerController.
 *  The spring-arm camera here is a placeholder until the camera pass of the 3C.
 */
UCLASS()
class NEN_API AHikeCharacter : public ACharacter
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	USpringArmComponent* CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	UCameraComponent* FollowCamera;

public:

	AHikeCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category="Hike")
	UHikeMovementComponent* GetHikeMovement() const;

	// ---- intent, called by the player controller (or touch UI)

	UFUNCTION(BlueprintCallable, Category="Hike|Input")
	void HikeJumpPressed();

	UFUNCTION(BlueprintCallable, Category="Hike|Input")
	void HikeJumpReleased();

	UFUNCTION(BlueprintCallable, Category="Hike|Input")
	void SetWantsToRun(bool bRun);

	// ---- cosmetic hooks for sound / VFX / animation (fired once, not during network replays)

	/** Jumped off the ground (bWasFlap = false) or flapped with a golden feather (true) */
	UFUNCTION(BlueprintImplementableEvent, Category="Hike|Events")
	void OnHikeJump(bool bWasFlap);

	UFUNCTION(BlueprintImplementableEvent, Category="Hike|Events")
	void OnGlideChanged(bool bIsGliding);

	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }

protected:

	/** Prints speed / feathers / glide state on screen for tuning */
	UPROPERTY(EditAnywhere, Category="Hike|Debug")
	bool bShowMovementDebug = true;
};
