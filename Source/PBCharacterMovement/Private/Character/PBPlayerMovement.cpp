// Copyright 2017-2019 Project Borealis

#include "Character/PBPlayerMovement.h"

#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Sound/SoundCue.h"

#include "Sound/PBMoveStepSound.h"
#include "Character/PBPlayerCharacter.h"

static TAutoConsoleVariable<int32> CVarShowPos(TEXT("cl.ShowPos"), 0, TEXT("Show position and movement information.\n"), ECVF_Default);

DECLARE_CYCLE_STAT(TEXT("Char StepUp"), STAT_CharStepUp, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char PhysFalling"), STAT_CharPhysFalling, STATGROUP_Character);

// MAGIC NUMBERS
const float MAX_STEP_SIDE_Z = 0.08f; // maximum z value for the normal on the vertical side of steps
const float VERTICAL_SLOPE_NORMAL_Z = 0.001f; // Slope is vertical if Abs(Normal.Z) <= this threshold. Accounts for precision problems that sometimes angle
											  // normals slightly off horizontal for vertical surface.

constexpr float DesiredGravity = -1143.0f;

// Purpose: override default player movement
UPBPlayerMovement::UPBPlayerMovement()
{
	// We have our own air movement handling, so we can allow for full air
	// control through UE's logic
	AirControl = 1.0f;
	// Disable air control boost
	AirControlBoostMultiplier = 1.0f;
	AirControlBoostVelocityThreshold = 0.0f;
	// HL2 cl_(forward & side)speed = 450Hu
	MaxAcceleration = 857.25f;
	// Set the default walk speed
	MaxWalkSpeed = 361.9f;
	WalkSpeed = 285.75f;
	RunSpeed = 361.9f;
	SprintSpeed = 609.6f;
	// Acceleration multipliers (HL2's sv_accelerate and sv_airaccelerate)
	GroundAccelerationMultiplier = 10.0f;
	AirAccelerationMultiplier = 10.0f;
	// 30 air speed cap from HL2
	AirSpeedCap = 57.15f;
	// HL2 like friction
	// sv_friction
	GroundFriction = 4.0f;
	BrakingFriction = 4.0f;
	bUseSeparateBrakingFriction = false;
	// No multiplier
	BrakingFrictionFactor = 1.0f;
	// Historical value for Source
	BrakingSubStepTime = 0.015f;
	// Avoid breaking up time step
	MaxSimulationTimeStep = 0.5f;
	MaxSimulationIterations = 1;
	// Braking deceleration (sv_stopspeed)
	FallingLateralFriction = 0.0f;
	BrakingDecelerationFalling = 0.0f;
	BrakingDecelerationFlying = 190.5f;
	BrakingDecelerationSwimming = 190.5f;
	BrakingDecelerationWalking = 190.5f;
	// HL2 step height
	MaxStepHeight = 34.29f;
	DefaultStepHeight = MaxStepHeight;
	// Step height scaling due to speed
	MinStepHeight = 7.5f;
	// Jump z from HL2's 160Hu
	// 21Hu jump height
	// 510ms jump time
	JumpZVelocity = FMath::Sqrt(2.0f * FMath::Abs(UPhysicsSettings::Get()->DefaultGravityZ) * 72.113775f);
	// Don't bounce off characters
	JumpOffJumpZFactor = 0.0f;
	// Default show pos to false
	bShowPos = false;
	// Speed multiplier bounds
	SpeedMultMin = SprintSpeed * 1.7f;
	SpeedMultMax = SprintSpeed * 2.5f;
	// Start out braking
	bBrakingFrameTolerated = true;
	// Crouching
	SetCrouchedHalfHeight(34.29f);
	MaxWalkSpeedCrouched = RunSpeed * 0.33333333f;
	bCanWalkOffLedgesWhenCrouching = true;
	CrouchTime = MOVEMENT_DEFAULT_CROUCHTIME;
	UncrouchTime = MOVEMENT_DEFAULT_UNCROUCHTIME;
	CrouchJumpTime = MOVEMENT_DEFAULT_CROUCHJUMPTIME;
	UncrouchJumpTime = MOVEMENT_DEFAULT_UNCROUCHJUMPTIME;
	// Noclip
	NoClipVerticalMoveMode = 0;
	// Slope angle is 45.57 degrees
	SetWalkableFloorZ(0.7f);
	DefaultWalkableFloorZ = GetWalkableFloorZ();
	// Tune physics interactions
	StandingDownwardForceScale = 1.0f;
	// Reasonable values polled from NASA (https://msis.jsc.nasa.gov/sections/section04.htm#Figure%204.9.3-6)
	// and Standard Handbook of Machine Design
	InitialPushForceFactor = 100.0f;
	PushForceFactor = 500.0f;
	// Let's not do any weird stuff...Gordon isn't a trampoline
	RepulsionForce = 0.0f;
	MaxTouchForce = 0.0f;
	TouchForceFactor = 0.0f;
	// Just push all objects based on their impact point
	// it might be weird with a lot of dev objects due to scale, but
	// it's much more realistic.
	bPushForceUsingZOffset = false;
	PushForcePointZOffsetFactor = -0.66f;
	// Scale push force down if we are slow
	bScalePushForceToVelocity = true;
	// Don't push more if there's more mass
	bPushForceScaledToMass = false;
	bTouchForceScaledToMass = false;
	Mass = 85.0f;	 // player.mdl is 85kg
	// Don't smooth rotation at all
	bUseControllerDesiredRotation = false;
	// Flat base
	bUseFlatBaseForFloorChecks = true;
	// Agent props
	NavAgentProps.bCanCrouch = true;
	NavAgentProps.bCanFly = true;
	PBCharacter = Cast<APBPlayerCharacter>(GetOwner());
	// Make sure gravity is correct for player movement
	GravityScale = DesiredGravity / UPhysicsSettings::Get()->DefaultGravityZ;
	// Make sure ramp movement in correct
	bMaintainHorizontalGroundVelocity = true;
}

void UPBPlayerMovement::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{	
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	PlayMoveSound(DeltaTime);

	if (bHasDeferredMovementMode)
	{
		bHasDeferredMovementMode = false;
		SetMovementMode(DeferredMovementMode);
	}

	// Skip player movement when we're simulating physics (ie ragdoll)
	if (UpdatedComponent->IsSimulatingPhysics())
	{
		return;
	}

	if ((bShowPos || CVarShowPos->GetInt() != 0) && CharacterOwner)
	{
		GEngine->AddOnScreenDebugMessage(1, 1.0f, FColor::Green,
										 FString::Printf(TEXT("pos: %f %f %f"), CharacterOwner->GetActorLocation().X, CharacterOwner->GetActorLocation().Y,
														 CharacterOwner->GetActorLocation().Z));
		GEngine->AddOnScreenDebugMessage(2, 1.0f, FColor::Green,
										 FString::Printf(TEXT("ang: %f %f %f"), CharacterOwner->GetControlRotation().Yaw,
														 CharacterOwner->GetControlRotation().Pitch, CharacterOwner->GetControlRotation().Roll));
		GEngine->AddOnScreenDebugMessage(3, 1.0f, FColor::Green, FString::Printf(TEXT("vel: %f"), FMath::Sqrt(Velocity.X * Velocity.X + Velocity.Y * Velocity.Y)));
	}

	// Crouch transition but not in noclip or on a ladder
	if (bIsInCrouchTransition && !bCheatFlying)
	{
		// If the player wants to uncrouch
		if (!bWantsToCrouch)
		{
			{
				if (IsWalking())
				{
					// Normal uncrouch
					DoUnCrouchResize(UncrouchTime, DeltaTime);
				}
				else
				{
					// Uncrouch jump
					DoUnCrouchResize(UncrouchJumpTime, DeltaTime);
				}
			}
		}
		else
		{
			if (bOnLadder)	  // if on a ladder, cancel this because bWantsToCrouch should be false
			{
				bIsInCrouchTransition = false;
			}
			else
			{
				if (IsWalking())
				{
					DoCrouchResize(CrouchTime, DeltaTime);
				}
				else
				{
					DoCrouchResize(CrouchJumpTime, DeltaTime);
				}
			}
		}
	}

	bBrakingFrameTolerated = IsMovingOnGround();
	bCrouchFrameTolerated = IsCrouching();
}

bool UPBPlayerMovement::DoJump(bool bClientSimulation)
{
	return !bCheatFlying && Super::DoJump(bClientSimulation);
}

float GetFrictionFromHit(const FHitResult& Hit)
{
	float SurfaceFriction = 1.0f;
	if (Hit.PhysMaterial.IsValid())
	{
		SurfaceFriction = FMath::Min(1.0f, Hit.PhysMaterial->Friction * 1.25f);
	}
	return SurfaceFriction;
}

void UPBPlayerMovement::TwoWallAdjust(FVector& Delta, const FHitResult& Hit, const FVector& OldHitNormal) const
{
	Super::TwoWallAdjust(Delta, Hit, OldHitNormal);
}

float UPBPlayerMovement::SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, bool bHandleImpact)
{
	return Super::SlideAlongSurface(Delta, Time, Normal, Hit, bHandleImpact);
}

FVector UPBPlayerMovement::ComputeSlideVector(const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const
{
	return Super::ComputeSlideVector(Delta, Time, Normal, Hit);
}

FVector UPBPlayerMovement::HandleSlopeBoosting(const FVector& SlideResult, const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const
{
	return Super::HandleSlopeBoosting(SlideResult, Delta, Time, Normal, Hit);
}

bool UPBPlayerMovement::ShouldCatchAir(const FFindFloorResult& OldFloor, const FFindFloorResult& NewFloor)
{
	// Get surface friction
	const float OldSurfaceFriction = GetFrictionFromHit(OldFloor.HitResult);

	// As we get faster, make our speed multiplier smaller (so it scales with smaller friction)
	const float SpeedMult = SpeedMultMax / Velocity.Size2D();
	const bool bSliding = OldSurfaceFriction * SpeedMult < 0.5f;

	// See if we got less steep or are continuing at the same slope
	const float ZDiff = NewFloor.HitResult.ImpactNormal.Z - OldFloor.HitResult.ImpactNormal.Z;
	const bool bGainingRamp = ZDiff >= 0.0f;

	// Velocity is always horizontal. Therefore, if we are moving up a ramp, we get >90 deg angle with the normal
	// This results in a negative cos. This also checks if our old floor was ramped at all, because a flat floor wouldn't pass this check.
	const float Slope = Velocity | OldFloor.HitResult.ImpactNormal;
	const bool bWasGoingUpRamp = Slope < 0.0f;

	// Finally, we want to also handle the case of strafing off of a ramp, so check if they're strafing.
	const float StrafeMovement = FMath::Abs(GetLastInputVector() | GetOwner()->GetActorRightVector());
	const bool bStrafingOffRamp = StrafeMovement > 0.0f;

	// So, our only relevant conditions are when we are going up a ramp or strafing off of it.
	const bool bMovingForCatchAir = bWasGoingUpRamp || bStrafingOffRamp;

	if (bSliding && bGainingRamp && bMovingForCatchAir)
	{
		return true;
	}

	return Super::ShouldCatchAir(OldFloor, NewFloor);
}

bool UPBPlayerMovement::IsWithinEdgeTolerance(const FVector& CapsuleLocation, const FVector& TestImpactPoint, const float CapsuleRadius) const
{
	return Super::IsWithinEdgeTolerance(CapsuleLocation, TestImpactPoint, CapsuleRadius);
}

bool UPBPlayerMovement::ShouldCheckForValidLandingSpot(float DeltaTime, const FVector& Delta, const FHitResult& Hit) const
{
	// TODO: check for flat base valid landing spots? at the moment this check is too generous for the capsule hemisphere
	return !bUseFlatBaseForFloorChecks && Super::ShouldCheckForValidLandingSpot(DeltaTime, Delta, Hit);
}

bool UPBPlayerMovement::IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const
{
	if (!Hit.bBlockingHit)
	{
		return false;
	}
	// Skip some checks if penetrating. Penetration will be handled by the FindFloor call (using a smaller capsule)
	if (!Hit.bStartPenetrating)
	{
		// Reject unwalkable floor normals.
		if (!IsWalkable(Hit))
		{
			return false;
		}

		float PawnRadius, PawnHalfHeight;
		CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);

		// Reject hits that are above our lower hemisphere (can happen when sliding down a vertical surface).
		if (bUseFlatBaseForFloorChecks)
		{
			// Reject hits that are above our box
			const float LowerHemisphereZ = Hit.Location.Z - PawnHalfHeight + SWEEP_EDGE_REJECT_DISTANCE + KINDA_SMALL_NUMBER;
			if ((Hit.ImpactNormal.Z < GetWalkableFloorZ() || Hit.ImpactNormal.Z == 1.0f) && Hit.ImpactPoint.Z > LowerHemisphereZ)
			{
				return false;
			}
		}
		else
		{
			// Reject hits that are above our lower hemisphere (can happen when sliding down a vertical surface).
			const float LowerHemisphereZ = Hit.Location.Z - PawnHalfHeight + PawnRadius;
			if (Hit.ImpactPoint.Z >= LowerHemisphereZ)
			{
				return false;
			}
		}

		// Reject hits that are barely on the cusp of the radius of the capsule
		if (!IsWithinEdgeTolerance(Hit.Location, Hit.ImpactPoint, PawnRadius))
		{
			return false;
		}
	}
	else
	{
		// Penetrating
		if (Hit.Normal.Z < KINDA_SMALL_NUMBER)
		{
			// Normal is nearly horizontal or downward, that's a penetration adjustment next to a vertical or overhanging wall. Don't pop to the floor.
			return false;
		}
	}
	FFindFloorResult FloorResult;
	FindFloor(CapsuleLocation, FloorResult, false, &Hit);
	if (!FloorResult.IsWalkableFloor())
	{
		return false;
	}
	return true;
}

FHitResult TraceLineFullCharacter(UCapsuleComponent* CharacterToTraceBy, UWorld* World, AActor* CallingActor, bool bForceTraceComplex, bool bDebug)
{
	auto RV_TraceParams = FCollisionQueryParams(SCENE_QUERY_STAT(CharacterTrace), true, CallingActor);
	RV_TraceParams.bTraceComplex = CharacterToTraceBy->bTraceComplexOnMove || bForceTraceComplex;
	RV_TraceParams.bReturnPhysicalMaterial = true;

	// Re-initialize hit info
	FHitResult RV_Hit(ForceInit);

	World->SweepSingleByChannel(RV_Hit, CharacterToTraceBy->GetComponentLocation(), CharacterToTraceBy->GetComponentLocation() - FVector(0, 0, CharacterToTraceBy->GetScaledCapsuleHalfHeight() * 2.f), FQuat::Identity, ECC_Visibility, FCollisionShape::MakeBox(FVector(CharacterToTraceBy->GetScaledCapsuleRadius(), CharacterToTraceBy->GetScaledCapsuleRadius(), CharacterToTraceBy->GetScaledCapsuleHalfHeight() * 1.5f)), RV_TraceParams);
	if (bDebug)
	{
		DrawDebugBox(World, CharacterToTraceBy->GetComponentLocation() - FVector(0, 0, CharacterToTraceBy->GetScaledCapsuleHalfHeight()), FVector(CharacterToTraceBy->GetScaledCapsuleRadius(), CharacterToTraceBy->GetScaledCapsuleRadius(), CharacterToTraceBy->GetScaledCapsuleHalfHeight()), FColor(255, 0, 0), false, -1, 0, 12.333);
	}

	return RV_Hit;
}

void UPBPlayerMovement::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	// Reset step side if we are changing modes
	StepSide = false;

	FHitResult Hit;
	// did we jump or land
	bool bJumped = false;

	if (PreviousMovementMode == MOVE_Walking && MovementMode == MOVE_Falling)
	{
		Hit = TraceLineFullCharacter(CharacterOwner->GetCapsuleComponent(), GetWorld(), CharacterOwner, false, false);
		bJumped = true;
	}
	else if (PreviousMovementMode == MOVE_Falling && MovementMode == MOVE_Walking)
	{
		Hit = CurrentFloor.HitResult;
	}

	PlayJumpSound(Hit, bJumped);
}

float UPBPlayerMovement::GetCameraRoll()
{
	if (RollSpeed == 0.0f || RollAngle == 0.0f)
	{
		return 0.0f;
	}
	float Side = Velocity | FRotationMatrix(GetCharacterOwner()->GetControlRotation()).GetScaledAxis(EAxis::Y);
	const float Sign = FMath::Sign(Side);
	Side = FMath::Abs(Side);
	if (Side < RollSpeed)
	{
		Side = Side * RollAngle / RollSpeed;
	}
	else
	{
		Side = RollAngle;
	}
	return Side * Sign;
}

void UPBPlayerMovement::SetNoClip(bool bNoClip)
{
	// We need to defer movement in case we set this outside of main game thread loop, since character movement resets movement back in tick.
	if (bNoClip)
	{
		SetMovementMode(MOVE_Flying);
		DeferredMovementMode = MOVE_Flying;
		bCheatFlying = true;
		GetCharacterOwner()->SetActorEnableCollision(false);
	}
	else
	{
		SetMovementMode(MOVE_Walking);
		DeferredMovementMode = MOVE_Walking;
		bCheatFlying = false;
		GetCharacterOwner()->SetActorEnableCollision(true);
	}
	bHasDeferredMovementMode = true;
}

void UPBPlayerMovement::ToggleNoClip()
{
	SetNoClip(!bCheatFlying);
}

void UPBPlayerMovement::ApplyVelocityBraking(float DeltaTime, float Friction, float BrakingDeceleration)
{
	// UE4-COPY: void UCharacterMovementComponent::ApplyVelocityBraking(float DeltaTime, float Friction, float BrakingDeceleration)
	if (Velocity.IsNearlyZero(0.1f) || !HasValidData() || HasAnimRootMotion() || DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	const float Speed = Velocity.Size2D();

	const float FrictionFactor = FMath::Max(0.0f, BrakingFrictionFactor);
	Friction = FMath::Max(0.0f, Friction * FrictionFactor);
	{
		BrakingDeceleration = FMath::Max(BrakingDeceleration, Speed);
	}
	BrakingDeceleration = FMath::Max(0.0f, BrakingDeceleration);
	const bool bZeroFriction = FMath::IsNearlyZero(Friction);
	const bool bZeroBraking = BrakingDeceleration == 0.0f;

	if (bZeroFriction || bZeroBraking)
	{
		return;
	}

	const FVector OldVel = Velocity;

	// subdivide braking to get reasonably consistent results at lower frame rates
	// (important for packet loss situations w/ networking)
	float RemainingTime = DeltaTime;
	const float MaxTimeStep = FMath::Clamp(BrakingSubStepTime, 1.0f / 75.0f, 1.0f / 20.0f);

	// Decelerate to brake to a stop
	const FVector RevAccel = -Velocity.GetSafeNormal();
	while (RemainingTime >= MIN_TICK_TIME)
	{
		const float Delta = (RemainingTime > MaxTimeStep ? FMath::Min(MaxTimeStep, RemainingTime * 0.5f) : RemainingTime);
		RemainingTime -= Delta;

		// apply friction and braking
		Velocity += (Friction * BrakingDeceleration * RevAccel) * Delta;

		// Don't reverse direction
		if ((Velocity | OldVel) <= 0.0f)
		{
			Velocity = FVector::ZeroVector;
			return;
		}
	}

	// Clamp to zero if nearly zero
	if (Velocity.IsNearlyZero(KINDA_SMALL_NUMBER))
	{
		Velocity = FVector::ZeroVector;
	}
}

UPBMoveStepSound* UPBPlayerMovement::GetMoveStepSoundBySurface(EPhysicalSurface SurfaceType) const
{
	TSubclassOf<UPBMoveStepSound>* GotSound = PBCharacter->GetMoveStepSound(TEnumAsByte<EPhysicalSurface>(SurfaceType));

	if (GotSound)
	{
		return GotSound->GetDefaultObject();
	}

	return nullptr;
}


void UPBPlayerMovement::PlayMoveSound(const float DeltaTime)
{
	if (!bShouldPlayMoveSounds)
	{
		return;
	}

	// Count move sound time down if we've got it
	if (MoveSoundTime > 0.0f)
	{
		MoveSoundTime = FMath::Max(0.0f, MoveSoundTime - 1000.0f * DeltaTime);
	}

	// Check if it's time to play the sound
	if (MoveSoundTime > 0.0f)
	{
		return;
	}

	const float Speed = Velocity.SizeSquared();
	float RunSpeedThreshold;
	float SprintSpeedThreshold;

	if (IsCrouching() || bOnLadder)
	{
		RunSpeedThreshold = MaxWalkSpeedCrouched;
		SprintSpeedThreshold = MaxWalkSpeedCrouched * 1.7f;
	}
	else
	{
		RunSpeedThreshold = MaxWalkSpeed;
		SprintSpeedThreshold = SprintSpeed;
	}

	// Only play sounds if we are moving fast enough on the ground or on a
	// ladder
	const bool bPlaySound = (bBrakingFrameTolerated || bOnLadder) && Speed >= RunSpeedThreshold * RunSpeedThreshold;

	if (!bPlaySound)
	{
		return;
	}

	const bool bSprinting = Speed >= SprintSpeedThreshold * SprintSpeedThreshold;

	float MoveSoundVolume = 0.f;

	UPBMoveStepSound* MoveSound = nullptr;

	if (bOnLadder)
	{
		MoveSoundVolume = 0.5f;
		MoveSoundTime = 450.0f;
		MoveSound = GetMoveStepSoundBySurface(SurfaceType1);
	}
	else
	{
		MoveSoundTime = bSprinting ? 300.0f : 400.0f;
		const FHitResult Hit = CurrentFloor.HitResult;

		if (Hit.PhysMaterial.IsValid())
		{
			MoveSound = GetMoveStepSoundBySurface(Hit.PhysMaterial->SurfaceType);
		}
		if (!MoveSound)
		{
			MoveSound = GetMoveStepSoundBySurface(SurfaceType_Default);
		}

		// Double-check that is valid before accessing it
		if (MoveSound)
		{
			MoveSoundVolume = bSprinting ? MoveSound->GetSprintVolume() : MoveSound->GetWalkVolume();

			if (IsCrouching())
			{
				MoveSoundVolume *= 0.65f;
				MoveSoundTime += 100.0f;
			}
		}
	}

	if (MoveSound)
	{
		TArray<USoundCue*> MoveSoundCues;

		if (bSprinting && !bOnLadder)
		{
			MoveSoundCues = StepSide ? MoveSound->GetSprintLeftSounds() : MoveSound->GetSprintRightSounds();
		}
		if (!bSprinting || bOnLadder || MoveSoundCues.Num() < 1)
		{
			MoveSoundCues = StepSide ? MoveSound->GetStepLeftSounds() : MoveSound->GetStepRightSounds();
		}

		// Error handling - Sounds not valid
		if (MoveSoundCues.Num() < 1)	// Sounds array not valid
		{
			// Get default sounds
			MoveSound = GetMoveStepSoundBySurface(SurfaceType_Default);

			if (!MoveSound)
			{
				return;
			}

			if (bSprinting)
			{
				// Get default sprint sounds
				MoveSoundCues = StepSide ? MoveSound->GetSprintLeftSounds() : MoveSound->GetSprintRightSounds();
			}

			if (!bSprinting || MoveSoundCues.Num() < 1)
			{
				// If bSprinting = true, the code enter this IF only if the updated MoveSoundCues with default sprint sounds is not valid (length < 1)
				// If bSprinting = false, the code enter this IF because the walk sounds are not valid and must try to pick them from the default surface
				// Get default walk sounds
				MoveSoundCues = StepSide ? MoveSound->GetStepLeftSounds() : MoveSound->GetStepRightSounds();
			}

			if (MoveSoundCues.Num() < 1)
			{
				// SurfaceType_Default sounds not found, return
				return;
			}
		}

		// Sound array is valid, play a sound
		// If the array has just one element pick that one skipping random
		USoundCue* Sound = MoveSoundCues[MoveSoundCues.Num() == 1 ? 0 : FMath::RandRange(0, MoveSoundCues.Num() - 1)];

		Sound->VolumeMultiplier = MoveSoundVolume;

		const FVector Location = CharacterOwner->GetActorLocation();
		const FVector StepLocation(FVector(Location.X, Location.Y, Location.Z - GetCharacterOwner()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));

		/*UPBGameplayStatics::SpawnSoundAtLocation(CharacterOwner->GetWorld(), Sound, StepLocation);*/
		UGameplayStatics::SpawnSoundAtLocation(CharacterOwner->GetWorld(), Sound, StepLocation);

		StepSide = !StepSide;
	}
}

void UPBPlayerMovement::PlayJumpSound(const FHitResult& Hit, bool bJumped)
{
	if (!bShouldPlayMoveSounds)
	{
		return;
	}

	UPBMoveStepSound* MoveSound = nullptr;
	TSubclassOf<UPBMoveStepSound>* GotSound = nullptr;
	if (Hit.PhysMaterial.IsValid())
	{
		GotSound = PBCharacter->GetMoveStepSound(Hit.PhysMaterial->SurfaceType);
	}
	if (GotSound)
	{
		MoveSound = GotSound->GetDefaultObject();
	}
	if (!MoveSound)
	{
		if (!PBCharacter->GetMoveStepSound(TEnumAsByte<EPhysicalSurface>(SurfaceType_Default)))
		{
			return;
		}
		MoveSound = PBCharacter->GetMoveStepSound(TEnumAsByte<EPhysicalSurface>(SurfaceType_Default))->GetDefaultObject();
	}

	if (MoveSound)
	{
		float MoveSoundVolume;

		// if we didn't jump, adjust volume for landing
		if (!bJumped)
		{
			const float FallSpeed = -Velocity.Z;
			if (FallSpeed > PBCharacter->GetMinSpeedForFallDamage())
			{
				MoveSoundVolume = 1.0f;
			}
			else if (FallSpeed > PBCharacter->GetMinSpeedForFallDamage() / 2.0f)
			{
				MoveSoundVolume = 0.85f;
			}
			else if (FallSpeed < PBCharacter->GetMinLandBounceSpeed())
			{
				MoveSoundVolume = 0.0f;
			}
			else
			{
				MoveSoundVolume = 0.5f;
			}
		}
		else
		{
			MoveSoundVolume = PBCharacter->IsSprinting() ? MoveSound->GetSprintVolume() : MoveSound->GetWalkVolume();
		}

		if (IsCrouching())
		{
			MoveSoundVolume *= 0.65f;
		}

		if (MoveSoundVolume <= 0.0f)
		{
			return;
		}

		const TArray<USoundCue*>& MoveSoundCues = bJumped ? MoveSound->GetJumpSounds() : MoveSound->GetLandSounds();

		if (MoveSoundCues.Num() < 1)
		{
			return;
		}

		// If the array has just one element pick that one skipping random
		USoundCue* Sound = MoveSoundCues[MoveSoundCues.Num() == 1 ? 0 : FMath::RandRange(0, MoveSoundCues.Num() - 1)];

		Sound->VolumeMultiplier = MoveSoundVolume;
		const FVector Location = CharacterOwner->GetActorLocation();
		const FVector StepLocation(Location.X, Location.Y, Location.Z - GetCharacterOwner()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		/*UPBGameplayStatics::SpawnSoundAtLocation(CharacterOwner->GetWorld(), Sound, StepLocation);*/
		UGameplayStatics::SpawnSoundAtLocation(CharacterOwner->GetWorld(), Sound, StepLocation);
	}
}

#if WIP_SURFING
void UPBPlayerMovement::PreemptCollision(float DeltaTime, float SurfaceFriction)
{
	if (Velocity.IsNearlyZero())
	{
		return;
	}

	// Surfing
	if (!IsMovingOnGround())
	{
		FHitResult HitResult;
		FVector Start = GetCharacterOwner()->GetCapsuleComponent()->GetComponentLocation() -
						FVector(0.0f, 0.0f, GetCharacterOwner()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		float FloorSweepTraceDist = MaxStepHeight + MAX_FLOOR_DIST + KINDA_SMALL_NUMBER;
		FVector End = Start - FVector(0.0f, 0.0f, FloorSweepTraceDist);
		GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, UpdatedComponent->GetCollisionObjectType(),
											 FCollisionQueryParams(FName(TEXT("SurfTrace")), true, GetCharacterOwner()));
		if (HitResult.bBlockingHit && HitResult.ImpactNormal.Z < GetWalkableFloorZ())
		{
			FVector MovementVector(Velocity.X, Velocity.Y, Velocity.Z);
			// vf = vi + at
			MovementVector.Z +=
				(GetCharacterOwner()->GetCapsuleComponent()->GetPhysicsLinearVelocity().Z + GetGravityZ() * DeltaTime * (1.0f - HitResult.ImpactNormal.Z));
			float FrictionMult = FMath::Min(0.0f, -2.0f + SurfaceFriction);
			FVector CollisionVec = HitResult.ImpactNormal;
			CollisionVec *= FrictionMult;
			CollisionVec *= (MovementVector | HitResult.ImpactNormal);
			CollisionVec.X = 0.0f;
			CollisionVec.Y = 0.0f;
			float Speed = Velocity.SizeSquared2D();
			if (CollisionVec.Z * CollisionVec.Z > Speed)
			{
				CollisionVec.Z = FMath::Sqrt(Speed);
			}
			AddImpulse(CollisionVec, true);
		}
	}
}
#endif // WIP_SURFING

void UPBPlayerMovement::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	// UE4-COPY: void UCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)

	// Do not update velocity when using root motion or when SimulatedProxy -
	// SimulatedProxy are repped their Velocity
	if (!HasValidData() || HasAnimRootMotion() || DeltaTime < MIN_TICK_TIME ||
		(CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy && !bWasSimulatingRootMotion))
	{
		return;
	}

	Friction = FMath::Max(0.0f, Friction);
	const float MaxAccel = GetMaxAcceleration();
	float MaxSpeed = GetMaxSpeed();

	// Player doesn't path follow
#if 0
	// Check if path following requested movement
	bool bZeroRequestedAcceleration = true;
	FVector RequestedAcceleration = FVector::ZeroVector;
	float RequestedSpeed = 0.0f;
	if (ApplyRequestedMove(DeltaTime, MaxAccel, MaxSpeed, Friction, BrakingDeceleration, RequestedAcceleration, RequestedSpeed))
	{
		RequestedAcceleration = RequestedAcceleration.GetClampedToMaxSize(MaxAccel);
		bZeroRequestedAcceleration = false;
	}
#endif

	if (bForceMaxAccel)
	{
		// Force acceleration at full speed.
		// In consideration order for direction: Acceleration, then Velocity, then Pawn's rotation.
		if (Acceleration.SizeSquared() > SMALL_NUMBER)
		{
			Acceleration = Acceleration.GetSafeNormal() * MaxAccel;
		}
		else
		{
			Acceleration = MaxAccel * (Velocity.SizeSquared() < SMALL_NUMBER ? UpdatedComponent->GetForwardVector() : Velocity.GetSafeNormal());
		}

		AnalogInputModifier = 1.0f;
	}

#if 0
	// Path following above didn't care about the analog modifier, but we do for everything else below, so get the fully modified value.
	// Use max of requested speed and max speed if we modified the speed in ApplyRequestedMove above.
	const float MaxInputSpeed = FMath::Max(MaxSpeed * AnalogInputModifier, GetMinAnalogSpeed());
	MaxSpeed = FMath::Max(RequestedSpeed, MaxInputSpeed);
#else
	MaxSpeed = FMath::Max(MaxSpeed * AnalogInputModifier, GetMinAnalogSpeed());
#endif

	// Apply braking or deceleration
	const bool bZeroAcceleration = Acceleration.IsNearlyZero();
	const bool bIsGroundMove = IsMovingOnGround() && bBrakingFrameTolerated;

	if (!IsFalling() && CurrentFloor.IsWalkableFloor())
	{
		SurfaceFriction = GetFrictionFromHit(CurrentFloor.HitResult);
	}
	else
	{
		constexpr float JumpVelocity = 266.7f;
		const bool bPlayerControlsMovedVertically = bOnLadder || Velocity.Z > JumpVelocity || Velocity.Z <= 0.0f;
		SurfaceFriction = bPlayerControlsMovedVertically ? 1.0f : 0.25f;
	}

	// Apply friction
	if (bIsGroundMove)
	{
		const bool bVelocityOverMax = IsExceedingMaxSpeed(MaxSpeed);
		const FVector OldVelocity = Velocity;

		const float ActualBrakingFriction = (bUseSeparateBrakingFriction ? BrakingFriction : Friction) * SurfaceFriction;
		ApplyVelocityBraking(DeltaTime, ActualBrakingFriction, BrakingDeceleration);

		// Don't allow braking to lower us below max speed if we started above it.
		if (bVelocityOverMax && Velocity.SizeSquared() < FMath::Square(MaxSpeed) && FVector::DotProduct(Acceleration, OldVelocity) > 0.0f)
		{
			Velocity = OldVelocity.GetSafeNormal() * MaxSpeed;
		}
	}

	// Apply fluid friction
	if (bFluid)
	{
		Velocity = Velocity * (1.0f - FMath::Min(Friction * DeltaTime, 1.0f));
	}

	// Limit before
	if (Velocity.X > AxisSpeedLimit)
	{
		Velocity.X = AxisSpeedLimit;
	}

	if (Velocity.Y > AxisSpeedLimit)
	{
		Velocity.Y = AxisSpeedLimit;
	}

	if (Velocity.Z > AxisSpeedLimit)
	{
		Velocity.Z = AxisSpeedLimit;
	}

	// no clip
	if (bCheatFlying)
	{
		if (bZeroAcceleration)
		{
			Velocity = FVector(0.0f);
		}
		else
		{
			auto LookVec = CharacterOwner->GetControlRotation().Vector();
			auto LookVec2D = CharacterOwner->GetActorForwardVector();
			LookVec2D.Z = 0.0f;
			auto PerpendicularAccel = (LookVec2D | Acceleration) * LookVec2D;
			auto TangentialAccel = Acceleration - PerpendicularAccel;
			auto UnitAcceleration = Acceleration;
			auto Dir = UnitAcceleration.CosineAngle2D(LookVec);
			auto NoClipAccelClamp = PBCharacter->IsSprinting() ? 2.0f * MaxAcceleration : MaxAcceleration;
			Velocity = (Dir * LookVec * PerpendicularAccel.Size2D() + TangentialAccel).GetClampedToSize(NoClipAccelClamp, NoClipAccelClamp);
		}
	}
	else
	{
		// Apply input acceleration
		if (!bZeroAcceleration)
		{
			// Clamp acceleration to max speed
			Acceleration = Acceleration.GetClampedToMaxSize2D(MaxSpeed);
			// Find veer
			const FVector AccelDir = Acceleration.GetSafeNormal2D();
			const float Veer = Velocity.X * AccelDir.X + Velocity.Y * AccelDir.Y;
			// Get add speed with air speed cap
			const float AddSpeed = (bIsGroundMove ? Acceleration : Acceleration.GetClampedToMaxSize2D(AirSpeedCap)).Size2D() - Veer;
			if (AddSpeed > 0.0f)
			{
				// Apply acceleration
				float AccelerationMultiplier = bIsGroundMove ? GroundAccelerationMultiplier : AirAccelerationMultiplier;
				Acceleration *= AccelerationMultiplier * SurfaceFriction * DeltaTime;
				Acceleration = Acceleration.GetClampedToMaxSize2D(AddSpeed);
				Velocity += Acceleration;
			}
		}

		// No requested accel on player
#if 0
		// Apply additional requested acceleration
		if (!bZeroRequestedAcceleration)
		{
			Velocity += RequestedAcceleration * DeltaTime;
		}
#endif

		// TODO: Surfing
#if WIP_SURFING
		PreemptCollision(DeltaTime, SurfaceFriction);
#endif

		// Limit afterwards
		if (Velocity.X > AxisSpeedLimit)
		{
			Velocity.X = AxisSpeedLimit;
		}

		if (Velocity.Y > AxisSpeedLimit)
		{
			Velocity.Y = AxisSpeedLimit;
		}

		if (Velocity.Z > AxisSpeedLimit)
		{
			Velocity.Z = AxisSpeedLimit;
		}

		float SpeedSq = Velocity.SizeSquared2D();

		// Dynamic step height code for allowing sliding on a slope when at a high speed
		if (SpeedSq <= MaxWalkSpeedCrouched * MaxWalkSpeedCrouched)
		{
			// If we're crouching or not sliding, just use max
			MaxStepHeight = GetClass()->GetDefaultObject<UPBPlayerMovement>()->MaxStepHeight;
			SetWalkableFloorZ(GetClass()->GetDefaultObject<UPBPlayerMovement>()->GetWalkableFloorZ());
		}
		else
		{
			// Scale step/ramp height down the faster we go
			float Speed = FMath::Sqrt(SpeedSq);
			float SpeedScale = (Speed - SpeedMultMin) / (SpeedMultMax - SpeedMultMin);
			float SpeedMultiplier = FMath::Clamp(SpeedScale, 0.0f, 1.0f);
			SpeedMultiplier *= SpeedMultiplier;
			if (!IsFalling())
			{
				// If we're on ground, factor in friction.
				SpeedMultiplier = FMath::Max((1.0f - SurfaceFriction) * SpeedMultiplier, 0.0f);
			}
			MaxStepHeight = FMath::Lerp(DefaultStepHeight, MinStepHeight, SpeedMultiplier);
			SetWalkableFloorZ(FMath::Lerp(DefaultWalkableFloorZ, 0.9848f, SpeedMultiplier));
		}
	}

	// Players don't use RVO avoidance
#if 0
	if (bUseRVOAvoidance)
	{
		CalcAvoidanceVelocity(DeltaTime);
	}
#endif
}

#if ENGINE_MAJOR_VERSION == 4
void UPBPlayerMovement::SetCrouchedHalfHeight(const float NewValue)
{
	CrouchedHalfHeight = NewValue;

	if (PBCharacter != nullptr)
	{
		PBCharacter->RecalculateCrouchedEyeHeight();
	}
}

float UPBPlayerMovement::GetCrouchedHalfHeight() const
{ 
	return CrouchedHalfHeight; 
}
#endif

void UPBPlayerMovement::Crouch(bool bClientSimulation)
{
	bIsInCrouchTransition = true;
}

void UPBPlayerMovement::DoCrouchResize(float TargetTime, float DeltaTime, bool bClientSimulation)
{
	// UE4-COPY: void UCharacterMovementComponent::Crouch(bool bClientSimulation)

	if (!HasValidData() || (!bClientSimulation && !CanCrouchInCurrentState()))
	{
		bIsInCrouchTransition = false;
		return;
	}

	// See if collision is already at desired size.
	UCapsuleComponent* CharacterCapsule = CharacterOwner->GetCapsuleComponent();
	if (FMath::IsNearlyEqual(CharacterCapsule->GetUnscaledCapsuleHalfHeight(), GetCrouchedHalfHeight()))
	{
		if (!bClientSimulation)
		{
			CharacterOwner->bIsCrouched = true;
		}
		CharacterOwner->OnStartCrouch(0.0f, 0.0f);
		bIsInCrouchTransition = false;
		return;
	}

	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();

	if (bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		// restore collision size before crouching
		CharacterCapsule->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
		bShrinkProxyCapsule = true;
	}

	// Change collision size to crouching dimensions
	const auto ComponentScale = CharacterCapsule->GetShapeScale();
	const auto OldUnscaledHalfHeight = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float OldUnscaledRadius = CharacterCapsule->GetUnscaledCapsuleRadius();
	const float FullCrouchDiff = OldUnscaledHalfHeight - GetCrouchedHalfHeight();
	float CurrentUnscaledHalfHeight = CharacterCapsule->GetUnscaledCapsuleHalfHeight();
	// Determine the crouching progress
	const bool bInstantCrouch = FMath::IsNearlyZero(TargetTime);
	float CurrentAlpha = 1.0f - (CurrentUnscaledHalfHeight - GetCrouchedHalfHeight()) / FullCrouchDiff;
	// Determine how much we are progressing this tick
	float TargetAlphaDiff = 1.0f;
	float TargetAlpha = 1.0f;
	if (!bInstantCrouch)
	{
		TargetAlphaDiff = DeltaTime / CrouchTime;
		TargetAlpha = CurrentAlpha + TargetAlphaDiff;
	}
	if (TargetAlpha >= 1.0f || FMath::IsNearlyEqual(TargetAlpha, 1.0f))
	{
		TargetAlpha = 1.0f;
		TargetAlphaDiff = TargetAlpha - CurrentAlpha;
		bIsInCrouchTransition = false;
		CharacterOwner->bIsCrouched = true;
	}
	// Determine the target height for this tick
	float TargetCrouchedHalfHeight = OldUnscaledHalfHeight - FullCrouchDiff * TargetAlpha;
	// Height is not allowed to be smaller than radius.
	float ClampedCrouchedHalfHeight = FMath::Max3(0.0f, OldUnscaledRadius, TargetCrouchedHalfHeight);
	CharacterCapsule->SetCapsuleSize(OldUnscaledRadius, ClampedCrouchedHalfHeight);
	float HalfHeightAdjust = FullCrouchDiff * TargetAlphaDiff;
	float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	if (!bClientSimulation)
	{
		if (bCrouchMaintainsBaseLocation)
		{
			// Intentionally not using MoveUpdatedComponent, where a horizontal
			// plane constraint would prevent the base of the capsule from
			// staying at the same spot.
			UpdatedComponent->MoveComponent(FVector(0.0f, 0.0f, -ScaledHalfHeightAdjust), UpdatedComponent->GetComponentQuat(), true, nullptr, MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
		}
		else
		{
			UpdatedComponent->MoveComponent(FVector(0.0f, 0.0f, ScaledHalfHeightAdjust), UpdatedComponent->GetComponentQuat(), true, nullptr, MOVECOMP_NoFlags, ETeleportType::None);
		}
		CharacterOwner->BaseEyeHeight = FMath::Lerp(DefaultCharacter->BaseEyeHeight, CharacterOwner->CrouchedEyeHeight, TargetAlpha);
	}

	bForceNextFloorCheck = true;

	AdjustProxyCapsuleSize();
	CharacterOwner->OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// Don't smooth this change in mesh position
	if ((bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) || (IsNetMode(NM_ListenServer) && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy))
	{
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		if (ClientData)
		{
			ClientData->MeshTranslationOffset -= FVector(0.0f, 0.0f, ScaledHalfHeightAdjust);
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}
}

void UPBPlayerMovement::UnCrouch(bool bClientSimulation)
{
	bIsInCrouchTransition = true;
}

void UPBPlayerMovement::DoUnCrouchResize(float TargetTime, float DeltaTime, bool bClientSimulation)
{
	// UE4-COPY: void UCharacterMovementComponent::UnCrouch(bool bClientSimulation)

	if (!HasValidData())
	{
		bIsInCrouchTransition = false;
		return;
	}

	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();

	UCapsuleComponent* CharacterCapsule = CharacterOwner->GetCapsuleComponent();

	// See if collision is already at desired size.
	if (FMath::IsNearlyEqual(CharacterCapsule->GetUnscaledCapsuleHalfHeight(), DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()))
	{
		if (!bClientSimulation)
		{
			CharacterOwner->bIsCrouched = false;
		}
		CharacterOwner->OnEndCrouch(0.0f, 0.0f);
		bCrouchFrameTolerated = false;
		bIsInCrouchTransition = false;
		return;
	}

	const float CurrentCrouchedHalfHeight = CharacterCapsule->GetScaledCapsuleHalfHeight();

	const float ComponentScale = CharacterCapsule->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterCapsule->GetUnscaledCapsuleHalfHeight();
	const float UncrouchedHeight = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float FullCrouchDiff = UncrouchedHeight - GetCrouchedHalfHeight();
	// Determine the crouching progress
	const bool InstantCrouch = FMath::IsNearlyZero(TargetTime);
	float CurrentAlpha = 1.0f - (UncrouchedHeight - OldUnscaledHalfHeight) / FullCrouchDiff;
	float TargetAlphaDiff = 1.0f;
	float TargetAlpha = 1.0f;
	const UWorld* MyWorld = GetWorld();
	const FVector PawnLocation = UpdatedComponent->GetComponentLocation();
	if (!InstantCrouch)
	{
		TargetAlphaDiff = DeltaTime / TargetTime;
		TargetAlpha = CurrentAlpha + TargetAlphaDiff;
		// Don't partial uncrouch in tight places (like vents)
		if (bCrouchMaintainsBaseLocation)
		{
			// Try to stay in place and see if the larger capsule fits. We use a
			// slightly taller capsule to avoid penetration.
			const float SweepInflation = KINDA_SMALL_NUMBER * 10.0f;
			FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CrouchTrace), false, CharacterOwner);
			FCollisionResponseParams ResponseParam;
			InitCollisionParams(CapsuleParams, ResponseParam);

			// Check how much we have left to go (with some wiggle room to still allow for partial uncrouches in some areas)
			const float HalfHeightAdjust = ComponentScale * (UncrouchedHeight - OldUnscaledHalfHeight) * GroundUncrouchCheckFactor;

			// Compensate for the difference between current capsule size and standing size
			// Shrink by negative amount, so actually grow it.
			const FCollisionShape StandingCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, -SweepInflation - HalfHeightAdjust);
			const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();
			FVector StandingLocation = PawnLocation + FVector(0.0f, 0.0f, StandingCapsuleShape.GetCapsuleHalfHeight() - CurrentCrouchedHalfHeight);
			bool bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, FQuat::Identity, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
			if (bEncroached)
			{
				// We're blocked from doing a full uncrouch, so don't attempt for now
				return;
			}
		}
	}
	if (TargetAlpha >= 1.0f || FMath::IsNearlyEqual(TargetAlpha, 1.0f))
	{
		TargetAlpha = 1.0f;
		TargetAlphaDiff = TargetAlpha - CurrentAlpha;
		bIsInCrouchTransition = false;
	}
	const float HalfHeightAdjust = FullCrouchDiff * TargetAlphaDiff;
	const float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	// Grow to uncrouched size.
	check(CharacterCapsule);

	if (!bClientSimulation)
	{
		// Try to stay in place and see if the larger capsule fits. We use a
		// slightly taller capsule to avoid penetration.
		const float SweepInflation = KINDA_SMALL_NUMBER * 10.0f;
		FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CrouchTrace), false, CharacterOwner);
		FCollisionResponseParams ResponseParam;
		InitCollisionParams(CapsuleParams, ResponseParam);

		// Compensate for the difference between current capsule size and
		// standing size
		// Shrink by negative amount, so actually grow it.
		const FCollisionShape StandingCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, -SweepInflation - ScaledHalfHeightAdjust);
		const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();
		bool bEncroached = true;

		if (!bCrouchMaintainsBaseLocation)
		{
			// Expand in place
			bEncroached = MyWorld->OverlapBlockingTestByChannel(PawnLocation, FQuat::Identity, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);

			if (bEncroached)
			{
				// Try adjusting capsule position to see if we can avoid
				// encroachment.
				if (ScaledHalfHeightAdjust > 0.0f)
				{
					// Shrink to a short capsule, sweep down to base to find
					// where that would hit something, and then try to stand up
					// from there.
					float PawnRadius, PawnHalfHeight;
					CharacterCapsule->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);
					const float ShrinkHalfHeight = PawnHalfHeight - PawnRadius;
					const float TraceDist = PawnHalfHeight - ShrinkHalfHeight;

					FHitResult Hit(1.0f);
					const FCollisionShape ShortCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, ShrinkHalfHeight);

					if (!Hit.bStartPenetrating)
					{
						// Compute where the base of the sweep ended up, and see
						// if we can stand there
						const float DistanceToBase = (Hit.Time * TraceDist) + ShortCapsuleShape.Capsule.HalfHeight;
						const FVector NewLoc = FVector(PawnLocation.X, PawnLocation.Y, PawnLocation.Z - DistanceToBase + StandingCapsuleShape.Capsule.HalfHeight + SweepInflation + MIN_FLOOR_DIST / 2.0f);
						bEncroached = MyWorld->OverlapBlockingTestByChannel(NewLoc, FQuat::Identity, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
						if (!bEncroached)
						{
							// Intentionally not using MoveUpdatedComponent,
							// where a horizontal plane constraint would prevent
							// the base of the capsule from staying at the same
							// spot.
							UpdatedComponent->MoveComponent(NewLoc - PawnLocation, UpdatedComponent->GetComponentQuat(), false, nullptr, MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
						}
					}
				}
			}
		}
		else
		{
			// Expand while keeping base location the same.
			FVector StandingLocation = PawnLocation + FVector(0.0f, 0.0f, StandingCapsuleShape.GetCapsuleHalfHeight() - CurrentCrouchedHalfHeight);
			bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, FQuat::Identity, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);

			if (bEncroached)
			{
				if (IsMovingOnGround())
				{
					// Something might be just barely overhead, try moving down
					// closer to the floor to avoid it.
					const float MinFloorDist = KINDA_SMALL_NUMBER * 10.0f;
					if (CurrentFloor.bBlockingHit && CurrentFloor.FloorDist > MinFloorDist)
					{
						StandingLocation.Z -= CurrentFloor.FloorDist - MinFloorDist;
						bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, FQuat::Identity, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
					}
				}
			}

			if (!bEncroached)
			{
				// Commit the change in location.
				UpdatedComponent->MoveComponent(StandingLocation - PawnLocation, UpdatedComponent->GetComponentQuat(), false, nullptr, MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
				bForceNextFloorCheck = true;
			}

			CharacterOwner->BaseEyeHeight = FMath::Lerp(CharacterOwner->CrouchedEyeHeight, DefaultCharacter->BaseEyeHeight, TargetAlpha);
		}

		// If still encroached then abort.
		if (bEncroached)
		{
			return;
		}

		CharacterOwner->bIsCrouched = false;
	}
	else
	{
		bShrinkProxyCapsule = true;
	}

	// Now call SetCapsuleSize() to cause touch/untouch events and actually grow
	// the capsule
	CharacterCapsule->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), OldUnscaledHalfHeight + HalfHeightAdjust, true);

	const float MeshAdjust = ScaledHalfHeightAdjust;
	AdjustProxyCapsuleSize();
	CharacterOwner->OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	bCrouchFrameTolerated = false;

	// Don't smooth this change in mesh position
	if ((bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) || (IsNetMode(NM_ListenServer) && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy))
	{
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		if (ClientData)
		{
			ClientData->MeshTranslationOffset += FVector(0.0f, 0.0f, MeshAdjust);
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}
}

bool UPBPlayerMovement::CanAttemptJump() const
{
	bool bCanAttemptJump = IsJumpAllowed();
	if (IsMovingOnGround())
	{
		const float FloorZ = FVector(0.0f, 0.0f, 1.0f) | CurrentFloor.HitResult.ImpactNormal;
		const float WalkableFloor = GetWalkableFloorZ();
		bCanAttemptJump &= (FloorZ >= WalkableFloor) || FMath::IsNearlyEqual(FloorZ, WalkableFloor);
	}
	else if (!IsFalling())
	{
		bCanAttemptJump &= bOnLadder;
	}
	return bCanAttemptJump;
}

float UPBPlayerMovement::GetMaxSpeed() const
{
	if (bCheatFlying)
	{
		return (PBCharacter->IsSprinting() ? SprintSpeed : WalkSpeed) * 1.5f;
	}
	float Speed;
	if (PBCharacter->IsSprinting())
	{
		if (IsCrouching() && bCrouchFrameTolerated)
		{
			Speed = MaxWalkSpeedCrouched * 1.7f;
		}
		else
		{
			Speed = SprintSpeed;
		}
	}
	else if (PBCharacter->DoesWantToWalk())
	{
		Speed = WalkSpeed;
	}
	else if (IsCrouching() && bCrouchFrameTolerated)
	{
		Speed = MaxWalkSpeedCrouched;
	}
	else
	{
		Speed = RunSpeed;
	}

	return Speed;
}
