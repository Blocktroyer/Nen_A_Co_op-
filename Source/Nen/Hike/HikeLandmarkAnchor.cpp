// Nen - landmark focus helper for the Short Hike style camera

#include "HikeLandmarkAnchor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/ArrowComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

AHikeLandmarkAnchor::AHikeLandmarkAnchor()
{
	PrimaryActorTick.bCanEverTick = true;

	// run after character movement so the anchor sits on this frame's player position
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	// purely local camera helper: every client computes its own
	bReplicates = false;

	LandmarkPoint = CreateDefaultSubobject<USceneComponent>(TEXT("LandmarkPoint"));
	RootComponent = LandmarkPoint;

	PlayerAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("PlayerAnchor"));
	PlayerAnchor->SetupAttachment(LandmarkPoint);
	PlayerAnchor->SetUsingAbsoluteLocation(true);
	PlayerAnchor->SetUsingAbsoluteRotation(true);

	AnchorArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("AnchorArrow"));
	AnchorArrow->SetupAttachment(PlayerAnchor);
	AnchorArrow->ArrowSize = 2.f;
	AnchorArrow->SetHiddenInGame(true);
}

void AHikeLandmarkAnchor::BeginPlay()
{
	Super::BeginPlay();

	CurrentYaw = BaseYaw;
	PlayerAnchor->SetWorldRotation(FRotator(0.f, CurrentYaw, 0.f));
}

void AHikeLandmarkAnchor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, PlayerIndex);
	if (!Player)
	{
		return; // dedicated server, or the player hasn't spawned yet
	}

	const FVector PlayerLocation = Player->GetActorLocation();
	const FVector LandmarkLocation = LandmarkPoint->GetComponentLocation();
	const FVector ToLandmark(LandmarkLocation.X - PlayerLocation.X, LandmarkLocation.Y - PlayerLocation.Y, 0.f);
	const float Distance = ToLandmark.Size();

	// Start from wherever the player was looking, so the switch into this camera doesn't swing
	const bool bInside = Distance <= EnterRadius;
	if (bInside && !bPlayerWasInside && bCaptureBaseYawOnEnter)
	{
		if (const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, PlayerIndex))
		{
			BaseYaw = CameraManager->GetCameraRotation().Yaw;
			CurrentYaw = BaseYaw;
		}
	}
	bPlayerWasInside = bInside;

	// Turn part of the way toward the landmark, never more than MaxYawOffset
	float TargetYaw = CurrentYaw;
	if (Distance > MinDistance && HeadingInfluence > 0.f)
	{
		const float ToLandmarkYaw = ToLandmark.Rotation().Yaw;
		float Delta = FMath::FindDeltaAngleDegrees(BaseYaw, ToLandmarkYaw);

		// Once the pull is maxed out, drag BaseYaw along with the player. Walking all the way
		// around the landmark then keeps turning the camera with you, instead of flipping to the
		// other side when the landmark passes directly behind the base heading.
		const float DragLimit = FMath::Min(MaxYawOffset / HeadingInfluence, 150.f);
		if (FMath::Abs(Delta) > DragLimit)
		{
			const float Side = FMath::Sign(Delta);
			BaseYaw = FRotator::NormalizeAxis(ToLandmarkYaw - Side * DragLimit);
			Delta = Side * DragLimit;
		}

		TargetYaw = BaseYaw + FMath::Clamp(Delta * HeadingInfluence, -MaxYawOffset, MaxYawOffset);
	}

	const float Remaining = FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw);
	const float Alpha = (YawInterpSpeed > 0.f) ? FMath::Min(1.f, YawInterpSpeed * DeltaSeconds) : 1.f;
	CurrentYaw = FRotator::NormalizeAxis(CurrentYaw + Remaining * Alpha);

	PlayerAnchor->SetWorldLocationAndRotation(PlayerLocation, FRotator(0.f, CurrentYaw, 0.f));

	if (bDrawDebug)
	{
		DrawDebugLine(GetWorld(), PlayerLocation, LandmarkLocation, FColor::Yellow, false, -1.f, 0, 2.f);
		DrawDebugDirectionalArrow(GetWorld(), PlayerLocation,
			PlayerLocation + FRotator(0.f, CurrentYaw, 0.f).Vector() * 300.f, 60.f, FColor::Cyan, false, -1.f, 0, 3.f);
		DrawDebugCircle(GetWorld(), LandmarkLocation, EnterRadius, 64, FColor::Orange, false, -1.f, 0, 4.f,
			FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
	}
}
