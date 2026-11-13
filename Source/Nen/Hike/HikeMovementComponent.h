// Nen - A Short Hike style movement ("Controls" in 3C)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HikeMovementComponent.generated.h"

/**
 *  Movement feel modelled on A Short Hike:
 *   - snappy, camera-relative ground movement with an optional run
 *   - variable-height jump (release early = short hop), coyote time, jump buffering
 *   - golden feathers: each one is a mid-air flap, all refilled on landing
 *   - hold jump while falling to glide
 *
 *  All input intent travels to the server through saved-move compressed flags,
 *  so it is client-predicted and safe for online co-op as well as local play.
 *  Every number here is meant to be tuned in the Character Blueprint's movement component.
 */
UCLASS(ClassGroup=(Nen), meta=(BlueprintSpawnableComponent))
class NEN_API UHikeMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend class FSavedMove_Hike;

public:

	UHikeMovementComponent();

	// ---------------------------------------------------------------- Ground

	/** Top speed while the run button is held (MaxWalkSpeed is the normal speed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Ground", meta=(ClampMin=0, Units="cm/s"))
	float RunSpeed = 800.f;

	/** If false, the run input is ignored (e.g. lock it behind the running-shoes pickup) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Ground")
	bool bCanRun = true;

	// ---------------------------------------------------------------- Jump

	/** Upward speed of a mid-air flap (ground jump uses JumpZVelocity) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Jump", meta=(ClampMin=0, Units="cm/s"))
	float FlapZVelocity = 700.f;

	/** Releasing jump while rising multiplies upward speed by this (variable jump height) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Jump", meta=(ClampMin=0, ClampMax=1))
	float JumpCutMultiplier = 0.45f;

	/** A jump is never cut before this much rise time, so a tap still gives a readable hop */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Jump", meta=(ClampMin=0, Units="s"))
	float MinJumpRiseTime = 0.08f;

	/** Gravity multiplier while falling (not gliding). >1 gives a snappier, less floaty arc */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Jump", meta=(ClampMin=0))
	float FallGravityMultiplier = 1.35f;

	/** Terminal fall speed when not gliding */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Jump", meta=(ClampMin=0, Units="cm/s"))
	float MaxFallSpeed = 2200.f;

	/** Grace period after walking off a ledge in which a ground jump still works */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Jump", meta=(ClampMin=0, Units="s"))
	float CoyoteTime = 0.12f;

	/** A jump pressed this long before it becomes possible (e.g. just before landing) still fires */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Jump", meta=(ClampMin=0, Units="s"))
	float JumpBufferTime = 0.12f;

	/** Air control when jumping / falling normally */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Jump", meta=(ClampMin=0, ClampMax=1))
	float NormalAirControl = 0.6f;

	/** How fast horizontal speed bleeds off in the air when the stick is released */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Jump", meta=(ClampMin=0, Units="cm/s"))
	float NormalAirBrakingDeceleration = 600.f;

	// ---------------------------------------------------------------- Feathers

	/** Golden feathers owned = number of flaps per airtime */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hike|Feathers", meta=(ClampMin=0))
	int32 GoldenFeathers = 2;

	// ---------------------------------------------------------------- Glide

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Glide")
	bool bCanGlide = true;

	/** Sink speed the glide settles at */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Glide", meta=(ClampMin=0, Units="cm/s"))
	float GlideFallSpeed = 180.f;

	/** How quickly a fast fall is braked down to GlideFallSpeed (higher = more abrupt) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Glide", meta=(ClampMin=0))
	float GlideCatchRate = 6.f;

	/** Gravity multiplier while gliding */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Glide", meta=(ClampMin=0))
	float GlideGravityScale = 0.3f;

	/** Horizontal top speed while gliding */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Glide", meta=(ClampMin=0, Units="cm/s"))
	float GlideSpeed = 850.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Glide", meta=(ClampMin=0, ClampMax=1))
	float GlideAirControl = 0.9f;

	/** Low braking keeps glide momentum when the stick is released */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hike|Glide", meta=(ClampMin=0, Units="cm/s"))
	float GlideBrakingDeceleration = 150.f;

	// ---------------------------------------------------------------- Input intent (set by the player controller via the character)

	void SetJumpInput(bool bPressed);
	void SetRunInput(bool bPressed);

	// ---------------------------------------------------------------- Queries

	UFUNCTION(BlueprintPure, Category="Hike")
	bool IsGliding() const { return bIsGliding; }

	UFUNCTION(BlueprintPure, Category="Hike")
	bool IsRunning() const { return bWantsToRun && bCanRun && IsMovingOnGround() && Velocity.SizeSquared2D() > 1.f; }

	UFUNCTION(BlueprintPure, Category="Hike")
	int32 GetFlapsRemaining() const { return FlapsRemaining; }

	UFUNCTION(BlueprintPure, Category="Hike")
	float GetCoyoteTimeRemaining() const { return CoyoteTimeRemaining; }

	/** Change how many golden feathers the player owns (pickups, debug) */
	UFUNCTION(BlueprintCallable, Category="Hike")
	void SetGoldenFeathers(int32 NewCount);

	// ---------------------------------------------------------------- UCharacterMovementComponent

	virtual void BeginPlay() override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual float GetMaxSpeed() const override;
	virtual float GetGravityZ() const override;
	virtual FVector NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

protected:

	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;

private:

	bool TryJump();
	void PerformJump(bool bIsFlap);
	void SetGliding(bool bNewGliding);

	// input intent (replicated as compressed flags)
	bool bJumpHeld = false;
	bool bJumpPressedPending = false;
	bool bWantsToRun = false;

	// simulation state (saved for replay)
	int32 FlapsRemaining = 0;
	float CoyoteTimeRemaining = 0.f;
	bool bJumpRising = false;
	float JumpRiseTime = 0.f;
	bool bIsGliding = false;

	// local-only
	float JumpBufferRemaining = 0.f;
};

/** Saved move carrying the Hike input flags and the state needed to replay moves after a server correction */
class FSavedMove_Hike : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	FSavedMove_Hike()
		: bSavedJumpHeld(0), bSavedJumpPressed(0), bSavedWantsToRun(0), bSavedJumpRising(0)
	{
	}

	enum EHikeFlags : uint8
	{
		FLAG_HikeJumpHeld    = FLAG_Custom_0,
		FLAG_HikeJumpPressed = FLAG_Custom_1,
		FLAG_HikeRun         = FLAG_Custom_2,
	};

	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* C) override;

	uint8 bSavedJumpHeld : 1;
	uint8 bSavedJumpPressed : 1;
	uint8 bSavedWantsToRun : 1;
	uint8 bSavedJumpRising : 1;

	int32 SavedFlapsRemaining = 0;
	float SavedCoyoteTime = 0.f;
	float SavedJumpRiseTime = 0.f;
};

class FNetworkPredictionData_Client_Hike : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;

	FNetworkPredictionData_Client_Hike(const UCharacterMovementComponent& ClientMovement) : Super(ClientMovement) {}

	virtual FSavedMovePtr AllocateNewMove() override;
};
