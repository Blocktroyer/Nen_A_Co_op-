// Nen - A Short Hike style movement ("Controls" in 3C)

#include "HikeMovementComponent.h"
#include "HikeCharacter.h"
#include "GameFramework/Character.h"

UHikeMovementComponent::UHikeMovementComponent()
{
	// Ground: quick to start, quick to stop, turns almost instantly
	MaxWalkSpeed = 450.f;
	MinAnalogWalkSpeed = 40.f;
	MaxAcceleration = 3000.f;
	BrakingDecelerationWalking = 2600.f;
	GroundFriction = 10.f;
	bOrientRotationToMovement = true;
	RotationRate = FRotator(0.f, 900.f, 0.f);

	// Air: heavier than default gravity so the arc reads crisply
	GravityScale = 2.f;
	JumpZVelocity = 820.f;
	AirControl = NormalAirControl;
	BrakingDecelerationFalling = NormalAirBrakingDeceleration;
	FallingLateralFriction = 0.f;

	NavAgentProps.bCanJump = true;
}

void UHikeMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	FlapsRemaining = GoldenFeathers;
}

// ------------------------------------------------------------------------------------------- input

void UHikeMovementComponent::SetJumpInput(bool bPressed)
{
	if (bPressed && !bJumpHeld)
	{
		bJumpPressedPending = true;
		JumpBufferRemaining = JumpBufferTime;
	}
	bJumpHeld = bPressed;
}

void UHikeMovementComponent::SetRunInput(bool bPressed)
{
	bWantsToRun = bPressed;
}

void UHikeMovementComponent::SetGoldenFeathers(int32 NewCount)
{
	GoldenFeathers = FMath::Max(0, NewCount);
	if (IsMovingOnGround())
	{
		FlapsRemaining = GoldenFeathers;
	}
	else
	{
		FlapsRemaining = FMath::Min(FlapsRemaining, GoldenFeathers);
	}
}

// ------------------------------------------------------------------------------------------- per-move logic

void UHikeMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);

	if (IsFalling())
	{
		CoyoteTimeRemaining = FMath::Max(0.f, CoyoteTimeRemaining - DeltaSeconds);
	}

	// Variable jump height: releasing jump while rising cuts the ascent
	if (bJumpRising)
	{
		JumpRiseTime += DeltaSeconds;
		if (!IsFalling() || Velocity.Z <= 0.f)
		{
			bJumpRising = false;
		}
		else if (!bJumpHeld && JumpRiseTime >= MinJumpRiseTime)
		{
			Velocity.Z *= JumpCutMultiplier;
			bJumpRising = false;
		}
	}

	// Jump / flap, with buffering
	if (bJumpPressedPending)
	{
		if (TryJump())
		{
			bJumpPressedPending = false;
		}
		else if (CharacterOwner && CharacterOwner->IsLocallyControlled() && !CharacterOwner->bClientUpdating)
		{
			// only the owning client ages the buffer; the server just follows the flag it receives
			JumpBufferRemaining -= DeltaSeconds;
			if (JumpBufferRemaining <= 0.f)
			{
				bJumpPressedPending = false;
			}
		}
	}

	// Glide: falling + jump held
	SetGliding(bCanGlide && IsFalling() && bJumpHeld && !bJumpRising && Velocity.Z <= 0.f);

	AirControl = bIsGliding ? GlideAirControl : NormalAirControl;
	BrakingDecelerationFalling = bIsGliding ? GlideBrakingDeceleration : NormalAirBrakingDeceleration;
}

bool UHikeMovementComponent::TryJump()
{
	if (!CharacterOwner || !CanAttemptJump())
	{
		return false;
	}

	const bool bGrounded = IsMovingOnGround();
	const bool bInCoyote = IsFalling() && CoyoteTimeRemaining > 0.f;

	if (bGrounded || bInCoyote)
	{
		PerformJump(false);
		return true;
	}

	if (IsFalling() && FlapsRemaining > 0)
	{
		--FlapsRemaining;
		PerformJump(true);
		return true;
	}

	// in the air with no feathers: keep the press buffered (lands -> jumps), and holding it glides
	return false;
}

void UHikeMovementComponent::PerformJump(bool bIsFlap)
{
	if (bIsFlap)
	{
		// a flap overrides any downward speed, like a fresh little jump
		Velocity.Z = FlapZVelocity;
	}
	else
	{
		Velocity.Z = FMath::Max(Velocity.Z, JumpZVelocity);
	}

	if (!IsFalling())
	{
		SetMovementMode(MOVE_Falling);
	}

	CoyoteTimeRemaining = 0.f;
	bJumpRising = true;
	JumpRiseTime = 0.f;
	SetGliding(false);

	if (!CharacterOwner->bClientUpdating)
	{
		if (AHikeCharacter* HikeCharacter = Cast<AHikeCharacter>(CharacterOwner))
		{
			HikeCharacter->OnHikeJump(bIsFlap);
		}
	}
}

void UHikeMovementComponent::SetGliding(bool bNewGliding)
{
	if (bIsGliding == bNewGliding)
	{
		return;
	}

	bIsGliding = bNewGliding;

	if (CharacterOwner && !CharacterOwner->bClientUpdating)
	{
		if (AHikeCharacter* HikeCharacter = Cast<AHikeCharacter>(CharacterOwner))
		{
			HikeCharacter->OnGlideChanged(bIsGliding);
		}
	}
}

void UHikeMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	if (MovementMode == MOVE_Falling && PreviousMovementMode == MOVE_Walking)
	{
		// walked off a ledge (not launched upwards): grant coyote time
		CoyoteTimeRemaining = (Velocity.Z <= 10.f) ? CoyoteTime : 0.f;
	}
	else if (IsMovingOnGround())
	{
		// landed: feathers come back
		FlapsRemaining = GoldenFeathers;
		CoyoteTimeRemaining = 0.f;
		bJumpRising = false;
		SetGliding(false);
	}
}

// ------------------------------------------------------------------------------------------- physics hooks

float UHikeMovementComponent::GetMaxSpeed() const
{
	const float RunOrWalk = (bWantsToRun && bCanRun) ? RunSpeed : MaxWalkSpeed;

	switch (MovementMode)
	{
	case MOVE_Walking:
	case MOVE_NavWalking:
		return IsCrouching() ? MaxWalkSpeedCrouched : RunOrWalk;
	case MOVE_Falling:
		return bIsGliding ? GlideSpeed : RunOrWalk;
	default:
		return Super::GetMaxSpeed();
	}
}

float UHikeMovementComponent::GetGravityZ() const
{
	float Gravity = Super::GetGravityZ();

	if (IsFalling())
	{
		if (bIsGliding)
		{
			Gravity *= GlideGravityScale;
		}
		else if (Velocity.Z < 0.f)
		{
			Gravity *= FallGravityMultiplier;
		}
	}

	return Gravity;
}

FVector UHikeMovementComponent::NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const
{
	FVector Result = Super::NewFallVelocity(InitialVelocity, Gravity, DeltaTime);

	if (bIsGliding && Result.Z < -GlideFallSpeed)
	{
		// ease into the glide instead of stopping dead
		Result.Z = FMath::FInterpTo(Result.Z, -GlideFallSpeed, DeltaTime, GlideCatchRate);
	}

	Result.Z = FMath::Max(Result.Z, -MaxFallSpeed);
	return Result;
}

// ------------------------------------------------------------------------------------------- networking

void UHikeMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	bJumpHeld           = (Flags & FSavedMove_Hike::FLAG_HikeJumpHeld) != 0;
	bJumpPressedPending = (Flags & FSavedMove_Hike::FLAG_HikeJumpPressed) != 0;
	bWantsToRun         = (Flags & FSavedMove_Hike::FLAG_HikeRun) != 0;
}

FNetworkPredictionData_Client* UHikeMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		UHikeMovementComponent* MutableThis = const_cast<UHikeMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Hike(*this);
	}
	return ClientPredictionData;
}

FSavedMovePtr FNetworkPredictionData_Client_Hike::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_Hike());
}

void FSavedMove_Hike::Clear()
{
	Super::Clear();

	bSavedJumpHeld = false;
	bSavedJumpPressed = false;
	bSavedWantsToRun = false;
	bSavedJumpRising = false;
	SavedFlapsRemaining = 0;
	SavedCoyoteTime = 0.f;
	SavedJumpRiseTime = 0.f;
}

uint8 FSavedMove_Hike::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();

	if (bSavedJumpHeld)    { Result |= FLAG_HikeJumpHeld; }
	if (bSavedJumpPressed) { Result |= FLAG_HikeJumpPressed; }
	if (bSavedWantsToRun)  { Result |= FLAG_HikeRun; }

	return Result;
}

bool FSavedMove_Hike::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	const FSavedMove_Hike* Other = static_cast<const FSavedMove_Hike*>(NewMove.Get());

	if (bSavedJumpPressed || Other->bSavedJumpPressed
		|| bSavedJumpHeld != Other->bSavedJumpHeld
		|| bSavedWantsToRun != Other->bSavedWantsToRun
		|| SavedFlapsRemaining != Other->SavedFlapsRemaining)
	{
		return false;
	}

	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FSavedMove_Hike::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

	if (const UHikeMovementComponent* Move = Cast<UHikeMovementComponent>(C->GetCharacterMovement()))
	{
		bSavedJumpHeld = Move->bJumpHeld;
		bSavedJumpPressed = Move->bJumpPressedPending;
		bSavedWantsToRun = Move->bWantsToRun;
		bSavedJumpRising = Move->bJumpRising;
		SavedFlapsRemaining = Move->FlapsRemaining;
		SavedCoyoteTime = Move->CoyoteTimeRemaining;
		SavedJumpRiseTime = Move->JumpRiseTime;
	}
}

void FSavedMove_Hike::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);

	if (UHikeMovementComponent* Move = Cast<UHikeMovementComponent>(C->GetCharacterMovement()))
	{
		Move->bJumpRising = bSavedJumpRising;
		Move->FlapsRemaining = SavedFlapsRemaining;
		Move->CoyoteTimeRemaining = SavedCoyoteTime;
		Move->JumpRiseTime = SavedJumpRiseTime;
	}
}
