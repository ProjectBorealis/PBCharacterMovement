// Copyright Project Borealis

#pragma once

#include "GameFramework/CharacterMovementComponent.h"

#include "PBPlayerCharacter.h"

#include "PBPlayerMovement.generated.h"

constexpr float LADDER_MOUNT_TIMEOUT = 0.2f;

// Crouch Timings (in seconds)
constexpr float MOVEMENT_DEFAULT_CROUCHTIME = 0.4f;
constexpr float MOVEMENT_DEFAULT_CROUCHJUMPTIME = 0.0f;
constexpr float MOVEMENT_DEFAULT_UNCROUCHTIME = 0.2f;
constexpr float MOVEMENT_DEFAULT_UNCROUCHJUMPTIME = 0.8f;

class USoundCue;

constexpr float DesiredGravity = -1143.0f;

UCLASS()
class PBCHARACTERMOVEMENT_API UPBPlayerMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

protected:
	/** If the player is using a ladder */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Gameplay)
	bool bOnLadder;

	/** Should crouch slide? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	bool bShouldCrouchSlide;

	/** If the player is currently crouch sliding */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Gameplay)
	bool bCrouchSliding;

	/** schedule a crouch slide to landing */
	bool bDeferCrouchSlideToLand;

	/** Time crouch sliding started */
	float CrouchSlideStartTime;

	/** How long a crouch slide boosts for */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float CrouchSlideBoostTime;

	/** The minimum starting boost */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float MinCrouchSlideBoost;

	/** The factor for determining the initial crouch slide boost up a slope */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float CrouchSlideBoostSlopeFactor;

	/** How much to multiply initial velocity by when starting a crouch slide */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float CrouchSlideBoostMultiplier;

	/** How much forward velocity player needs relative to sprint speed in order to start a crouch slide */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float CrouchSlideSpeedRequirementMultiplier;

	/** The max velocity multiplier for acceleration in crouch sliding */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float MaxCrouchSlideVelocityBoost;

	/** The min velocity multiplier for acceleration in crouch sliding */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float MinCrouchSlideVelocityBoost;

	/** Time before being able to crouch slide again */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float CrouchSlideCooldown;

	/** Enter crouch slide mode, giving the player a boost and adjusting camera effects */
	void StartCrouchSlide();
	/** If crouch sliding mode is turned on and valid in the current movement state and thus should occur */
	bool ShouldCrouchSlide() const;

	/** The time that the player can remount on the ladder */
	float OffLadderTicks = -1.0f;

	/** The multiplier for acceleration when on ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float GroundAccelerationMultiplier;

	/** The multiplier for acceleration when in air. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Jumping / Falling")
	float AirAccelerationMultiplier;

	/* The vector differential magnitude cap when in air. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Jumping / Falling")
	float AirSpeedCap;

	/* The vector differential magnitude cap when in air and sliding. This is here to give player less momentum control while sliding on a slope, but giving more control while jumping using AirSpeedCap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Jumping / Falling")
	float AirSlideSpeedCap;

	/* Proportion of player input acceleration (0 to disable, 0.5 for half, 2 for double, etc.) to use for the horizontal air dash. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Jumping / Falling")
	float AirJumpDashMagnitude = 0.0f;

	/* If an air jump resets all horizontal movement. Useful in tandom with air dash to reset all velocity to a new direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Jumping / Falling")
	bool bAirJumpResetsHorizontal = false;

	/** Time to crouch on ground in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float CrouchTime;

	/** Time to uncrouch on ground in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float UncrouchTime;

	/** Time to crouch in air in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float CrouchJumpTime;

	/** Time to uncrouch in air in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float UncrouchJumpTime;

	/** Speed on a ladder */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Ladder")
	float LadderSpeed;

	/** Ladder timeout */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Ladder")
	float LadderTimeout;

	/** the minimum step height from moving fast */
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite)
	float MinStepHeight;

	/** the fraction of MaxStepHeight to use for step down height, otherwise player enters air move state and lets gravity do the work. */
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite)
	float StepDownHeightFraction;

	/** Friction multiplier to use when on an edge */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float EdgeFrictionMultiplier;

	/** height away from a floor to apply edge friction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float EdgeFrictionHeight;

	/** distance in the direction of movement to look ahead for the edge */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float EdgeFrictionDist;

	/** only apply edge friction when braking (no acceleration) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	bool bEdgeFrictionOnlyWhenBraking;

	/** apply edge friction when crouching, even if not braking */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float bEdgeFrictionAlwaysWhenCrouching;

	/** Time the player has before applying friction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Jumping / Falling")
	float BrakingWindow;

	/* Progress checked against the Braking Window */
	float BrakingWindowTimeElapsed;

	/** If the player has already landed for a frame, and breaking may be applied. */
	bool bBrakingFrameTolerated;

	/** Wait a frame before crouch speed. */
	bool bCrouchFrameTolerated;

	/** If in the crouching transition */
	bool bIsInCrouchTransition;

	/** If the player is currently locked in crouch state */
	bool bLockInCrouch = false;

	APBPlayerCharacter* GetPBCharacter() const { return PBPlayerCharacter; }

	/** The PB player character */
	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<APBPlayerCharacter> PBPlayerCharacter;

	/** The target ground speed when running. */
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", UIMin = "0"))
	float RunSpeed;

	/** The target ground speed when sprinting.  */
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", UIMin = "0"))
	float SprintSpeed;

	/** The target ground speed when walking slowly. */
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", UIMin = "0"))
	float WalkSpeed;

	/** The minimum speed to scale up from for slope movement  */
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", UIMin = "0"))
	float SpeedMultMin;

	/** The maximum speed to scale up to for slope movement */
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", UIMin = "0"))
	float SpeedMultMax;

	/** The maximum angle we can roll for camera adjust */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement (General Settings)")
	float RollAngle;

	/** Speed of rolling the camera */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement (General Settings)")
	float RollSpeed;

	/** Speed of rolling the camera */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement (General Settings)")
	float BounceMultiplier = 0.0f;

	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", UIMin = "0"))
	float AxisSpeedLimit;

	/** Threshold relating to speed ratio and friction which causes us to catch air */
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", UIMin = "0"))
	float SlideLimit = 0.5f;

	/** Fraction of uncrouch half-height to check for before doing starting an uncrouch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement (General Settings)")
	float GroundUncrouchCheckFactor = 0.75f;

	bool bShouldPlayMoveSounds = true;

	/** Milliseconds between step sounds */
	float MoveSoundTime = 0.0f;
	/** If we are stepping left, else, right */
	bool StepSide = false;

	/** Plays sound effect according to movement and surface */
	virtual void PlayMoveSound(float DeltaTime);

	virtual void PlayJumpSound(const FHitResult& Hit, bool bJumped);

	UPBMoveStepSound* GetMoveStepSoundBySurface(EPhysicalSurface SurfaceType) const;

public:
	/** Print pos and vel (Source: cl_showpos) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement (General Settings)")
	uint32 bShowPos : 1;

	UPBPlayerMovement();

	virtual void InitializeComponent() override;
	virtual void OnRegister() override;

	// Overrides for Source-like movement
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void ApplyVelocityBraking(float DeltaTime, float Friction, float BrakingDeceleration) override;
	bool ShouldLimitAirControl(float DeltaTime, const FVector& FallAcceleration) const override;
	FVector NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const override;

	void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	void UpdateCharacterStateAfterMovement(float DeltaSeconds) override;

	void UpdateSurfaceFriction(bool bIsSliding = false);
	void UpdateCrouching(float DeltaTime, bool bOnlyUnCrouch = false);

	// Overrides for crouch transitions
	virtual void Crouch(bool bClientSimulation = false) override;
	virtual void UnCrouch(bool bClientSimulation = false) override;
	virtual void DoCrouchResize(float TargetTime, float DeltaTime, bool bClientSimulation = false);
	virtual void DoUnCrouchResize(float TargetTime, float DeltaTime, bool bClientSimulation = false);

	bool MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) override;

	// Jump overrides
	bool CanAttemptJump() const override;
	bool DoJump(bool bClientSimulation) override;

	float GetFallSpeed(bool bAfterLand = false);

	/** Exit crouch slide mode, and stop camera effects */
	void StopCrouchSliding();

	/** If the crouch lock should be toggled on or off, crouch lock locks the player in their crouch state */
	void ToggleCrouchLock(bool bLock);

	void TwoWallAdjust(FVector& OutDelta, const FHitResult& Hit, const FVector& OldHitNormal) const override;
	float SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, bool bHandleImpact = false) override;
	FVector ComputeSlideVector(const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const override;
	FVector HandleSlopeBoosting(const FVector& SlideResult, const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const override;
	bool ShouldCatchAir(const FFindFloorResult& OldFloor, const FFindFloorResult& NewFloor) override;
	bool IsWithinEdgeTolerance(const FVector& CapsuleLocation, const FVector& TestImpactPoint, const float CapsuleRadius) const override;
	bool ShouldCheckForValidLandingSpot(float DeltaTime, const FVector& Delta, const FHitResult& Hit) const override;
	void HandleImpact(const FHitResult& Hit, float TimeSlice = 0.0f, const FVector& MoveDelta = FVector::ZeroVector) override;
	bool IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const override;

	/** called when player does an air jump. can be used for SFX/VFX. */
	UFUNCTION(BlueprintImplementableEvent)
	void OnAirJump(int32 JumpTimes);

	float GetFrictionFromHit(const FHitResult& Hit) const;
	void TraceCharacterFloor(FHitResult& OutHit) const;
	void TraceLineToFloor(FHitResult& OutHit) const;

	// Acceleration
	FORCEINLINE FVector GetAcceleration() const { return Acceleration; }

	// Crouch locked
	FORCEINLINE bool GetCrouchLocked() const { return bLockInCrouch; }

	float GetSprintSpeed() const { return SprintSpeed; }

	void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	/** Do camera roll effect based on velocity */
	float GetCameraRoll();

	/** Is this player on a ladder? */
	UFUNCTION(BlueprintCallable)
	bool IsOnLadder() const;

	/** Return the speed used to climb ladders */
	float GetLadderClimbSpeed() const;

	void SetNoClip(bool bNoClip);

	/** Toggle no clip */
	void ToggleNoClip();

	bool IsBrakingFrameTolerated() const { return bBrakingFrameTolerated; }

	bool IsInCrouchTransition() const { return bIsInCrouchTransition; }

	bool IsCrouchSliding() const { return bCrouchSliding; }

	void SetShouldPlayMoveSounds(bool bShouldPlay) { bShouldPlayMoveSounds = bShouldPlay; }

	virtual float GetMaxSpeed() const override;

	virtual void ApplyDownwardForce(float DeltaSeconds) override;

private:
	float DefaultStepHeight;
	float DefaultSpeedMultMin;
	float DefaultSpeedMultMax;
	float DefaultWalkableFloorZ;
	float SurfaceFriction;
	TWeakObjectPtr<UPrimitiveComponent> OldBase;

	/** If we have done an initial landing */
	bool bHasEverLanded = false;

	/** if we're currently sliding in air */
	bool bSlidingInAir = false;

	/** if we were sliding in air in the prior frame */
	bool bWasSlidingInAir = false;

	bool bHasDeferredMovementMode;
	EMovementMode DeferredMovementMode;
};
