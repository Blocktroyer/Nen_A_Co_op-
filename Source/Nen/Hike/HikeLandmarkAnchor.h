// Nen - landmark focus helper for the Short Hike style camera

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HikeLandmarkAnchor.generated.h"

class UArrowComponent;

/**
 *  Place this on a landmark (a summit, a tall cliff).
 *  Every frame it moves PlayerAnchor onto the local player and turns it toward the landmark.
 *  A Black Eye camera that follows PlayerAnchor in "Subject Heading" mode therefore swings
 *  around the landmark as the player walks around it, keeping the landmark in view behind them.
 *
 *  It holds no Black Eye references: all framing, damping and blending live on the camera.
 */
UCLASS()
class NEN_API AHikeLandmarkAnchor : public AActor
{
	GENERATED_BODY()

public:

	AHikeLandmarkAnchor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category="Hike|Camera")
	USceneComponent* GetPlayerAnchor() const { return PlayerAnchor; }

	UFUNCTION(BlueprintPure, Category="Hike|Camera")
	USceneComponent* GetLandmarkPoint() const { return LandmarkPoint; }

protected:

	/** The point of interest. Move the actor (or this component) to the summit */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	USceneComponent* LandmarkPoint;

	/** Follows the player; its yaw points toward the landmark (clamped by the settings below) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	USceneComponent* PlayerAnchor;

	UPROPERTY(VisibleAnywhere, Category="Components")
	UArrowComponent* AnchorArrow;

	/** Camera heading (world yaw, degrees) the landmark pulls away from */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Camera")
	float BaseYaw = 45.f;

	/** When the player comes within EnterRadius, take BaseYaw from the camera they were using */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Camera")
	bool bCaptureBaseYawOnEnter = true;

	/** Set slightly larger than the trigger volume so the capture happens before the camera switch */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Camera", meta=(ClampMin=0, Units="cm"))
	float EnterRadius = 3000.f;

	/** 0 = the heading never turns, 1 = the landmark always sits straight behind the player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Camera", meta=(ClampMin=0, ClampMax=1))
	float HeadingInfluence = 0.6f;

	/** The heading never turns further than this from BaseYaw. Past it, BaseYaw is dragged along with the player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Camera", meta=(ClampMin=0, ClampMax=180))
	float MaxYawOffset = 70.f;

	/** How quickly the anchor turns toward its target heading (0 = instantly). Black Eye's damping adds on top */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Camera", meta=(ClampMin=0))
	float YawInterpSpeed = 3.f;

	/** Closer than this the direction to the landmark is unstable, so the heading holds still */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Camera", meta=(ClampMin=0, Units="cm"))
	float MinDistance = 300.f;

	/** Which local player to track (0 for online co-op; each client tracks its own player) */
	UPROPERTY(EditAnywhere, Category="Hike|Camera", meta=(ClampMin=0))
	int32 PlayerIndex = 0;

	UPROPERTY(EditAnywhere, Category="Hike|Debug")
	bool bDrawDebug = false;

private:

	float CurrentYaw = 0.f;
	bool bPlayerWasInside = false;
};
