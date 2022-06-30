// Copyright 2017-2019 Project Borealis

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PBPlayerMovement.generated.h"

#define LADDER_MOUNT_TIMEOUT 0.2f

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
public:
	/**
	 * UObject constructor.
	 */
	UPBPlayerMovement(const FObjectInitializer& ObjectInitializer);

	/* return the Max Speed for the current state. */
	virtual float GetMaxSpeed() const override;

	/** @return Maximum acceleration for the current state. */
	virtual float GetMaxAcceleration() const override;

	/** Update the character state in PerformMovement right before doing the actual position change
	 * Using OnMovementUpdated to do the stuff in here for the simulated proxy since this is not called on the simulated, need a tick.
	 */
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;

	/** Get prediction data for a client game. Should not be used if not running as a client. Allocates the data on demand and can be overridden to allocate a custom override if desired. Result must be a FNetworkPredictionData_Client_Character. */
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	/** Unpack compressed flags from a saved move and set state accordingly. See FSavedMove_Character. */
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;

	/*returns true if the capsule was shrunk successfully*/
	virtual bool ShrinkCapsule(float NewUnscaledHalfHeight, bool bClientSimulation);
	/*returns true if the capsule was expanded successfully or false if it hits something*/
	virtual bool ExpandCapsule(float NewUnscaledHalfHeight, bool bClientSimulation);

	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;

	/* does the character want to sprint, set to true from StartSpriting.
	 * set to true in StartSprint and false in StopSprint.
	 * if held down, it will start automatically sprinting the next time its possible to sprint, check in CanSprint
	 */
	uint8 bWantsToSprint : 1;

	//#TODO add a cool down timer
	/*Max sprint time before cool down sets in, -1 for unlimited NOT USED FOR NOW*/
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = Sprint)
		float MaxSprintTime;

	/*set the max speed to the normal Walking walking speed multiplied by this amount when sprinting*/
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", UIMin = "0"))
		float MaxSprintSpeed;

	/** The maximum ground speed when walking and crouched. */
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", UIMin = "0"))
		float MaxWalkSpeedProne;

	/*the amount you can move to the side, 1 will allow the player to sprint sideways*/
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0", UIMin = "1.0"))
		float SprintSideMultiplier;

	/*The maximum Accleration calculated using the currentSpeed/maximum speed,
	 *this will give a 2x boost multiplier if the player is moving too slow at the start
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Character Movement: Sprinting")
		UCurveFloat* SprintAccelerationCurve;

	/** If true, this Pawn is capable of sprinting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MovementProperties)
		uint8 bCanSprint : 1;

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

private:
	/** Plays sound effect according to movement and surface */
	void PlayMoveSound(float DeltaTime);

#if WIP_SURFING
	void PreemptCollision(float DeltaTime, float SurfaceFriction);
#endif

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
	/** the minimum step height from moving fast */
	UPROPERTY(Category = "Character Movement: Walking", EditAnywhere, BlueprintReadWrite)
		float MinStepHeight;
	/** If the player has already landed for a frame, and breaking may be applied. */
	bool bBrakingFrameTolerated;
	/** If in Sprinting transition */
	bool bIsSprinting;
	/** If in Crouching transition */
	bool bIsCrouching;
	/** If in Crouching transition */
	bool WantsToCrouch;
	/** floor check removed */
	bool UseFlatBaseForFloorChecks = false;
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
	/*The Time taken to crouch, the change in height doesn't matter since its calculated*/
	UPROPERTY(Category = "Character Movement: Crouching", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.1", UIMin = "0"))
		float CrouchTime;
	bool bAppliedFriction;
};