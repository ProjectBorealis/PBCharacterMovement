// Copyright 2017-2019 Project Borealis

#include "Character/PBPlayerMovement.h"

#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
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
	// Braking deceleration (sv_stopspeed)
	FallingLateralFriction = 0.0f;
	BrakingDecelerationFalling = 0.0f;
	BrakingDecelerationFlying = 190.5f;
	BrakingDecelerationSwimming = 190.5f;
	BrakingDecelerationWalking = 190.5f;
	// HL2 step height
	MaxStepHeight = 34.29f;
	// Step height scaling due to speed
	MinStepHeight = 7.5f;
	// Jump z from HL2's 160Hu
	// 21Hu jump height
	// 510ms jump time
	JumpZVelocity = 304.8f;
	// Always have the same jump
	JumpOffJumpZFactor = 1.0f;
	// Default show pos to false
	bShowPos = false;
	// Speed multiplier bounds
	SpeedMultMin = SprintSpeed * 1.7f;
	SpeedMultMax = SprintSpeed * 2.5f;
	// Start out braking
	bBrakingFrameTolerated = true;
	// Crouching
	CrouchedHalfHeight = 34.29f;
	MaxWalkSpeedCrouched = 120.65f;
	bCanWalkOffLedgesWhenCrouching = true;
	CrouchTime = MOVEMENT_DEFAULT_CROUCHTIME;
	UncrouchTime = MOVEMENT_DEFAULT_UNCROUCHTIME;
	CrouchJumpTime = MOVEMENT_DEFAULT_CROUCHJUMPTIME;
	UncrouchJumpTime = MOVEMENT_DEFAULT_UNCROUCHJUMPTIME;
	// Noclip
	NoClipVerticalMoveMode = 0;
	// Slope angle is 45.57 degrees
	SetWalkableFloorZ(0.7f);
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
	Mass = 65.77f; // Gordon is 145lbs
	// Don't smooth rotation at all
	bUseControllerDesiredRotation = false;
	// Flat base
	bUseFlatBaseForFloorChecks = true;
	// Agent props
	NavAgentProps.bCanCrouch = true;
	NavAgentProps.bCanFly = true;
	PBCharacter = Cast<APBPlayerCharacter>(GetOwner());
}

void UPBPlayerMovement::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	bAppliedFriction = false;

	// TODO(mastercoms): HACK: double friction in order to account for insufficient braking on substepping
	if (DeltaTime > MaxSimulationTimeStep)
	{
		BrakingFrictionFactor = 2.0f;
	}
	else
	{
		BrakingFrictionFactor = 1.0f;
	}
	
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

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
	if (bInCrouch && !bCheatFlying && !bOnLadder)
	{
		// Crouch
		if (!bWantsToCrouch)
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

	bBrakingFrameTolerated = IsMovingOnGround();
}

bool UPBPlayerMovement::DoJump(bool bClientSimulation)
{
	return bCheatFlying || Super::DoJump(bClientSimulation);
}

#if MID_AIR_STEP
void UPBPlayerMovement::PhysFalling(float deltaTime, int32 Iterations)
{
	SCOPE_CYCLE_COUNTER(STAT_CharPhysFalling);

	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	FVector FallAcceleration = GetFallingLateralAcceleration(deltaTime);
	FallAcceleration.Z = 0.f;
	const bool bHasAirControl = (FallAcceleration.SizeSquared2D() > 0.f);

	float remainingTime = deltaTime;
	while ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations))
	{
		Iterations++;
		const float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;

		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		const FQuat PawnRotation = UpdatedComponent->GetComponentQuat();
		bJustTeleported = false;

		RestorePreAdditiveRootMotionVelocity();

		FVector OldVelocity = Velocity;
		FVector VelocityNoAirControl = Velocity;

		// Apply input
		if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			const float MaxDecel = GetMaxBrakingDeceleration();
			// Compute VelocityNoAirControl
			if (bHasAirControl)
			{
				// Find velocity *without* acceleration.
				TGuardValue<FVector> RestoreAcceleration(Acceleration, FVector::ZeroVector);
				TGuardValue<FVector> RestoreVelocity(Velocity, Velocity);
				Velocity.Z = 0.f;
				CalcVelocity(timeTick, FallingLateralFriction, false, MaxDecel);
				VelocityNoAirControl = FVector(Velocity.X, Velocity.Y, OldVelocity.Z);
			}

			// Compute Velocity
			{
				// Acceleration = FallAcceleration for CalcVelocity(), but we restore it after using it.
				TGuardValue<FVector> RestoreAcceleration(Acceleration, FallAcceleration);
				Velocity.Z = 0.f;
				CalcVelocity(timeTick, FallingLateralFriction, false, MaxDecel);
				Velocity.Z = OldVelocity.Z;
			}

			// Just copy Velocity to VelocityNoAirControl if they are the same (ie no acceleration).
			if (!bHasAirControl)
			{
				VelocityNoAirControl = Velocity;
			}
		}

		// Apply gravity
		const FVector Gravity(0.f, 0.f, GetGravityZ());
		float GravityTime = timeTick;

		// If jump is providing force, gravity may be affected.
		if (CharacterOwner->JumpForceTimeRemaining > 0.0f)
		{
			// Consume some of the force time. Only the remaining time (if any) is affected by gravity when bApplyGravityWhileJumping=false.
			const float JumpForceTime = FMath::Min(CharacterOwner->JumpForceTimeRemaining, timeTick);
			GravityTime = bApplyGravityWhileJumping ? timeTick : FMath::Max(0.0f, timeTick - JumpForceTime);

			// Update Character state
			CharacterOwner->JumpForceTimeRemaining -= JumpForceTime;
			if (CharacterOwner->JumpForceTimeRemaining <= 0.0f)
			{
				CharacterOwner->ResetJumpState();
			}
		}

		Velocity = NewFallVelocity(Velocity, Gravity, GravityTime);
		VelocityNoAirControl = bHasAirControl ? NewFallVelocity(VelocityNoAirControl, Gravity, GravityTime) : Velocity;
		const FVector AirControlAccel = (Velocity - VelocityNoAirControl) / timeTick;

		ApplyRootMotionToVelocity(timeTick);

		if (bNotifyApex && (Velocity.Z <= 0.f))
		{
			// Just passed jump apex since now going down
			bNotifyApex = false;
			NotifyJumpApex();
		}

		// Move
		FHitResult Hit(1.f);
		FVector Adjusted = 0.5f * (OldVelocity + Velocity) * timeTick;
		SafeMoveUpdatedComponent(Adjusted, PawnRotation, true, Hit);

		if (!HasValidData())
		{
			return;
		}

		float LastMoveTimeSlice = timeTick;
		float subTimeTickRemaining = timeTick * (1.f - Hit.Time);

		if (IsSwimming()) // just entered water
		{
			remainingTime += subTimeTickRemaining;
			StartSwimming(OldLocation, OldVelocity, timeTick, remainingTime, Iterations);
			return;
		}
		else if (Hit.bBlockingHit)
		{
			if (IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit))
			{
				remainingTime += subTimeTickRemaining;
				ProcessLanded(Hit, remainingTime, Iterations);
				return;
			}
			else
			{
				// Compute impact deflection based on final velocity, not integration step.
				// This allows us to compute a new velocity from the deflected vector, and ensures the full gravity effect is included in the slide result.
				Adjusted = Velocity * timeTick;

				// See if we can convert a normally invalid landing spot (based on the hit result) to a usable one.
				if (!Hit.bStartPenetrating)
				{
					// hit a barrier, try to step up
					const FVector GravDir(0.f, 0.f, -1.f);
					bool bSteppedUp = StepUp(GravDir, FVector(Velocity.X, Velocity.Y, 0.f) * subTimeTickRemaining, Hit);

					if (bSteppedUp)
					{
						bool Landed = true;
						if (Velocity.Z > 0.f)
						{
							const float DoubleGravity = -2.f * GetGravityZ();
							const float RemainingHeight = Velocity.Z * Velocity.Z / DoubleGravity - (UpdatedComponent->GetComponentLocation().Z - OldLocation.Z);
							if (RemainingHeight > 0.f)
							{
								Velocity.Z = FMath::Sqrt(RemainingHeight * DoubleGravity);
								Landed = false;
							}
						}

						if (Landed)
						{
							ProcessLanded(Hit, remainingTime, Iterations);
							return;
						}

						continue;
					}

					if (ShouldCheckForValidLandingSpot(timeTick, Adjusted, Hit))
					{
						const FVector PawnLocation = UpdatedComponent->GetComponentLocation();
						FFindFloorResult FloorResult;
						FindFloor(PawnLocation, FloorResult, false);
						if (FloorResult.IsWalkableFloor() && IsValidLandingSpot(PawnLocation, FloorResult.HitResult))
						{
							remainingTime += subTimeTickRemaining;
							ProcessLanded(FloorResult.HitResult, remainingTime, Iterations);
							return;
						}
					}
				}

				HandleImpact(Hit, LastMoveTimeSlice, Adjusted);

				// If we've changed physics mode, abort.
				if (!HasValidData() || !IsFalling())
				{
					return;
				}

				// Limit air control based on what we hit.
				// We moved to the impact point using air control, but may want to deflect from there based on a limited air control acceleration.
				if (bHasAirControl)
				{
					const bool bCheckLandingSpot = false; // we already checked above.
					const FVector AirControlDeltaV = LimitAirControl(LastMoveTimeSlice, AirControlAccel, Hit, bCheckLandingSpot) * LastMoveTimeSlice;
					Adjusted = (VelocityNoAirControl + AirControlDeltaV) * LastMoveTimeSlice;
				}

				const FVector OldHitNormal = Hit.Normal;
				const FVector OldHitImpactNormal = Hit.ImpactNormal;
				FVector Delta = ComputeSlideVector(Adjusted, 1.f - Hit.Time, OldHitNormal, Hit);

				// Compute velocity after deflection (only gravity component for RootMotion)
				if (subTimeTickRemaining > KINDA_SMALL_NUMBER && !bJustTeleported)
				{
					const FVector NewVelocity = (Delta / subTimeTickRemaining);
					Velocity = HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() ? FVector(Velocity.X, Velocity.Y, NewVelocity.Z) : NewVelocity;
				}

				if (subTimeTickRemaining > KINDA_SMALL_NUMBER && (Delta | Adjusted) > 0.f)
				{
					// Move in deflected direction.
					SafeMoveUpdatedComponent(Delta, PawnRotation, true, Hit);

					if (Hit.bBlockingHit)
					{
						// hit second wall
						LastMoveTimeSlice = subTimeTickRemaining;
						subTimeTickRemaining = subTimeTickRemaining * (1.f - Hit.Time);

						if (IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit))
						{
							remainingTime += subTimeTickRemaining;
							ProcessLanded(Hit, remainingTime, Iterations);
							return;
						}

						HandleImpact(Hit, LastMoveTimeSlice, Delta);

						// If we've changed physics mode, abort.
						if (!HasValidData() || !IsFalling())
						{
							return;
						}

						// Act as if there was no air control on the last move when computing new deflection.
						if (bHasAirControl && Hit.Normal.Z > VERTICAL_SLOPE_NORMAL_Z)
						{
							const FVector LastMoveNoAirControl = VelocityNoAirControl * LastMoveTimeSlice;
							Delta = ComputeSlideVector(LastMoveNoAirControl, 1.f, OldHitNormal, Hit);
						}

						FVector PreTwoWallDelta = Delta;
						TwoWallAdjust(Delta, Hit, OldHitNormal);

						// Limit air control, but allow a slide along the second wall.
						if (bHasAirControl)
						{
							const bool bCheckLandingSpot = false; // we already checked above.
							const FVector AirControlDeltaV = LimitAirControl(subTimeTickRemaining, AirControlAccel, Hit, bCheckLandingSpot) * subTimeTickRemaining;

							// Only allow if not back in to first wall
							if (FVector::DotProduct(AirControlDeltaV, OldHitNormal) > 0.f)
							{
								Delta += (AirControlDeltaV * subTimeTickRemaining);
							}
						}

						// Compute velocity after deflection (only gravity component for RootMotion)
						if (subTimeTickRemaining > KINDA_SMALL_NUMBER && !bJustTeleported)
						{
							const FVector NewVelocity = (Delta / subTimeTickRemaining);
							Velocity = HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() ? FVector(Velocity.X, Velocity.Y, NewVelocity.Z) : NewVelocity;
						}

						// bDitch=true means that pawn is straddling two slopes, neither of which he can stand on
						bool bDitch = ((OldHitImpactNormal.Z > 0.f) && (Hit.ImpactNormal.Z > 0.f) && (FMath::Abs(Delta.Z) <= KINDA_SMALL_NUMBER) &&
									   ((Hit.ImpactNormal | OldHitImpactNormal) < 0.f));
						SafeMoveUpdatedComponent(Delta, PawnRotation, true, Hit);
						if (Hit.Time == 0.f)
						{
							// if we are stuck then try to side step
							FVector SideDelta = (OldHitNormal + Hit.ImpactNormal).GetSafeNormal2D();
							if (SideDelta.IsNearlyZero())
							{
								SideDelta = FVector(OldHitNormal.Y, -OldHitNormal.X, 0).GetSafeNormal();
							}
							SafeMoveUpdatedComponent(SideDelta, PawnRotation, true, Hit);
						}

						if (bDitch || IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit) || FMath::IsNearlyZero(Hit.Time))
						{
							remainingTime = 0.f;
							ProcessLanded(Hit, remainingTime, Iterations);
							return;
						}
						else if (GetPerchRadiusThreshold() > 0.f && FMath::IsNearlyEqual(Hit.Time, 1.0f) && OldHitImpactNormal.Z >= GetWalkableFloorZ())
						{
							// We might be in a virtual 'ditch' within our perch radius. This is rare.
							const FVector PawnLocation = UpdatedComponent->GetComponentLocation();
							const float ZMovedDist = FMath::Abs(PawnLocation.Z - OldLocation.Z);
							const float MovedDist2DSq = (PawnLocation - OldLocation).SizeSquared2D();
							if (ZMovedDist <= 0.2f * timeTick && MovedDist2DSq <= 4.f * timeTick)
							{
								Velocity.X += 0.25f * GetMaxSpeed() * (FMath::FRand() - 0.5f);
								Velocity.Y += 0.25f * GetMaxSpeed() * (FMath::FRand() - 0.5f);
								Velocity.Z = FMath::Max<float>(JumpZVelocity * 0.25f, 1.f);
								Delta = Velocity * timeTick;
								SafeMoveUpdatedComponent(Delta, PawnRotation, true, Hit);
							}
						}
					}
				}
			}
		}

		if (Velocity.SizeSquared2D() <= KINDA_SMALL_NUMBER * 10.f)
		{
			Velocity.X = 0.f;
			Velocity.Y = 0.f;
		}
	}
}

bool UPBPlayerMovement::CanStepUp(const FHitResult& Hit) const
{
	if (!Hit.IsValidBlockingHit() || !HasValidData())
	{
		return false;
	}

	if (IsFalling())
	{
		FHitResult HitResult = FHitResult(ForceInit);
		FVector Start = GetCharacterOwner()->GetCapsuleComponent()->GetComponentLocation() -
						FVector(0.0f, 0.0f, GetCharacterOwner()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		float FloorSweepTraceDist = MaxStepHeight / 2.0f + MAX_FLOOR_DIST + KINDA_SMALL_NUMBER;
		FVector End = Start - FVector(0.0f, 0.0f, FloorSweepTraceDist);
		GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECollisionChannel::ECC_WorldStatic,
											 FCollisionQueryParams(FName(TEXT("FallingStepTrace")), true, GetCharacterOwner()));
		if (!HitResult.bBlockingHit)
		{
			return false;
		}
		if ((HitResult.ImpactNormal | FVector(0.0f, 0.0f, 1.0f)) < GetWalkableFloorZ())
		{
			return false;
		}
	}

	// No component for "fake" hits when we are on a known good base.
	const UPrimitiveComponent* HitComponent = Hit.Component.Get();
	if (!HitComponent)
	{
		return true;
	}

	if (!HitComponent->CanCharacterStepUp(CharacterOwner))
	{
		return false;
	}

	// No actor for "fake" hits when we are on a known good base.
	const AActor* HitActor = Hit.GetActor();
	if (!HitActor)
	{
		return true;
	}

	if (!HitActor->CanBeBaseForCharacter(CharacterOwner))
	{
		return false;
	}

	return true;
}

bool UPBPlayerMovement::StepUp(const FVector& GravDir, const FVector& Delta, const FHitResult& InHit, FStepDownResult* OutStepDownResult)
{
	SCOPE_CYCLE_COUNTER(STAT_CharStepUp);

	if (!CanStepUp(InHit) || MaxStepHeight <= 0.f)
	{
		return false;
	}

	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	float PawnRadius, PawnHalfHeight;
	CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);

	// Don't bother stepping up if top of capsule is hitting something.
	const float InitialImpactZ = InHit.ImpactPoint.Z;
	if (InitialImpactZ > OldLocation.Z + (PawnHalfHeight - PawnRadius))
	{
		return false;
	}

	if (GravDir.IsZero())
	{
		return false;
	}

	// Gravity should be a normalized direction
	ensure(GravDir.IsNormalized());

	float StepTravelUpHeight = IsFalling() ? MaxStepHeight / 3.0f : MaxStepHeight;
	float StepTravelDownHeight = StepTravelUpHeight;
	const float StepSideZ = -1.f * FVector::DotProduct(InHit.ImpactNormal, GravDir);
	float PawnInitialFloorBaseZ = OldLocation.Z - PawnHalfHeight;
	float PawnFloorPointZ = PawnInitialFloorBaseZ;

	if (IsMovingOnGround() && CurrentFloor.IsWalkableFloor())
	{
		// Since we float a variable amount off the floor, we need to enforce max step height off the actual point of impact with the floor.
		const float FloorDist = FMath::Max(0.f, CurrentFloor.GetDistanceToFloor());
		PawnInitialFloorBaseZ -= FloorDist;
		StepTravelUpHeight = FMath::Max(StepTravelUpHeight - FloorDist, 0.f);
		StepTravelDownHeight = (MaxStepHeight + MAX_FLOOR_DIST * 2.f);

		const bool bHitVerticalFace = !IsWithinEdgeTolerance(InHit.Location, InHit.ImpactPoint, PawnRadius);
		if (!CurrentFloor.bLineTrace && !bHitVerticalFace)
		{
			PawnFloorPointZ = CurrentFloor.HitResult.ImpactPoint.Z;
		}
		else
		{
			// Base floor point is the base of the capsule moved down by how far we are hovering over the surface we are hitting.
			PawnFloorPointZ -= CurrentFloor.FloorDist;
		}
	}

	// Don't step up if the impact is below us, accounting for distance from floor.
	if (InitialImpactZ <= PawnInitialFloorBaseZ)
	{
		return false;
	}

	// Scope our movement updates, and do not apply them until all intermediate moves are completed.
	FScopedMovementUpdate ScopedStepUpMovement(UpdatedComponent, EScopedUpdate::DeferredUpdates);

	// step up - treat as vertical wall
	FHitResult SweepUpHit(1.f);
	const FQuat PawnRotation = UpdatedComponent->GetComponentQuat();
	MoveUpdatedComponent(-GravDir * StepTravelUpHeight, PawnRotation, true, &SweepUpHit);

	if (SweepUpHit.bStartPenetrating)
	{
		// Undo movement
		ScopedStepUpMovement.RevertMove();
		return false;
	}

	// step fwd
	FHitResult Hit(1.f);
	MoveUpdatedComponent(Delta, PawnRotation, true, &Hit);

	// Check result of forward movement
	if (Hit.bBlockingHit)
	{
		if (Hit.bStartPenetrating)
		{
			// Undo movement
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// If we hit something above us and also something ahead of us, we should notify about the upward hit as well.
		// The forward hit will be handled later (in the bSteppedOver case below).
		// In the case of hitting something above but not forward, we are not blocked from moving so we don't need the notification.
		if (SweepUpHit.bBlockingHit)
		{
			HandleImpact(SweepUpHit);
		}

		// pawn ran into a wall
		HandleImpact(Hit);
		if (IsFalling())
		{
			return true;
		}

		// adjust and try again
		const float ForwardHitTime = Hit.Time;
		const float ForwardSlideAmount = SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);

		if (IsFalling())
		{
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// If both the forward hit and the deflection got us nowhere, there is no point in this step up.
		if (FMath::IsNearlyZero(ForwardHitTime) && FMath::IsNearlyZero(ForwardSlideAmount))
		{
			ScopedStepUpMovement.RevertMove();
			return false;
		}
	}

	// Step down
	MoveUpdatedComponent(GravDir * StepTravelDownHeight, UpdatedComponent->GetComponentQuat(), true, &Hit);

	// If step down was initially penetrating abort the step up
	if (Hit.bStartPenetrating)
	{
		ScopedStepUpMovement.RevertMove();
		return false;
	}

	FStepDownResult StepDownResult;
	if (Hit.IsValidBlockingHit())
	{
		// See if this step sequence would have allowed us to travel higher than our max step height allows.
		const float DeltaZ = Hit.ImpactPoint.Z - PawnFloorPointZ;
		if (DeltaZ > MaxStepHeight)
		{
			// UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (too high Height %.3f) up from floor base %f to %f"), DeltaZ,
			// PawnInitialFloorBaseZ, NewLocation.Z);
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// Reject unwalkable surface normals here.
		if (!IsWalkable(Hit))
		{
			// Reject if normal opposes movement direction
			const bool bNormalTowardsMe = (Delta | Hit.ImpactNormal) < 0.f;
			if (bNormalTowardsMe)
			{
				// UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (unwalkable normal %s opposed to movement)"), *Hit.ImpactNormal.ToString());
				ScopedStepUpMovement.RevertMove();
				return false;
			}

			// Also reject if we would end up being higher than our starting location by stepping down.
			// It's fine to step down onto an unwalkable normal below us, we will just slide off. Rejecting those moves would prevent us from being able to walk
			// off the edge.
			if (Hit.Location.Z > OldLocation.Z)
			{
				// UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (unwalkable normal %s above old position)"), *Hit.ImpactNormal.ToString());
				ScopedStepUpMovement.RevertMove();
				return false;
			}
		}

		// Reject moves where the downward sweep hit something very close to the edge of the capsule. This maintains consistency with FindFloor as well.
		if (!IsWithinEdgeTolerance(Hit.Location, Hit.ImpactPoint, PawnRadius))
		{
			// UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (outside edge tolerance)"));
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// Don't step up onto invalid surfaces if traveling higher.
		if (DeltaZ > 0.f && !CanStepUp(Hit))
		{
			// UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (up onto surface with !CanStepUp())"));
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// See if we can validate the floor as a result of this step down. In almost all cases this should succeed, and we can avoid computing the floor outside
		// this method.
		if (OutStepDownResult != NULL)
		{
			FindFloor(UpdatedComponent->GetComponentLocation(), StepDownResult.FloorResult, false, &Hit);

			// Reject unwalkable normals if we end up higher than our initial height.
			// It's fine to walk down onto an unwalkable surface, don't reject those moves.
			if (Hit.Location.Z > OldLocation.Z)
			{
				// We should reject the floor result if we are trying to step up an actual step where we are not able to perch (this is rare).
				// In those cases we should instead abort the step up and try to slide along the stair.
				if (!StepDownResult.FloorResult.bBlockingHit && StepSideZ < MAX_STEP_SIDE_Z)
				{
					ScopedStepUpMovement.RevertMove();
					return false;
				}
			}

			StepDownResult.bComputedFloor = true;
		}
	}

	// Copy step down result.
	if (OutStepDownResult != NULL)
	{
		*OutStepDownResult = StepDownResult;
	}

	// Don't recalculate velocity based on this height adjustment, if considering vertical adjustments.
	bJustTeleported |= !bMaintainHorizontalGroundVelocity;

	return true;
}
#endif

void UPBPlayerMovement::TwoWallAdjust(FVector& OutDelta, const FHitResult& Hit, const FVector& OldHitNormal) const
{
	Super::Super::TwoWallAdjust(OutDelta, Hit, OldHitNormal);
}

float UPBPlayerMovement::SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, bool bHandleImpact)
{
	return Super::Super::SlideAlongSurface(Delta, Time, Normal, Hit, bHandleImpact);
}

FVector UPBPlayerMovement::HandleSlopeBoosting(const FVector& SlideResult, const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const
{
	return SlideResult;
}

bool UPBPlayerMovement::ShouldCatchAir(const FFindFloorResult& OldFloor, const FFindFloorResult& NewFloor)
{
	float SurfaceFriction = 1.0f;
	if (OldFloor.HitResult.PhysMaterial.IsValid())
	{
		UPhysicalMaterial* PhysMat = OldFloor.HitResult.PhysMaterial.Get();
		if (PhysMat)
		{
			SurfaceFriction = FMath::Min(1.0f, PhysMat->Friction * 1.25f);
		}
	}

	float Speed = Velocity.Size2D();
	float MaxSpeed = SprintSpeed * 1.5f;

	float SpeedMult = MaxSpeed / Speed;

	float ZDiff = NewFloor.HitResult.ImpactNormal.Z - OldFloor.HitResult.ImpactNormal.Z;

	if (ZDiff > 0.0f && SurfaceFriction * SpeedMult < 0.5f)
	{
		return true;
	}

	return Super::ShouldCatchAir(OldFloor, NewFloor);
}

void UPBPlayerMovement::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	// Reset step side if we are changing modes
	StepSide = false;
	
	FHitResult Hit;
	// did we jump or land
	bool bJumped = false;

	if (PreviousMovementMode == EMovementMode::MOVE_Walking && MovementMode == EMovementMode::MOVE_Falling)
	{
		// Hit = UPBUtil::TraceLineFullCharacter(CharacterOwner->GetCapsuleComponent(), GetWorld(), CharacterOwner);
		FCollisionQueryParams TraceParams(FName(TEXT("RV_Trace")), true, CharacterOwner);
		TraceParams.bTraceComplex = CharacterOwner->GetCapsuleComponent()->bTraceComplexOnMove;
		TraceParams.bReturnPhysicalMaterial = true;

		GetWorld()->SweepSingleByChannel(Hit, CharacterOwner->GetCapsuleComponent()->GetComponentLocation(),
									  CharacterOwner->GetCapsuleComponent()->GetComponentLocation() - FVector(0.0f, 0.0f, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.0f),
									  FQuat::Identity, ECC_Visibility,
									  FCollisionShape::MakeBox(FVector(CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius(), CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius(),
																	   CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 1.5f)),
									  TraceParams);
		bJumped = true;
	}
	if (PreviousMovementMode == EMovementMode::MOVE_Falling && MovementMode == EMovementMode::MOVE_Walking)
	{
		Hit = CurrentFloor.HitResult;
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
		if (!PBCharacter->GetMoveStepSound(TEnumAsByte<EPhysicalSurface>(EPhysicalSurface::SurfaceType_Default)))
		{
			return;
		}
		MoveSound = PBCharacter->GetMoveStepSound(TEnumAsByte<EPhysicalSurface>(EPhysicalSurface::SurfaceType_Default))->GetDefaultObject();
	}

	if (MoveSound)
	{
		float MoveSoundVolume = MoveSound->GetWalkVolume();

		if (IsCrouching())
		{
			MoveSoundVolume *= 0.65f;
		}

		TArray<USoundCue*> MoveSoundCues = bJumped ? MoveSound->GetJumpSounds() : MoveSound->GetLandSounds();

		if (MoveSoundCues.Num() < 1)
		{
			return;
		}

		USoundCue* Sound = MoveSoundCues[FMath::RandRange(0, MoveSoundCues.Num() - 1)];

		Sound->VolumeMultiplier = MoveSoundVolume;

		/*UPBGameplayStatics::PlaySound(Sound, GetCharacterOwner(),
									  // FVector(0.0f, 0.0f, -GetCharacterOwner()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),
									  EPBSoundCategory::Footstep);*/
		UGameplayStatics::SpawnSoundAttached(Sound, GetCharacterOwner()->GetRootComponent());
	}
}

void UPBPlayerMovement::ToggleNoClip()
{
	if (bCheatFlying)
	{
		SetMovementMode(MOVE_Walking);
		bCheatFlying = false;
		GetCharacterOwner()->SetActorEnableCollision(true);
	}
	else
	{
		SetMovementMode(MOVE_Flying);
		bCheatFlying = true;
		GetCharacterOwner()->SetActorEnableCollision(false);
	}
}

void UPBPlayerMovement::ApplyVelocityBraking(float DeltaTime, float Friction, float BrakingDeceleration)
{
	float Speed = Velocity.Size2D();
	if (Speed <= 0.1f || !HasValidData() || HasAnimRootMotion() || DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	const float FrictionFactor = FMath::Max(0.0f, BrakingFrictionFactor);
	Friction = FMath::Max(0.0f, Friction * FrictionFactor);
	BrakingDeceleration = FMath::Max(BrakingDeceleration, Speed);
	BrakingDeceleration = FMath::Max(0.0f, BrakingDeceleration);
	const bool bZeroFriction = FMath::IsNearlyZero(Friction);
	const bool bZeroBraking = BrakingDeceleration == 0.0f;

	if (bZeroFriction || bZeroBraking)
	{
		return;
	}

	const FVector OldVel = Velocity;

	// Decelerate to brake to a stop
	const FVector RevAccel = Friction * BrakingDeceleration * Velocity.GetSafeNormal();
	Velocity -= RevAccel * DeltaTime;

	// Don't reverse direction
	if ((Velocity | OldVel) <= 0.0f)
	{
		Velocity = FVector::ZeroVector;
		return;
	}

	// Clamp to zero if nearly zero, or if below min threshold and braking.
	const float VSizeSq = Velocity.SizeSquared();
	if (VSizeSq <= KINDA_SMALL_NUMBER)
	{
		Velocity = FVector::ZeroVector;
	}
}

void UPBPlayerMovement::PlayMoveSound(float DeltaTime)
{
	// Count move sound time down if we've got it
	if (MoveSoundTime > 0)
	{
		MoveSoundTime = FMath::Max(0.0f, MoveSoundTime - 1000.0f * DeltaTime);
	}

	// Check if it's time to play the sound
	if (MoveSoundTime > 0)
	{
		return;
	}

	float Speed = Velocity.SizeSquared();
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
	bool bPlaySound = (bBrakingFrameTolerated || bOnLadder) && Speed >= RunSpeedThreshold * RunSpeedThreshold;

	if (!bPlaySound)
	{
		return;
	}

	bool bSprinting = Speed >= SprintSpeedThreshold * SprintSpeedThreshold;

	float MoveSoundVolume = 1.0f;

	UPBMoveStepSound* MoveSound = nullptr;

	if (bOnLadder)
	{
		MoveSoundVolume = 0.5f;
		MoveSoundTime = 450.0f;
		if (!PBCharacter->GetMoveStepSound(TEnumAsByte<EPhysicalSurface>(EPhysicalSurface::SurfaceType1)))
		{
			return;
		}
		MoveSound = PBCharacter->GetMoveStepSound(TEnumAsByte<EPhysicalSurface>(EPhysicalSurface::SurfaceType1))->GetDefaultObject();
	}
	else
	{
		MoveSoundTime = bSprinting ? 300.0f : 400.0f;
		FHitResult Hit = CurrentFloor.HitResult;
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
			if (!PBCharacter->GetMoveStepSound(TEnumAsByte<EPhysicalSurface>(EPhysicalSurface::SurfaceType_Default)))
			{
				return;
			}
			MoveSound = PBCharacter->GetMoveStepSound(TEnumAsByte<EPhysicalSurface>(EPhysicalSurface::SurfaceType_Default))->GetDefaultObject();
		}

		MoveSoundVolume = bSprinting ? MoveSound->GetSprintVolume() : MoveSound->GetWalkVolume();

		if (IsCrouching())
		{
			MoveSoundVolume *= 0.65f;
			MoveSoundTime += 100.0f;
		}
	}

	if (MoveSound)
	{
		TArray<USoundCue*> MoveSoundCues = StepSide ? MoveSound->GetStepLeftSounds() : MoveSound->GetStepRightSounds();

		if (MoveSoundCues.Num() < 1)
		{
			return;
		}

		USoundCue* Sound = MoveSoundCues[FMath::RandRange(0, MoveSoundCues.Num() - 1)];

		Sound->VolumeMultiplier = MoveSoundVolume;

		/*UPBGameplayStatics::PlaySound(Sound, GetCharacterOwner(),
									  // FVector(0.0f, 0.0f, -GetCharacterOwner()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),
									  EPBSoundCategory::Footstep);*/
		UGameplayStatics::SpawnSoundAttached(Sound, GetCharacterOwner()->GetRootComponent());
	}

	StepSide = !StepSide;
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
	PlayMoveSound(DeltaTime);

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

	// Check if path following requested movement
	bool bZeroRequestedAcceleration = true;
	FVector RequestedAcceleration = FVector::ZeroVector;
	float RequestedSpeed = 0.0f;
	if (ApplyRequestedMove(DeltaTime, MaxAccel, MaxSpeed, Friction, BrakingDeceleration, RequestedAcceleration, RequestedSpeed))
	{
		RequestedAcceleration = RequestedAcceleration.GetClampedToMaxSize(MaxAccel);
		bZeroRequestedAcceleration = false;
	}

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

	// Path following above didn't care about the analog modifier, but we do for everything else below, so get the fully modified value.
	// Use max of requested speed and max speed if we modified the speed in ApplyRequestedMove above.
	const float MaxInputSpeed = FMath::Max(MaxSpeed * AnalogInputModifier, GetMinAnalogSpeed());
	MaxSpeed = FMath::Max(RequestedSpeed, MaxInputSpeed);

	// Apply braking or deceleration
	const bool bZeroAcceleration = Acceleration.IsNearlyZero();
	const bool bIsGroundMove = IsMovingOnGround() && bBrakingFrameTolerated;

	float SurfaceFriction = 1.0f;
	UPhysicalMaterial* PhysMat = CurrentFloor.HitResult.PhysMaterial.Get();
	if (PhysMat)
	{
		SurfaceFriction = FMath::Min(1.0f, PhysMat->Friction * 1.25f);
	}

	// Apply friction
	// TODO: HACK: friction applied only once in substepping due to excessive friction, but this is too little for low frame rates
	if (bIsGroundMove && !bAppliedFriction)
	{
		const float ActualBrakingFriction = (bUseSeparateBrakingFriction ? BrakingFriction : Friction) * SurfaceFriction;
		ApplyVelocityBraking(DeltaTime, ActualBrakingFriction, BrakingDeceleration);
		bAppliedFriction = true;
	}

	// Apply fluid friction
	if (bFluid)
	{
		Velocity = Velocity * (1.0f - FMath::Min(Friction * DeltaTime, 1.0f));
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

		// Apply additional requested acceleration
		if (!bZeroRequestedAcceleration)
		{
			Velocity += RequestedAcceleration * DeltaTime;
		}

		// TODO: Surfing
#if WIP_SURFING
		PreemptCollision(DeltaTime, SurfaceFriction);
#endif

		Velocity = Velocity.GetClampedToMaxSize2D(13470.4f);

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
			// float SpeedMultiplier = UPBUtil::Clamp01(SpeedScale);
			float SpeedMultiplier = FMath::Clamp(SpeedScale, 0.0f, 1.0f);
			SpeedMultiplier *= SpeedMultiplier;
			if (!IsFalling())
			{
				// If we're on ground, factor in friction.
				SpeedMultiplier = FMath::Max((1.0f - SurfaceFriction) * SpeedMultiplier, 0.0f);
			}
			MaxStepHeight = FMath::Clamp(GetClass()->GetDefaultObject<UPBPlayerMovement>()->MaxStepHeight * (1.0f - SpeedMultiplier), MinStepHeight,
										 GetClass()->GetDefaultObject<UPBPlayerMovement>()->MaxStepHeight);
			SetWalkableFloorZ(FMath::Clamp(GetClass()->GetDefaultObject<UPBPlayerMovement>()->GetWalkableFloorZ() - (0.5f * (0.4f - SpeedMultiplier)),
										   GetClass()->GetDefaultObject<UPBPlayerMovement>()->GetWalkableFloorZ(), 0.9848f));
		}
	}

	if (bUseRVOAvoidance)
	{
		CalcAvoidanceVelocity(DeltaTime);
	}
}

void UPBPlayerMovement::Crouch(bool bClientSimulation)
{
	bInCrouch = true;
}

void UPBPlayerMovement::DoCrouchResize(float TargetTime, float DeltaTime, bool bClientSimulation)
{
	if (!HasValidData() || (!bClientSimulation && !CanCrouchInCurrentState()))
	{
		bInCrouch = false;
		return;
	}

	// See if collision is already at desired size.
	UCapsuleComponent* CharacterCapsule = CharacterOwner->GetCapsuleComponent();
	if (FMath::IsNearlyEqual(CharacterCapsule->GetUnscaledCapsuleHalfHeight(), CrouchedHalfHeight))
	{
		if (!bClientSimulation)
		{
			CharacterOwner->bIsCrouched = true;
		}
		CharacterOwner->OnStartCrouch(0.0f, 0.0f);
		bInCrouch = false;
		return;
	}

	auto DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();

	if (bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		// restore collision size before crouching
		CharacterCapsule->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(),
										 DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
		bShrinkProxyCapsule = true;
	}

	// Change collision size to crouching dimensions
	const auto ComponentScale = CharacterCapsule->GetShapeScale();
	const auto OldUnscaledHalfHeight = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float OldUnscaledRadius = CharacterCapsule->GetUnscaledCapsuleRadius();
	const float FullCrouchDiff = OldUnscaledHalfHeight - CrouchedHalfHeight;
	float CurrentUnscaledHalfHeight = CharacterCapsule->GetUnscaledCapsuleHalfHeight();
	// Determine the crouching progress
	const bool InstantCrouch = FMath::IsNearlyZero(TargetTime);
	float CurrentAlpha = 1.0f - (CurrentUnscaledHalfHeight - CrouchedHalfHeight) / FullCrouchDiff;
	// Determine how much we are progressing this tick
	float TargetAlphaDiff = 1.0f;
	float TargetAlpha = 1.0f;
	if (!InstantCrouch)
	{
		TargetAlphaDiff = DeltaTime / CrouchTime;
		TargetAlpha = CurrentAlpha + TargetAlphaDiff;
	}
	if (TargetAlpha >= 1.0f || FMath::IsNearlyEqual(TargetAlpha, 1.0f))
	{
		TargetAlpha = 1.0f;
		TargetAlphaDiff = TargetAlpha - CurrentAlpha;
		bInCrouch = false;
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
			UpdatedComponent->MoveComponent(FVector(0.0f, 0.0f, -ScaledHalfHeightAdjust), UpdatedComponent->GetComponentQuat(), true, nullptr,
											EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
		}
	}

	bForceNextFloorCheck = true;

	AdjustProxyCapsuleSize();
	CharacterOwner->OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// Don't smooth this change in mesh position
	if (bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		if (ClientData && !FMath::IsNearlyZero(ClientData->MeshTranslationOffset.Z))
		{
			ClientData->MeshTranslationOffset -= FVector(0.0f, 0.0f, ScaledHalfHeightAdjust);
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}
}

void UPBPlayerMovement::UnCrouch(bool bClientSimulation)
{
	bInCrouch = true;
}

void UPBPlayerMovement::DoUnCrouchResize(float TargetTime, float DeltaTime, bool bClientSimulation)
{
	if (!HasValidData())
	{
		bInCrouch = false;
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
		bInCrouch = false;
		return;
	}

	const float CurrentCrouchedHalfHeight = CharacterCapsule->GetScaledCapsuleHalfHeight();

	const float ComponentScale = CharacterCapsule->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterCapsule->GetUnscaledCapsuleHalfHeight();
	const float UncrouchedHeight = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float FullCrouchDiff = UncrouchedHeight - CrouchedHalfHeight;
	// Determine the crouching progress
	const bool InstantCrouch = FMath::IsNearlyZero(TargetTime);
	float CurrentAlpha = 1.0f - (UncrouchedHeight - OldUnscaledHalfHeight) / FullCrouchDiff;
	float TargetAlphaDiff = 1.0f;
	float TargetAlpha = 1.0f;
	if (!InstantCrouch)
	{
		TargetAlphaDiff = DeltaTime / TargetTime;
		TargetAlpha = CurrentAlpha + TargetAlphaDiff;
	}
	if (TargetAlpha >= 1.0f || FMath::IsNearlyEqual(TargetAlpha, 1.0f))
	{
		TargetAlpha = 1.0f;
		TargetAlphaDiff = TargetAlpha - CurrentAlpha;
		bInCrouch = false;
	}
	const float HalfHeightAdjust = FullCrouchDiff * TargetAlphaDiff;
	const float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;
	const FVector PawnLocation = UpdatedComponent->GetComponentLocation();

	// Grow to uncrouched size.
	check(CharacterCapsule);

	if (!bClientSimulation)
	{
		// Try to stay in place and see if the larger capsule fits. We use a
		// slightly taller capsule to avoid penetration.
		const UWorld* MyWorld = GetWorld();
		const float SweepInflation = KINDA_SMALL_NUMBER * 10.0f;
		FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CrouchTrace), false, CharacterOwner);
		FCollisionResponseParams ResponseParam;
		InitCollisionParams(CapsuleParams, ResponseParam);

		// Compensate for the difference between current capsule size and
		// standing size
		const FCollisionShape StandingCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, -SweepInflation - ScaledHalfHeightAdjust); // Shrink by
																																				  // negative amount,
																																				  // so actually grow it.
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
					const FVector Down = FVector(0.0f, 0.0f, -TraceDist);

					FHitResult Hit(1.0f);
					const FCollisionShape ShortCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, ShrinkHalfHeight);
					const bool bBlockingHit = MyWorld->SweepSingleByChannel(Hit, PawnLocation, PawnLocation + Down, FQuat::Identity, CollisionChannel,
																			ShortCapsuleShape, CapsuleParams);
					if (Hit.bStartPenetrating)
					{
						bEncroached = true;
					}
					else
					{
						// Compute where the base of the sweep ended up, and see
						// if we can stand there
						const float DistanceToBase = (Hit.Time * TraceDist) + ShortCapsuleShape.Capsule.HalfHeight;
						const FVector NewLoc = FVector(PawnLocation.X, PawnLocation.Y,
													   PawnLocation.Z - DistanceToBase + StandingCapsuleShape.Capsule.HalfHeight + SweepInflation + MIN_FLOOR_DIST / 2.0f);
						bEncroached = MyWorld->OverlapBlockingTestByChannel(NewLoc, FQuat::Identity, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
						if (!bEncroached)
						{
							// Intentionally not using MoveUpdatedComponent,
							// where a horizontal plane constraint would prevent
							// the base of the capsule from staying at the same
							// spot.
							UpdatedComponent->MoveComponent(NewLoc - PawnLocation, UpdatedComponent->GetComponentQuat(), false, nullptr,
															EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
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
						bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, FQuat::Identity, CollisionChannel, StandingCapsuleShape,
																			CapsuleParams, ResponseParam);
					}
				}
			}

			if (!bEncroached)
			{
				// Commit the change in location.
				UpdatedComponent->MoveComponent(StandingLocation - PawnLocation, UpdatedComponent->GetComponentQuat(), false, nullptr,
												EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
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

	// Now call SetCapsuleSize() to cause touch/untouch events and actually grow
	// the capsule
	CharacterCapsule->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), OldUnscaledHalfHeight + HalfHeightAdjust, true);

	const float MeshAdjust = ScaledHalfHeightAdjust;
	AdjustProxyCapsuleSize();
	CharacterOwner->OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// Don't smooth this change in mesh position
	if (bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		if (ClientData && !FMath::IsNearlyZero(ClientData->MeshTranslationOffset.Z))
		{
			ClientData->MeshTranslationOffset += FVector(0.0f, 0.0f, MeshAdjust);
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}
}

float UPBPlayerMovement::GetMaxSpeed() const
{
	if (IsWalking() || bCheatFlying || IsFalling())
	{
		if (PBCharacter->IsSprinting())
		{
			if (IsCrouching())
			{
				return MaxWalkSpeedCrouched * 1.7f;
			}
			return bCheatFlying ? SprintSpeed * 1.5f : SprintSpeed;
		}
		if (PBCharacter->DoesWantToWalk())
		{
			return WalkSpeed;
		}
		if (IsCrouching())
		{
			return MaxWalkSpeedCrouched;
		}
	}

	return Super::GetMaxSpeed();
}
