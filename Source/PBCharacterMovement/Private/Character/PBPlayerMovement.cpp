// Copyright Project Borealis

#include "Character/PBPlayerMovement.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Sound/SoundCue.h"

#if WITH_EDITOR
#include "DrawDebugHelpers.h"
#endif

#include "Sound/PBMoveStepSound.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PBPlayerMovement)

static TAutoConsoleVariable<int32> CVarShowPos(TEXT("cl.ShowPos"), 0, TEXT("Show position and movement information.\n"), ECVF_Default);

static TAutoConsoleVariable<int32> CVarAlwaysApplyFriction(TEXT("move.AlwaysApplyFriction"), 0, TEXT("Apply friction, even in air.\n"), ECVF_Default);

DECLARE_CYCLE_STAT(TEXT("Char StepUp"), STAT_CharStepUp, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char PhysFalling"), STAT_CharPhysFalling, STATGROUP_Character);

// MAGIC NUMBERS
constexpr float JumpVelocity = 266.7f;
const float MAX_STEP_SIDE_Z = 0.08f;          // maximum z value for the normal on the vertical side of steps
const float VERTICAL_SLOPE_NORMAL_Z = 0.001f; // Slope is vertical if Abs(Normal.Z) <= this threshold. Accounts for precision problems that sometimes angle
// normals slightly off horizontal for vertical surface.

#ifndef USE_HL2_GRAVITY
#define USE_HL2_GRAVITY 1
#endif

// Purpose: override default player movement
UPBPlayerMovement::UPBPlayerMovement()
{
	// We have our own air movement handling, so we can allow for full air
	// control through UE's logic
	AirControl = 1.0f;
	// Disable air control boost
	AirControlBoostMultiplier = 0.0f;
	AirControlBoostVelocityThreshold = 0.0f;
	// HL2 cl_(forward & side)speed = 450Hu
	MaxAcceleration = 857.25f;
	// Set the default walk speed
	WalkSpeed = 285.75f;
	RunSpeed = 361.9f;
	SprintSpeed = 609.6f;
	MaxWalkSpeed = RunSpeed;
	// Acceleration multipliers (HL2's sv_accelerate and sv_airaccelerate)
	GroundAccelerationMultiplier = 10.0f;
	AirAccelerationMultiplier = 10.0f;
	// 30 air speed cap from HL2
	AirSpeedCap = 57.15f;
	AirSlideSpeedCap = 57.15f;
	// HL2 like friction
	// sv_friction
	GroundFriction = 4.0f;
	BrakingFriction = 4.0f;
	SurfaceFriction = 1.0f;
	bUseSeparateBrakingFriction = false;
	// Edge friction
	EdgeFrictionMultiplier = 2.0f;
	EdgeFrictionHeight = 64.77f;
	EdgeFrictionDist = 30.48f;
	bEdgeFrictionOnlyWhenBraking = false;
	bEdgeFrictionAlwaysWhenCrouching = false;
	// No multiplier
	BrakingFrictionFactor = 1.0f;
	// Historical value for Source
	BrakingSubStepTime = 1 / 66.0f;
	// Time step
	MaxSimulationTimeStep = 1 / 66.0f;
	MaxSimulationIterations = 25;
	MaxJumpApexAttemptsPerSimulation = 4;
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
	MinStepHeight = 10.0f;
	StepDownHeightFraction = 0.9f;
	// Perching
	// We try to avoid going too broad with perching as it can cause a sliding issue with jumping on edges
	PerchRadiusThreshold = 0.5f; // 0.5 is the minimum value to prevent snags
	PerchAdditionalHeight = 0.0f;
	// Jump z from HL2's 160Hu
	// 21Hu jump height
	// 510ms jump time
	JumpZVelocity = 304.8f;
	// Don't bounce off characters
	JumpOffJumpZFactor = 0.0f;
	// Default show pos to false
	bShowPos = false;
	// We aren't on a ladder at first
	bOnLadder = false;
	OffLadderTicks = LADDER_MOUNT_TIMEOUT;
	LadderSpeed = 381.0f;
	// Speed multiplier bounds
	SpeedMultMin = SprintSpeed * 1.7f;
	SpeedMultMax = SprintSpeed * 2.5f;
	DefaultSpeedMultMin = SpeedMultMin;
	DefaultSpeedMultMax = SpeedMultMax;
	// Start out braking
	bBrakingFrameTolerated = true;
	BrakingWindowTimeElapsed = 0.0f;
	BrakingWindow = 0.015f;
	// Crouching
	SetCrouchedHalfHeight(34.29f);
	MaxWalkSpeedCrouched = RunSpeed * 0.33333333f;
	bCanWalkOffLedgesWhenCrouching = true;
	CrouchTime = MOVEMENT_DEFAULT_CROUCHTIME;
	UncrouchTime = MOVEMENT_DEFAULT_UNCROUCHTIME;
	CrouchJumpTime = MOVEMENT_DEFAULT_CROUCHJUMPTIME;
	UncrouchJumpTime = MOVEMENT_DEFAULT_UNCROUCHJUMPTIME;
	// Slope angle is 45.57 degrees
	SetWalkableFloorZ(0.7f);
	DefaultWalkableFloorZ = GetWalkableFloorZ();
	AxisSpeedLimit = 6667.5f;
	// Tune physics interactions
	StandingDownwardForceScale = 1.0f;
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
	Mass = 85.0f; // player.mdl is 85kg
	// Don't smooth rotation at all
	bUseControllerDesiredRotation = false;
	// Flat base
	bUseFlatBaseForFloorChecks = true;
	// Agent props
	NavAgentProps.bCanCrouch = true;
	NavAgentProps.bCanJump = true;
	NavAgentProps.bCanFly = true;
	// Crouch sliding
	bShouldCrouchSlide = false;
	CrouchSlideBoostTime = 0.1f;
	CrouchSlideBoostMultiplier = 1.5f;
	CrouchSlideSpeedRequirementMultiplier = 0.9f;
	MinCrouchSlideBoost = SprintSpeed * CrouchSlideBoostMultiplier;
	MaxCrouchSlideVelocityBoost = 6.0f;
	MinCrouchSlideVelocityBoost = 2.7f;
	CrouchSlideBoostSlopeFactor = 2.7f;
	CrouchSlideCooldown = 1.0f;
#if USE_HL2_GRAVITY
	// Make sure gravity is correct for player movement
	GravityScale = DesiredGravity / UPhysicsSettings::Get()->DefaultGravityZ;
#endif
	// Make sure ramp movement in correct
	bMaintainHorizontalGroundVelocity = true;
	bAlwaysCheckFloor = true;
	// Ignore base rotation
	// TODO: might do well to ONLY ignore base rotation if our base is simulating physics.
	// but the player might want control of their rotation ALWAYS.
	bIgnoreBaseRotation = true;
	bBasedMovementIgnorePhysicsBase = true;
	// Physics interactions
	bEnablePhysicsInteraction = true;
	RepulsionForce = 1.314f;
	MaxTouchForce = 100.0f;
	InitialPushForceFactor = 10.0f;
	PushForceFactor = 100000.0f;
	// Swimming
	Buoyancy = 0.99f;
	// Allow orient rotation during root motion
	bAllowPhysicsRotationDuringAnimRootMotion = true;
	// Prevent NaN
	RequestedVelocity = FVector::ZeroVector;
	// Optimization
	bEnableServerDualMoveScopedMovementUpdates = true;
}

void UPBPlayerMovement::InitializeComponent()
{
	Super::InitializeComponent();
	PBPlayerCharacter = Cast<APBPlayerCharacter>(CharacterOwner);

	// Get defaults from BP
	MaxWalkSpeed = RunSpeed;
	if (SpeedMultMin == DefaultSpeedMultMin)
	{
		// only update if not already customized in BP
		SpeedMultMin = SprintSpeed * 1.7f;
	}
	if (SpeedMultMax == DefaultSpeedMultMax)
	{
		// only update if not already customized in BP
		SpeedMultMax = SprintSpeed * 2.5f;
	}
	DefaultStepHeight = MaxStepHeight;
	DefaultWalkableFloorZ = GetWalkableFloorZ();
}

void UPBPlayerMovement::OnRegister()
{
	Super::OnRegister();

	const bool bIsReplay = (GetWorld() && GetWorld()->IsPlayingReplay());
	if (!bIsReplay && GetNetMode() == NM_ListenServer)
	{
		NetworkSmoothingMode = ENetworkSmoothingMode::Linear;
	}
}

void UPBPlayerMovement::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	PlayMoveSound(DeltaTime);

	if (bHasDeferredMovementMode)
	{
		SetMovementMode(DeferredMovementMode);
		bHasDeferredMovementMode = false;
	}

	// Skip player movement when we're simulating physics (ie ragdoll)
	if (UpdatedComponent->IsSimulatingPhysics())
	{
		return;
	}

	if (bShowPos || CVarShowPos.GetValueOnGameThread() != 0)
	{
		const FVector Position = UpdatedComponent->GetComponentLocation();
		const FRotator Rotation = CharacterOwner->GetControlRotation();
		const float Speed = Velocity.Size();
		GEngine->AddOnScreenDebugMessage(1, 1.0f, FColor::Green, FString::Printf(TEXT("pos: %.02f %.02f %.02f"), Position.X, Position.Y, Position.Z));
		GEngine->AddOnScreenDebugMessage(2, 1.0f, FColor::Green, FString::Printf(TEXT("ang: %.02f %.02f %.02f"), Rotation.Pitch, Rotation.Yaw, Rotation.Roll));
		GEngine->AddOnScreenDebugMessage(3, 1.0f, FColor::Green, FString::Printf(TEXT("vel:  %.02f"), Speed));
	}

	if (RollAngle != 0 && RollSpeed != 0 && GetPBCharacter()->GetController())
	{
		FRotator ControlRotation = GetPBCharacter()->GetController()->GetControlRotation();
		ControlRotation.Roll = GetCameraRoll();
		GetPBCharacter()->GetController()->SetControlRotation(ControlRotation);
	}

	if (IsMovingOnGround())
	{
		if (!bBrakingFrameTolerated)
		{
			BrakingWindowTimeElapsed += DeltaTime;
			if (BrakingWindowTimeElapsed >= BrakingWindow)
			{
				bBrakingFrameTolerated = true;
			}
		}
	}
	else
	{
		bBrakingFrameTolerated = false;
		BrakingWindowTimeElapsed = 0.0f; // Clear window
	}
	bCrouchFrameTolerated = IsCrouching();
}

bool UPBPlayerMovement::DoJump(bool bClientSimulation)
{
	// UE-COPY: UCharacterMovementComponent::DoJump(bool bReplayingMoves)

	if (!bCheatFlying && CharacterOwner && CharacterOwner->CanJump())
	{
		// Don't jump if we can't move up/down.
		if (!bConstrainToPlane || !FMath::IsNearlyEqual(FMath::Abs(GetGravitySpaceZ(PlaneConstraintNormal)), 1.f))
		{
			// If first frame of DoJump, we want to always inject the initial jump velocity.
			// For subsequent frames, during the time Jump is held, it depends... 
			// bDontFallXXXX == true means we want to ensure the character's Z velocity is never less than JumpZVelocity in this period
			// bDontFallXXXX == false means we just want to leave Z velocity alone and "let the chips fall where they may" (e.g. fall properly in physics)

			// NOTE: 
			// Checking JumpCurrentCountPreJump instead of JumpCurrentCount because Character::CheckJumpInput might have
			// incremented JumpCurrentCount just before entering this function... in order to compensate for the case when
			// on the first frame of the jump, we're already in falling stage. So we want the original value before any 
			// modification here.
			// 
			const bool bFirstJump = (CharacterOwner->JumpCurrentCountPreJump == 0);

			if (bFirstJump || bDontFallBelowJumpZVelocityDuringJump)
			{
				const int32 NewJumps = CharacterOwner->JumpCurrentCountPreJump + 1;
				if (IsFalling() && GetCharacterOwner()->JumpMaxCount > 1 && NewJumps <= GetCharacterOwner()->JumpMaxCount)
				{
					if (bAirJumpResetsHorizontal)
					{
						Velocity.X = 0.0f;
						Velocity.Y = 0.0f;
					}
					FVector InputVector = GetCharacterOwner()->GetPendingMovementInputVector() + GetLastInputVector();
					InputVector = InputVector.GetSafeNormal2D();
					Velocity += InputVector * GetMaxAcceleration() * AirJumpDashMagnitude;
					OnAirJump(NewJumps);
				}
				if (HasCustomGravity())
				{
					if (GetGravitySpaceZ(Velocity) < 0.0f)
					{
						SetGravitySpaceZ(Velocity, 0.0f);
					}
					SetGravitySpaceZ(Velocity, GetGravitySpaceZ(Velocity) + JumpZVelocity);
				}
				else
				{
					if (Velocity.Z < 0.0f)
					{
						Velocity.Z = 0.0f;
					}
					
					Velocity.Z += FMath::Max<FVector::FReal>(Velocity.Z, JumpZVelocity);
				}
			}
			
			SetMovementMode(MOVE_Falling);
			return true;
		}
	}
	
	return false;
}

float UPBPlayerMovement::GetFallSpeed(bool bAfterLand)
{
	FVector FallVelocity = Velocity;
	if (bAfterLand)
	{
		const float GravityStep = GetGravityZ() * GetWorld()->GetDeltaSeconds() * 0.5f;
		// we need to do another integration step of gravity before we get fall speed
		if (HasCustomGravity())
		{
			SetGravitySpaceZ(FallVelocity, GetGravitySpaceZ(FallVelocity) + GravityStep);
		}
		else
		{
			FallVelocity.Z += GravityStep;
		}
	}
	return -FallVelocity.Z;
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
	if (IsOnLadder() || bCheatFlying)
	{
		return Super::HandleSlopeBoosting(SlideResult, Delta, Time, Normal, Hit);
	}
	const float WallAngle = FMath::Abs(Hit.ImpactNormal.Z);
	FVector ImpactNormal = Normal;
	// If too extreme, use the more stable hit normal
	if (!(WallAngle <= VERTICAL_SLOPE_NORMAL_Z || WallAngle == 1.0f))
	{
		// Only use new normal if it isn't higher Z, to avoid moving higher than intended.
		// Similar to how ZLimit works in the Super implementation of this function.
		// Second check: if we ARE going for a lower impact normal, make sure it's
		// not in conflict with our delta. If the movement is pushing us up, we want to
		// slide upwards, rather than get pushed back down.
		if (Hit.ImpactNormal.Z <= ImpactNormal.Z && Delta.Z <= 0.0f)
		{
			ImpactNormal = Hit.ImpactNormal;
		}
	}
	if (bConstrainToPlane)
	{
		ImpactNormal = ConstrainNormalToPlane(ImpactNormal);
	}
	const float BounceCoefficient = 1.0f + BounceMultiplier * (1.0f - SurfaceFriction);
	return (Delta - BounceCoefficient * Delta.ProjectOnToNormal(ImpactNormal)) * Time;
}

bool UPBPlayerMovement::ShouldCatchAir(const FFindFloorResult& OldFloor, const FFindFloorResult& NewFloor)
{
	// If the new floor is below the old floor by fraction of max step height, catch air
	const float HeightDiff = NewFloor.HitResult.ImpactPoint.Z - OldFloor.HitResult.ImpactPoint.Z;
	if (HeightDiff < -MaxStepHeight * StepDownHeightFraction)
	{
		return true;
	}

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
#if 0
	return !bUseFlatBaseForFloorChecks && Super::ShouldCheckForValidLandingSpot(DeltaTime, Delta, Hit);
#else
	return Super::ShouldCheckForValidLandingSpot(DeltaTime, Delta, Hit);
#endif
}

void UPBPlayerMovement::HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta)
{
	Super::HandleImpact(Hit, TimeSlice, MoveDelta);
	if (TimeSlice > 0.0f && MoveDelta != FVector::ZeroVector && MoveDelta.Z)
	{
		UpdateSurfaceFriction(true);
	}
}

bool UPBPlayerMovement::IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const
{
	if (!Super::IsValidLandingSpot(CapsuleLocation, Hit))
	{
		return false;
	}

	// Slope bug fix
	// If moving up a slope...
	if (Hit.Normal.Z < 1.0f && (Velocity | Hit.Normal) < 0.0f)
	{
		// Let's calculate how we are gonna deflect off the surface
		FVector DeflectionVector = Velocity;
		// a step of gravity
		DeflectionVector.Z += 0.5f * GetGravityZ() * GetWorld()->GetDeltaSeconds();
		DeflectionVector = ComputeSlideVector(DeflectionVector, 1.0f, Hit.Normal, Hit);

		// going up too fast to land
		if (DeflectionVector.Z > JumpVelocity)
		{
			return false;
		}
	}

	return true;
}

void UPBPlayerMovement::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	// Reset step side if we are changing modes
	StepSide = false;

	// did we jump or land
	bool bJumped = false;
	bool bQueueJumpSound = false;

	// We reset landed state if we switch to a disabled mode. Flying mode should be okay though.
	if (MovementMode == MOVE_None)
	{
		bHasEverLanded = false;
	}

	if (PreviousMovementMode == MOVE_Walking && MovementMode == MOVE_Falling)
	{
		// If we were walking and now falling, we could be jumping.
		bJumped = true;
		// Only if moving up do we play the jump sound effect.
		bQueueJumpSound = Velocity.Z > 0.0f;
	}
	else if (PreviousMovementMode == MOVE_Falling && MovementMode == MOVE_Walking)
	{
		// We want to queue a jump sound here even if we haven't ever landed yet.
		// Since we're in walking state (from falling), we can now do our check for ground to see if we did land.
		bQueueJumpSound = true;
		if (bDeferCrouchSlideToLand)
		{
			bDeferCrouchSlideToLand = false;
			StartCrouchSlide();
		}
	}

	// Noclip goes from: flying -> falling -> walking because of default movement modes
	if (bHasDeferredMovementMode)
	{
		bQueueJumpSound = false;
	}

	// In some cases, we don't play a jump sound because it's either not queued, or we haven't ever landed.
	// in BOTH cases, we still want to handle detecting the first land. because in some cases we're
	// moving from move mode = NONE
	bool bDidPlayJumpSound = false;

	// If we are moving between falling/walking, and we meet conditions for playing a sound in our state.
	if (bQueueJumpSound)
	{
		// If we're intentionally falling off of spawn, then we want to play the land sound
		if (!bHasEverLanded)
		{
			if (GetOwner()->GetGameTimeSinceCreation() > 0.1f)
			{
				bHasEverLanded = true;
			}
		}
		if (bHasEverLanded)
		{
			// If we have found an initial ground from when we did our initial player spawn, we can play a sound.
			FHitResult Hit;
			TraceCharacterFloor(Hit);
			PlayJumpSound(Hit, bJumped);
			bDidPlayJumpSound = true;
		}
	}

	// This needs to be here AFTER PlayJumpSound or else our velocity Z gets reset to 0 before we do land sound.
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	if (!bDidPlayJumpSound)
	{
		if (MovementMode == MOVE_Walking && (GetMovementBase() || CurrentFloor.bBlockingHit))
		{
			// This happens in a couple of cases.
			// First, on initial player spawn, we default to walking.
			// But then we transition to falling immediately after if GetMovementBase() is null. (See SetDefaultMovementMode())
			// So, that walking -> falling transition would be invalid for a jump sound since it's just categorizing initial position in the air.
			// Second, once we are falling (when our player spawn is slightly above the ground), we are going to land.
			// This is generally because the player spawn was placed not accurately to the floor, which is reasonable.
			// But we don't want to play a land sound effect just because our player spawned!
			bHasEverLanded = true;
		}
	}
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

bool UPBPlayerMovement::IsOnLadder() const
{
	return bOnLadder;
}

float UPBPlayerMovement::GetLadderClimbSpeed() const
{
	return LadderSpeed;
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

#ifndef DIRECTIONAL_BRAKING
#define DIRECTIONAL_BRAKING 0
#endif

void UPBPlayerMovement::ApplyVelocityBraking(float DeltaTime, float Friction, float BrakingDeceleration)
{
	// UE4-COPY: void UCharacterMovementComponent::ApplyVelocityBraking(float DeltaTime, float Friction, float BrakingDeceleration)
	if (Velocity.IsNearlyZero(0.1f) || !HasValidData() || HasAnimRootMotion() || DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

#if DIRECTIONAL_BRAKING
	const float ForwardSpeed = FMath::Abs(Velocity | UpdatedComponent->GetForwardVector());
	const float SideSpeed = FMath::Abs(Velocity | UpdatedComponent->GetRightVector());
#else
	const float Speed = Velocity.Size2D();
#endif
	const float FrictionFactor = FMath::Max(0.0f, BrakingFrictionFactor);
	Friction = FMath::Max(0.0f, Friction * FrictionFactor);
#if DIRECTIONAL_BRAKING
	float ForwardBrakingDeceleration = BrakingDeceleration;
	float SideBrakingDeceleration = BrakingDeceleration;
#endif
	if (ShouldCrouchSlide())
	{
#if DIRECTIONAL_BRAKING
		const float Speed = Velocity.Size2D();
#endif
		if (Friction > 1.0f)
		{
			float CurrentTime = GetWorld()->GetTimeSeconds();
			float TimeDifference = CurrentTime - CrouchSlideStartTime;
			// Decay friction reduction
			Friction = FMath::Lerp(1.0f, Friction, FMath::Clamp(TimeDifference / CrouchSlideBoostTime, 0.0f, 1.0f));
		}
		BrakingDeceleration = FMath::Max(10.0f, Speed);
#if DIRECTIONAL_BRAKING
		ForwardBrakingDeceleration = BrakingDeceleration;
		SideBrakingDeceleration = BrakingDeceleration;
#endif
	}
	else
	{
#if DIRECTIONAL_BRAKING
		ForwardBrakingDeceleration = FMath::Max3(BrakingDeceleration, ForwardSpeed, 0.0f);
		SideBrakingDeceleration = FMath::Max3(BrakingDeceleration, SideSpeed, 0.0f);
#else
		BrakingDeceleration = FMath::Max(BrakingDeceleration, Speed);
#endif
	}
	const bool bZeroFriction = FMath::IsNearlyZero(Friction);
#if DIRECTIONAL_BRAKING
	const bool bZeroBraking = ForwardBrakingDeceleration == 0.0f && SideBrakingDeceleration == 0.0f;
#else
	const bool bZeroBraking = BrakingDeceleration == 0.0f;
#endif

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
#if DIRECTIONAL_BRAKING
	const FVector ForwardRevAccel = -FMath::Sign((Velocity.GetSafeNormal() | UpdatedComponent->GetForwardVector())) * UpdatedComponent->GetForwardVector();
	const FVector SideRevAccel = -FMath::Sign((Velocity.GetSafeNormal() | UpdatedComponent->GetRightVector())) * UpdatedComponent->GetRightVector();
#else
	const FVector RevAccel = -Velocity.GetSafeNormal();
#endif
	while (RemainingTime >= MIN_TICK_TIME)
	{
		const float Delta = (RemainingTime > MaxTimeStep ? FMath::Min(MaxTimeStep, RemainingTime * 0.5f) : RemainingTime);
		RemainingTime -= Delta;

		// apply friction and braking
#if DIRECTIONAL_BRAKING
		Velocity += (Friction * ForwardBrakingDeceleration * ForwardRevAccel) * Delta;
		Velocity += (Friction * SideBrakingDeceleration * SideRevAccel) * Delta;
#else
		Velocity += (Friction * BrakingDeceleration * RevAccel) * Delta;
#endif

		// Don't reverse direction
		// TODO: make this directionally separated too?
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

bool UPBPlayerMovement::ShouldLimitAirControl(float DeltaTime, const FVector& FallAcceleration) const
{
	return false;
}

FVector UPBPlayerMovement::NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const
{
	FVector FallVel = Super::NewFallVelocity(InitialVelocity, Gravity, DeltaTime);
	FallVel.Z = FMath::Clamp(FallVel.Z, -AxisSpeedLimit, AxisSpeedLimit);
	return FallVel;
}

void UPBPlayerMovement::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
	Velocity.Z = FMath::Clamp(Velocity.Z, -AxisSpeedLimit, AxisSpeedLimit);
	// reset value for new frame
	bSlidingInAir = false;
	UpdateCrouching(DeltaSeconds);
}

void UPBPlayerMovement::UpdateCharacterStateAfterMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateAfterMovement(DeltaSeconds);
	Velocity.Z = FMath::Clamp(Velocity.Z, -AxisSpeedLimit, AxisSpeedLimit);
	UpdateSurfaceFriction(bSlidingInAir);
	// forward to the next frame
	bWasSlidingInAir = bSlidingInAir;
	UpdateCrouching(DeltaSeconds, true);
}

void UPBPlayerMovement::UpdateSurfaceFriction(bool bIsSliding)
{
	if (!IsFalling() && CurrentFloor.IsWalkableFloor())
	{
		bSlidingInAir = false;
		if (OldBase.Get() != CurrentFloor.HitResult.GetComponent() || !CurrentFloor.HitResult.Component.IsValid())
		{
			OldBase = CurrentFloor.HitResult.GetComponent();
			FHitResult Hit;
			TraceCharacterFloor(Hit);
			SurfaceFriction = GetFrictionFromHit(Hit);
		}
	}
	else
	{
		bSlidingInAir = bIsSliding;
		const bool bPlayerControlsMovedVertically = IsOnLadder() || Velocity.Z > JumpVelocity || Velocity.Z <= 0.0f || bCheatFlying;
		if (bPlayerControlsMovedVertically)
		{
			SurfaceFriction = 1.0f;
		}
		else if (bIsSliding)
		{
			SurfaceFriction = 0.25f;
		}
	}
}

void UPBPlayerMovement::UpdateCrouching(float DeltaTime, bool bOnlyUncrouch)
{
	if (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		return;
	}

	// Crouch transition but not in noclip
	if (bIsInCrouchTransition && !bCheatFlying)
	{
		// If the player wants to uncrouch, or we have to uncrouch after movement
		if ((!bOnlyUncrouch && !bWantsToCrouch) || (bOnlyUncrouch && !CanCrouchInCurrentState()))
		{
			// and the player is not locked in a fully crouched position, we uncrouch
			if (!(bLockInCrouch && CharacterOwner->bIsCrouched))
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
		else if (!bOnlyUncrouch)
		{
			if (IsOnLadder()) // if on a ladder, cancel this because bWantsToCrouch should be false
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
}

void UPBPlayerMovement::StartCrouchSlide()
{
	float CurrentTime = GetWorld()->GetTimeSeconds();
	// Don't boost again if we are already boosting
	if (IsCrouchSliding() || CurrentTime - CrouchSlideStartTime <= CrouchSlideCooldown)
	{
		// Continue crouch sliding if we're going that fast
		if (Velocity.SizeSquared2D() >= MinCrouchSlideBoost * MinCrouchSlideBoost)
		{
			bCrouchSliding = true;
		}
		return;
	}

	const FVector FloorNormal = CurrentFloor.HitResult.ImpactNormal;
	const FVector CrouchSlideInput = GetOwner()->GetActorForwardVector();
	float Slope = (CrouchSlideInput | FloorNormal);
	float NewSpeed = FMath::Max(MinCrouchSlideBoost, Velocity.Size2D() * CrouchSlideBoostMultiplier);
	if (NewSpeed > MinCrouchSlideBoost && Slope < 0.0f)
	{
		NewSpeed = FMath::Clamp(NewSpeed + CrouchSlideBoostSlopeFactor * (NewSpeed - MinCrouchSlideBoost) * Slope, MinCrouchSlideBoost, NewSpeed);
	}
	Velocity = NewSpeed * Velocity.GetSafeNormal2D();
	// Set the time
	CrouchSlideStartTime = CurrentTime;
	bCrouchSliding = true;
}

bool UPBPlayerMovement::ShouldCrouchSlide() const
{
	return bCrouchSliding && IsMovingOnGround();
}

void UPBPlayerMovement::StopCrouchSliding()
{
	bCrouchSliding = false;
	bDeferCrouchSlideToLand = false;
}

void UPBPlayerMovement::ToggleCrouchLock(bool bLock)
{
	bLockInCrouch = bLock;
}

float UPBPlayerMovement::GetFrictionFromHit(const FHitResult& Hit) const
{
	float HitSurfaceFriction = 1.0f;
	if (Hit.PhysMaterial.IsValid())
	{
		HitSurfaceFriction = FMath::Min(1.0f, Hit.PhysMaterial->Friction * 1.25f);
	}
	return HitSurfaceFriction;
}

void UPBPlayerMovement::TraceCharacterFloor(FHitResult& OutHit) const
{
	FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CharacterFloorTrace), false, CharacterOwner);
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(CapsuleParams, ResponseParam);
	// must trace complex to get mesh phys materials
	CapsuleParams.bTraceComplex = true;
	// must get materials
	CapsuleParams.bReturnPhysicalMaterial = true;

	const FCollisionShape StandingCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_None);
	const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();
	FVector PawnLocation = UpdatedComponent->GetComponentLocation();
	PawnLocation.Z -= StandingCapsuleShape.GetCapsuleHalfHeight();
	FVector StandingLocation = PawnLocation;
	StandingLocation.Z -= MAX_FLOOR_DIST * 10.0f;
	GetWorld()->SweepSingleByChannel(OutHit, PawnLocation, StandingLocation, FQuat::Identity, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
}

void UPBPlayerMovement::TraceLineToFloor(FHitResult& OutHit) const
{
	FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(TraceLineToFloor), false, CharacterOwner);
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(CapsuleParams, ResponseParam);

	const FCollisionShape StandingCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_None);
	const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();
	FVector PawnLocation = UpdatedComponent->GetComponentLocation();
	PawnLocation.Z -= StandingCapsuleShape.GetCapsuleHalfHeight();
	if (Acceleration.IsNearlyZero())
	{
		if (!Velocity.IsNearlyZero())
		{
			PawnLocation += Velocity.GetSafeNormal2D() * EdgeFrictionDist;
		}
	}
	else
	{
		PawnLocation += Acceleration.GetSafeNormal2D() * EdgeFrictionDist;
	}
	FVector StandingLocation = PawnLocation;
	StandingLocation.Z -= EdgeFrictionHeight;
	// DrawDebugLine(GetWorld(), PawnLocation, StandingLocation, FColor::Red, false, 10.0f);
	GetWorld()->SweepSingleByChannel(OutHit, PawnLocation, StandingLocation, FQuat::Identity, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
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

	const float Speed = Velocity.SizeSquared2D();
	float WalkSpeedThreshold;
	float SprintSpeedThreshold;

	if (IsCrouching() || IsOnLadder())
	{
		WalkSpeedThreshold = MaxWalkSpeedCrouched;
		SprintSpeedThreshold = MaxWalkSpeedCrouched * 1.7f;
	}
	else
	{
		WalkSpeedThreshold = WalkSpeed;
		SprintSpeedThreshold = SprintSpeed;
	}

	// Only play sounds if we are moving fast enough on the ground or on a ladder
	const bool bPlaySound = (bBrakingFrameTolerated || IsOnLadder()) && Speed >= WalkSpeedThreshold * WalkSpeedThreshold && !ShouldCrouchSlide();

	if (!bPlaySound)
	{
		return;
	}

	const bool bSprinting = Speed >= SprintSpeedThreshold * SprintSpeedThreshold;

	float MoveSoundVolume = 0.f;

	UPBMoveStepSound* MoveSound = nullptr;

	if (IsOnLadder())
	{
		MoveSoundVolume = 0.5f;
		MoveSoundTime = 450.0f;
		MoveSound = GetMoveStepSoundBySurface(SurfaceType1);
	}
	else
	{
		MoveSoundTime = bSprinting ? 300.0f : 400.0f;
		FHitResult Hit;
		TraceCharacterFloor(Hit);

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

		if (bSprinting && !IsOnLadder())
		{
			MoveSoundCues = StepSide ? MoveSound->GetSprintLeftSounds() : MoveSound->GetSprintRightSounds();
		}
		if (!bSprinting || IsOnLadder() || MoveSoundCues.Num() < 1)
		{
			MoveSoundCues = StepSide ? MoveSound->GetStepLeftSounds() : MoveSound->GetStepRightSounds();
		}

		// Error handling - Sounds not valid
		if (MoveSoundCues.Num() < 1) // Sounds array not valid
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

		const FVector StepRelativeLocation(0.0f, 0.0f, -GetCharacterOwner()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());

		UGameplayStatics::SpawnSoundAttached(Sound, UpdatedComponent, NAME_None, StepRelativeLocation, FRotator::ZeroRotator);

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
		GotSound = PBPlayerCharacter->GetMoveStepSound(Hit.PhysMaterial->SurfaceType);
	}
	if (GotSound)
	{
		MoveSound = GotSound->GetDefaultObject();
	}
	if (!MoveSound)
	{
		if (!PBPlayerCharacter->GetMoveStepSound(TEnumAsByte<EPhysicalSurface>(SurfaceType_Default)))
		{
			return;
		}
		MoveSound = PBPlayerCharacter->GetMoveStepSound(TEnumAsByte<EPhysicalSurface>(SurfaceType_Default))->GetDefaultObject();
	}

	if (MoveSound)
	{
		float MoveSoundVolume;

		// if we didn't jump, adjust volume for landing
		if (!bJumped)
		{
			const float FallSpeed = GetFallSpeed(true);
			if (FallSpeed > PBPlayerCharacter->GetMinSpeedForFallDamage())
			{
				MoveSoundVolume = 1.0f;
			}
			else if (FallSpeed > PBPlayerCharacter->GetMinSpeedForFallDamage() / 2.0f)
			{
				MoveSoundVolume = 0.85f;
			}
			else if (FallSpeed < PBPlayerCharacter->GetMinLandBounceSpeed())
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
			MoveSoundVolume = PBPlayerCharacter->IsSprinting() ? MoveSound->GetSprintVolume() : MoveSound->GetWalkVolume();
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

		const FVector StepRelativeLocation(0.0f, 0.0f, -GetCharacterOwner()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());

		UGameplayStatics::SpawnSoundAttached(Sound, UpdatedComponent, NAME_None, StepRelativeLocation, FRotator::ZeroRotator);
	}
}

void UPBPlayerMovement::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	// UE4-COPY: void UCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)

	// Do not update velocity when using root motion or when SimulatedProxy and not simulating root motion - SimulatedProxy are repped their Velocity
	if (!HasValidData() || HasAnimRootMotion() || DeltaTime < MIN_TICK_TIME || (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy && !bWasSimulatingRootMotion))
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

	// Apply friction
	if (bIsGroundMove || CVarAlwaysApplyFriction->GetBool())
	{
		const bool bVelocityOverMax = IsExceedingMaxSpeed(MaxSpeed);
		const FVector OldVelocity = Velocity;

		float ActualBrakingFriction = (bUseSeparateBrakingFriction ? BrakingFriction : Friction) * SurfaceFriction;

		if (bIsGroundMove && EdgeFrictionMultiplier != 1.0f)
		{
			bool bDoEdgeFriction = false;
			if (!bEdgeFrictionOnlyWhenBraking)
			{
				bDoEdgeFriction = true;
			}
			else if (bEdgeFrictionAlwaysWhenCrouching && IsCrouching())
			{
				bDoEdgeFriction = true;
			}
			else if (bZeroAcceleration)
			{
				bDoEdgeFriction = true;
			}
			if (bDoEdgeFriction)
			{
				FHitResult Hit(ForceInit);
				TraceLineToFloor(Hit);
				if (!Hit.bBlockingHit)
				{
					ActualBrakingFriction *= EdgeFrictionMultiplier;
				}
			}
		}

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
	Velocity.X = FMath::Clamp(Velocity.X, -AxisSpeedLimit, AxisSpeedLimit);
	Velocity.Y = FMath::Clamp(Velocity.Y, -AxisSpeedLimit, AxisSpeedLimit);

	// no clip
	if (bCheatFlying)
	{
		StopCrouchSliding();
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
			auto NoClipAccelClamp = PBPlayerCharacter->IsSprinting() ? 2.0f * MaxAcceleration : MaxAcceleration;
			Velocity = (Dir * LookVec * PerpendicularAccel.Size2D() + TangentialAccel).GetClampedToSize(NoClipAccelClamp, NoClipAccelClamp);
		}
	}
	// ladder movement
	else if (IsOnLadder())
	{
		StopCrouchSliding();

		// instantly brake when you're on a ladder
		Velocity = FVector::ZeroVector;

		// only set the velocity if the player is moving
		if (!bZeroAcceleration)
		{
			// Handle ladder movement here
		}
	}
	// crouch slide on ground
	else if (ShouldCrouchSlide())
	{
		const FVector FloorNormal = CurrentFloor.HitResult.ImpactNormal;
		// Direction of our crouch slide
		const FVector CrouchSlideInput = GetOwner()->GetActorForwardVector();
		float CurrentTime = GetWorld()->GetTimeSeconds();
		float TimeDifference = CurrentTime - CrouchSlideStartTime;
		// Decay velocity boosting within acceleration over time
		FVector WishAccel = CrouchSlideInput * Velocity.Size2D() * FMath::Lerp(MaxCrouchSlideVelocityBoost, MinCrouchSlideVelocityBoost, FMath::Clamp(TimeDifference / CrouchSlideBoostTime, 0.0f, 1.0f));
		float Slope = (CrouchSlideInput | FloorNormal);
		// Handle slope (decay more on uphill, boost on downhill)
		WishAccel *= 1.0f + Slope;
		Velocity += WishAccel * DeltaTime;
		// Stop crouch sliding
		if (Velocity.IsNearlyZero())
		{
			StopCrouchSliding();
		}
	}
	// walk move
	else
	{
		if (IsMovingOnGround())
		{
			StopCrouchSliding();
		}
		// Apply input acceleration
		if (!bZeroAcceleration)
		{
			// Clamp acceleration to max speed
			const FVector WishAccel = Acceleration.GetClampedToMaxSize2D(MaxSpeed);
			// Find veer
			const FVector AccelDir = WishAccel.GetSafeNormal2D();
			const float Veer = Velocity.X * AccelDir.X + Velocity.Y * AccelDir.Y;
			// Get add speed with an air speed cap, depending on if we're sliding in air or not
			// note: we use b WAS SlidingInAir since we only can categorize our movement after a velocity step, therefore we have to use the slide state from the previous frame while computing velocity
			float SpeedCap = 0.0f;
			if (!bIsGroundMove)
			{
				// use original air speed cap for strafing during a slide, for surfing
				float ForwardAccel = AccelDir | GetOwner()->GetActorForwardVector();
				if (bWasSlidingInAir && FMath::IsNearlyZero(ForwardAccel))
				{
					SpeedCap = AirSlideSpeedCap;
				}
				else
				{
					SpeedCap = AirSpeedCap;
				}
			}
			const float AddSpeed = (bIsGroundMove ? WishAccel : WishAccel.GetClampedToMaxSize2D(SpeedCap)).Size2D() - Veer;
			if (AddSpeed > 0.0f)
			{
				// Apply acceleration
				const float AccelerationMultiplier = bIsGroundMove ? GroundAccelerationMultiplier : AirAccelerationMultiplier;
				FVector CurrentAcceleration = WishAccel * AccelerationMultiplier * SurfaceFriction * DeltaTime;
				CurrentAcceleration = CurrentAcceleration.GetClampedToMaxSize2D(AddSpeed);
				Velocity += CurrentAcceleration;
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
	}

	// Limit after
	Velocity.X = FMath::Clamp(Velocity.X, -AxisSpeedLimit, AxisSpeedLimit);
	Velocity.Y = FMath::Clamp(Velocity.Y, -AxisSpeedLimit, AxisSpeedLimit);

	const float SpeedSq = Velocity.SizeSquared2D();

	// Dynamic step height code for allowing sliding on a slope when at a high speed
	if (IsOnLadder() || SpeedSq <= MaxWalkSpeedCrouched * MaxWalkSpeedCrouched)
	{
		// If we're crouching or not sliding, just use max
		MaxStepHeight = DefaultStepHeight;
		if (GetWalkableFloorZ() != DefaultWalkableFloorZ)
		{
			SetWalkableFloorZ(DefaultWalkableFloorZ);
		}
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
		const float NewWalkableFloorZ = FMath::Lerp(DefaultWalkableFloorZ, 0.9848f, SpeedMultiplier);
		if (GetWalkableFloorZ() != NewWalkableFloorZ)
		{
			SetWalkableFloorZ(NewWalkableFloorZ);
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

void UPBPlayerMovement::Crouch(bool bClientSimulation)
{
	// TODO: replicate to the client simulation that we are in a crouch transition so they can do the resize too.
	if (bClientSimulation)
	{
		Super::Crouch(true);
		return;
	}
	// Check if we're moving forward fast enough
	// don't init crouch sliding twice
	if (bShouldCrouchSlide)
	{
		if ((Velocity | GetOwner()->GetActorForwardVector()) >= SprintSpeed * CrouchSlideSpeedRequirementMultiplier && !bCrouchSliding)
		{
			// if we have input on ground
			if (!Acceleration.IsNearlyZero() && IsMovingOnGround())
			{
				StartCrouchSlide();
			}
			// if we are falling down (to prevent crouch jump slides)
			else if (IsFalling() && Velocity.Z < 0.0f)
			{
				// if we are in the air, falling down, defer crouch slide
				bDeferCrouchSlideToLand = true;
			}
		}
	}
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
	const float ComponentScale = CharacterCapsule->GetShapeScale();
	const float OldUnscaledHalfHeight = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float OldUnscaledRadius = CharacterCapsule->GetUnscaledCapsuleRadius();
	const float FullCrouchDiff = OldUnscaledHalfHeight - GetCrouchedHalfHeight();
	const float CurrentUnscaledHalfHeight = CharacterCapsule->GetUnscaledCapsuleHalfHeight();
	// Determine the crouching progress
	const bool bInstantCrouch = FMath::IsNearlyZero(TargetTime);
	const float CurrentAlpha = 1.0f - (CurrentUnscaledHalfHeight - GetCrouchedHalfHeight()) / FullCrouchDiff;
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
	}

	bForceNextFloorCheck = true;

	const float MeshAdjust = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - ClampedCrouchedHalfHeight;
	AdjustProxyCapsuleSize();
	CharacterOwner->OnStartCrouch(MeshAdjust, MeshAdjust * ComponentScale);

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
	// TODO: replicate to the client simulation that we are in a crouch transition so they can do the resize too.
	if (bClientSimulation)
	{
		Super::UnCrouch(true);
		return;
	}
	bIsInCrouchTransition = true;
	StopCrouchSliding();
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
	const bool bInstantCrouch = FMath::IsNearlyZero(TargetTime);
	float CurrentAlpha = 1.0f - (UncrouchedHeight - OldUnscaledHalfHeight) / FullCrouchDiff;
	float TargetAlphaDiff = 1.0f;
	float TargetAlpha = 1.0f;
	const UWorld* MyWorld = GetWorld();
	const FVector PawnLocation = UpdatedComponent->GetComponentLocation();
	if (!bInstantCrouch)
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
		StopCrouchSliding();
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
					// const FVector Down = FVector(0.0f, 0.0f, -TraceDist);

					FHitResult Hit(1.0f);
					const FCollisionShape ShortCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, ShrinkHalfHeight);
					// const bool bBlockingHit = MyWorld->SweepSingleByChannel(Hit, PawnLocation, PawnLocation + Down, FQuat::Identity, CollisionChannel,
					// ShortCapsuleShape, CapsuleParams);

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

	// Now call SetCapsuleSize() to cause touch/untouch events and actually grow the capsule
	CharacterCapsule->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), OldUnscaledHalfHeight + HalfHeightAdjust, true);

	// OnEndCrouch takes the change from the Default size, not the current one (though they are usually the same).
	const float MeshAdjust = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - OldUnscaledHalfHeight + HalfHeightAdjust;
	AdjustProxyCapsuleSize();
	CharacterOwner->OnEndCrouch(MeshAdjust, MeshAdjust * ComponentScale);
	bCrouchFrameTolerated = false;

	// Don't smooth this change in mesh position
	if ((bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) || (IsNetMode(NM_ListenServer) && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy))
	{
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		if (ClientData)
		{
			ClientData->MeshTranslationOffset += FVector(0.0f, 0.0f, ScaledHalfHeightAdjust);
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}
}

bool UPBPlayerMovement::MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit, ETeleportType Teleport)
{
	FVector NewDelta = Delta;

	// Start from the capsule location pre-move
	FVector Loc = UpdatedComponent->GetComponentLocation();

	bool bResult = Super::MoveUpdatedComponentImpl(NewDelta, NewRotation, bSweep, OutHit, Teleport);
	
	if (bSweep && Teleport == ETeleportType::None && Delta != FVector::ZeroVector && IsFalling() && FMath::Abs(Delta.Z) > 0.0f)
	{
		const float HorizontalMovement = Delta.SizeSquared2D();
		if (HorizontalMovement > UE_KINDA_SMALL_NUMBER)
		{
			bool bBlockingHit;

			// Test with a box that is enclosed by the capsule.
			float PawnRadius, PawnHalfHeight;
			CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);
			// Scale by diagonal
			PawnRadius *= 0.707f;
			// Shrink our height so we don't intersect any current floor
			PawnHalfHeight -= SWEEP_EDGE_REJECT_DISTANCE;
			const FCollisionShape BoxShape = FCollisionShape::MakeBox(FVector(PawnRadius, PawnRadius, PawnHalfHeight));

			FVector Start = Loc;
			// this is solely a horizontal movement check, so assume we've already moved the Z delta.
			Start.Z += Delta.Z;

			FVector DeltaDir = Delta;
			DeltaDir.Z = 0.0f;
			FVector End = Start + DeltaDir;
			
			const ECollisionChannel TraceChannel = UpdatedComponent->GetCollisionObjectType();
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CapsuleHemisphereTrace), false, CharacterOwner);
			FCollisionResponseParams ResponseParam;
			InitCollisionParams(Params, ResponseParam);
			
			FHitResult Hit(1.f);

			//DrawDebugBox(GetWorld(), End, FVector(PawnRadius, PawnRadius, PawnHalfHeight), FQuat(RotateGravityToWorld(FVector(0.f, 0.f, -1.f)), UE_PI * 0.25f), FColor::Red, false, 10.0f, 0, 0.5f);

			// First test with the box rotated so the corners are along the major axes (ie rotated 45 degrees).
			bBlockingHit = GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat(RotateGravityToWorld(FVector(0.f, 0.f, -1.f)), UE_PI * 0.25f), TraceChannel, BoxShape, Params, ResponseParam);

			if (!bBlockingHit)
			{
				// Test again with the same box, not rotated.
				Hit.Reset(1.f, false);
				//DrawDebugBox(GetWorld(), End, FVector(PawnRadius, PawnRadius, PawnHalfHeight), GetWorldToGravityTransform(), FColor::Red, false, 10.0f, 0, 0.5f);
				bBlockingHit = GetWorld()->SweepSingleByChannel(Hit, Start, End, GetWorldToGravityTransform(), TraceChannel, BoxShape, Params, ResponseParam);
			}
			
			// if we hit a wall on the side of the box (not the edge or bottom), then we have to slide since this isn't a valid move for a flat base.
			if (bBlockingHit && !Hit.bStartPenetrating && FMath::Abs(Hit.ImpactNormal.Z) <= VERTICAL_SLOPE_NORMAL_Z)
			{
				//DrawDebugLine(GetWorld(), Start, End, FColor::Blue, false, 10.0f, 0, 0.5f);
				//UE_LOG(LogTemp, Log, TEXT("sliding on z: %f"), Hit.ImpactNormal.Z);
				// Blocked horizontally by box, compute new trajectory
				NewDelta = UMovementComponent::ComputeSlideVector(Delta, 1.0f, Hit.ImpactNormal, Hit);
				// override capsule hit with box hit
				// TODO: should we override some hit properties with the slide vector?
				if (OutHit)
				{
					*OutHit = Hit;
				}
				// reverse the move
				FHitResult DiscardHit;
				Super::MoveUpdatedComponentImpl(NewDelta - Delta, NewRotation, bSweep, &DiscardHit, Teleport);

				//DrawDebugLine(GetWorld(), Start, Start + NewDelta, FColor::Green, false, 10.0f, 0, 0.5f);
			}
		}
	}

	return bResult;
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
		bCanAttemptJump &= IsOnLadder();
	}
	return bCanAttemptJump;
}

float UPBPlayerMovement::GetMaxSpeed() const
{
	if (MovementMode != MOVE_Walking && MovementMode != MOVE_NavWalking && MovementMode != MOVE_Falling && MovementMode != MOVE_Flying)
	{
		return Super::GetMaxSpeed();
	}

	if (MovementMode == MOVE_Flying && !IsOnLadder() && !bCheatFlying)
	{
		return Super::GetMaxSpeed();
	}

	if (bCheatFlying)
	{
		return (PBPlayerCharacter->IsSprinting() ? SprintSpeed : WalkSpeed) * 1.5f;
	}
	// No suit can only crouch and walk.
	if (!PBPlayerCharacter->IsSuitEquipped())
	{
		if (IsCrouching() && bCrouchFrameTolerated)
		{
			return MaxWalkSpeedCrouched;
		}
		return WalkSpeed;
	}
	float Speed;
	if (ShouldCrouchSlide())
	{
		Speed = MinCrouchSlideBoost * MaxCrouchSlideVelocityBoost;
	}
	else if (IsCrouching() && bCrouchFrameTolerated)
	{
		Speed = MaxWalkSpeedCrouched;
	}
	else if (PBPlayerCharacter->IsSprinting())
	{
		Speed = SprintSpeed;
	}
	else if (PBPlayerCharacter->DoesWantToWalk())
	{
		Speed = WalkSpeed;
	}
	else
	{
		Speed = RunSpeed;
	}

	return Speed;
}

bool IsSmallBody(const FBodyInstance* Body, float SizeThreshold, float MassThreshold)
{
	if (!Body)
	{
		return false;
	}

	// if small mass or small size, the body is considered small

	if (Body->GetBodyMass() < MassThreshold)
	{
		return true;
	}

	const FVector Bounds = Body->GetBodyBounds().GetExtent();
	return Bounds.SizeSquared() < SizeThreshold * SizeThreshold;
}

void UPBPlayerMovement::ApplyDownwardForce(float DeltaSeconds)
{
	if (!CurrentFloor.HitResult.IsValidBlockingHit() || StandingDownwardForceScale == 0.0f)
	{
		return;
	}

	UPrimitiveComponent* BaseComp = CurrentFloor.HitResult.GetComponent();
	if (!BaseComp || BaseComp->Mobility != EComponentMobility::Movable)
	{
		return;
	}

	FBodyInstance* BI = BaseComp->GetBodyInstance(CurrentFloor.HitResult.BoneName);
	if (BI && BI->IsInstanceSimulatingPhysics() && !IsSmallBody(BI, 64.0f, 15.0f))
	{
		const FVector Gravity = -GetGravityDirection() * GetGravityZ();

		if (!Gravity.IsZero())
		{
			BI->AddForceAtPosition(Gravity * Mass * StandingDownwardForceScale, CurrentFloor.HitResult.ImpactPoint);
		}
	}
}

UPBMoveStepSound* UPBPlayerMovement::GetMoveStepSoundBySurface(EPhysicalSurface SurfaceType) const
{
	TSubclassOf<UPBMoveStepSound>* GotSound = GetPBCharacter()->GetMoveStepSound(TEnumAsByte<EPhysicalSurface>(SurfaceType));

	if (GotSound)
	{
		return GotSound->GetDefaultObject();
	}

	return nullptr;
}
