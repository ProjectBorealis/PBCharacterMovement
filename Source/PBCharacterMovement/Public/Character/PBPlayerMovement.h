// Copyright 2017-2019 Project Borealis

#pragma once

#include "CoreMinimal.h"

#include "GameFramework/CharacterMovementComponent.h"

#include "PBPlayerMovement.generated.h"

#define LADDER_MOUNT_TIMEOUT 0.2f

// Crouch Timings (in seconds)
#define MOVEMENT_DEFAULT_CROUCHTIME 0.4f
#define MOVEMENT_DEFAULT_CROUCHJUMPTIME 0.0f
#define MOVEMENT_DEFAULT_UNCROUCHTIME 0.2f
#define MOVEMENT_DEFAULT_UNCROUCHJUMPTIME 0.8f

// Testing mid-air stepping code
#ifndef MID_AIR_STEP
#define MID_AIR_STEP 0
#endif

// Testing surfing code
#ifndef WIP_SURFING
#define WIP_SURFING 0
#endif

class USoundCue;

UCLASS()
class PBCHARACTERMOVEMENT_API UPBPlayerMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

protected:
	/** If the player is using a ladder */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Gameplay)
	bool bOnLadder;

	/** Milliseconds between step sounds */
	float MoveSoundTime;

	/** If we are stepping left, else, right */
	bool StepSide;

	/** The multiplier for acceleration when on ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float GroundAccelerationMultiplier;

	/** The multiplier for acceleration when in air. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Walking")
	float AirAccelerationMultiplier;
	
	/* The vector differential magnitude cap when in air. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Jumping / Falling")
	float AirSpeedCap;

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

	/** the minimum step height from moving fast */
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite)
	float MinStepHeight;

	/** If the player has already landed for a frame, and breaking may be applied. */
	bool bBrakingFrameTolerated;

	/** If in the crouching transition */
	bool bInCrouch;

	/** The PB player character */
	class APBPlayerCharacter* PBCharacter;

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

	bool bAppliedFriction;

public:
	/** Print pos and vel (Source: cl_showpos) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement (General Settings)")
	uint32 bShowPos : 1;

	/** Vertical move mode for no clip: 0 - add down move; 1 - nothing; 2 - add up move */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Character Movement (General Settings)")
	int32 NoClipVerticalMoveMode;

	UPBPlayerMovement();

	// Overrides for Source-like movement
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void ApplyVelocityBraking(float DeltaTime, float Friction, float BrakingDeceleration) override;

	// Overrides for crouch transitions
	virtual void Crouch(bool bClientSimulation = false) override;
	virtual void UnCrouch(bool bClientSimulation = false) override;
	virtual void DoCrouchResize(float TargetTime, float DeltaTime, bool bClientSimulation = false);
	virtual void DoUnCrouchResize(float TargetTime, float DeltaTime, bool bClientSimulation = false);

	// Noclip overrides
	virtual bool DoJump(bool bClientSimulation) override;

#if MID_AIR_STEP
	// Step up
	virtual void PhysFalling(float deltaTime, int32 Iterations) override;
	virtual bool CanStepUp(const FHitResult& Hit) const override;
	virtual bool StepUp(const FVector& GravDir, const FVector& Delta, const FHitResult& Hit,
						struct UCharacterMovementComponent::FStepDownResult* OutStepDownResult = NULL) override;
#endif

	// Remove slope boost constaints
	virtual void TwoWallAdjust(FVector& Delta, const FHitResult& Hit, const FVector& OldHitNormal) const override;
	virtual float SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, bool bHandleImpact = false);
	virtual FVector HandleSlopeBoosting(const FVector& SlideResult, const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const override;
	virtual bool ShouldCatchAir(const FFindFloorResult& OldFloor, const FFindFloorResult& NewFloor) override;

	// Acceleration
	FORCEINLINE FVector GetAcceleration() const
	{
		return Acceleration;
	}

	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode);

	/** Toggle no clip */
	void ToggleNoClip();

	bool IsBrakingFrameTolerated() const
	{
		return bBrakingFrameTolerated;
	}

	bool IsInCrouch() const
	{
		return bInCrouch;
	}

	virtual float GetMaxSpeed() const override;

private:
	/** Plays sound effect according to movement and surface */
	void PlayMoveSound(float DeltaTime);

#if WIP_SURFING
	void PreemptCollision(float DeltaTime, float SurfaceFriction);
#endif
};
