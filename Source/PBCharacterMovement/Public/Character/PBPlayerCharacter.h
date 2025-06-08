// Copyright Project Borealis

#pragma once

#include "GameFramework/Character.h"

#include "PBPlayerCharacter.generated.h"

class USoundCue;
class UPBMoveStepSound;
class UPBPlayerMovement;

inline float SimpleSpline(float Value)
{
	const float ValueSquared = Value * Value;
	return (3.0f * ValueSquared - 2.0f * ValueSquared * Value);
}

UCLASS(config = Game)
class PBCHARACTERMOVEMENT_API APBPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	static const float CAPSULE_RADIUS;
	static const float CAPSULE_HEIGHT;

	APBPlayerCharacter(const FObjectInitializer& ObjectInitializer);

	void BeginPlay() override;
	void Tick(float DeltaTime) override;

	void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/* Triggered when player's movement mode has changed */
	void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PrevCustomMode) override;

	void ClearJumpInput(float DeltaTime) override;
	void Jump() override;
	void StopJumping() override;
	void OnJumped_Implementation() override;
	bool CanJumpInternal_Implementation() const override;
	
	bool CanCrouch() const override;

	void RecalculateBaseEyeHeight() override;

	void ApplyDamageMomentum(float DamageTaken, FDamageEvent const& DamageEvent, APawn* PawnInstigator, AActor* DamageCauser) override;

	/**
	 * Called via input to turn at a given rate.
	 * @param Rate	This is a normalized rate, i.e. 1.0 means 100% of desired
	 * turn rate
	 */
	UFUNCTION()
	void Turn(float Rate);

	/**
	 * Called via input to turn look up/down at a given rate.
	 * @param Rate	This is a normalized rate, i.e. 1.0 means 100% of desired
	 * turn rate
	 */
	UFUNCTION()
	void LookUp(float Rate);

	/**
	* Move forward/back
	*
	* @param Val Movment input to apply
	*/
	void MoveForward(float Val);

	/**
	* Strafe right/left
	*
	* @param Val Movment input to apply
	*/
	void MoveRight(float Val);

	/**
	* Move Up/Down in allowed movement modes.
	*
	* @param Val Movment input to apply
	*/
	void MoveUp(float Val);

	/* Frame rate independent turn */
	void TurnAtRate(float Val);

	/* Frame rate independent lookup */
	void LookUpAtRate(float Val);

	void AddControllerYawInput(float Val) override;

	void AddControllerPitchInput(float Val) override;

	/** Get whether the player is on a ladder at the moment */
	UFUNCTION()
	bool IsOnLadder() const;
	
	/** Gets the player's current fall speed */
	UFUNCTION(Category = "Player Movement", BlueprintCallable)
	float GetFallSpeed(bool bAfterLand = false);

	UFUNCTION()
	void OnCrouch();

	UFUNCTION()
	void OnUnCrouch();

	UFUNCTION()
	void CrouchToggle();

	/** */
	UFUNCTION(BlueprintCallable)
	bool CanWalkOn(const FHitResult& Hit) const;

	TSubclassOf<UPBMoveStepSound>* GetMoveStepSound(TEnumAsByte<EPhysicalSurface> Surface) { return MoveStepSounds.Find(Surface); }

#pragma region Mutators

	UFUNCTION(Category = "PB Getters", BlueprintPure)
	bool IsSprinting() const
	{
		return bIsSprinting;
	}
	UFUNCTION(Category = "PB Setters", BlueprintCallable)
	void SetSprinting(bool bSprint)
	{
		bIsSprinting = bSprint;
	}
	UFUNCTION(Category = "PB Getters", BlueprintPure)
	bool DoesWantToWalk() const
	{
		return bWantsToWalk;
	}
	UFUNCTION(Category = "PB Setters", BlueprintCallable)
	void SetWantsToWalk(bool bWalk)
	{
		bWantsToWalk = bWalk;
	}
	UFUNCTION(Category = "PB Getters", BlueprintPure)
	FORCEINLINE float GetBaseTurnRate() const
	{
		return BaseTurnRate;
	};
	UFUNCTION(Category = "PB Setters", BlueprintCallable)
	void SetBaseTurnRate(float val)
	{
		BaseTurnRate = val;
	};
	UFUNCTION(Category = "PB Getters", BlueprintPure)
	FORCEINLINE float GetBaseLookUpRate() const
	{
		return BaseLookUpRate;
	};
	UFUNCTION(Category = "PB Setters", BlueprintCallable)
	void SetBaseLookUpRate(float val)
	{
		BaseLookUpRate = val;
	};
	UFUNCTION(Category = "PB Getters", BlueprintPure)
	FORCEINLINE bool GetAutoBunnyhop() const
	{
		return bAutoBunnyhop;
	};
	UFUNCTION(Category = "PB Setters", BlueprintCallable)
	void SetAutoBunnyhop(bool val)
	{
		bAutoBunnyhop = val;
	};
	UFUNCTION(Category = "PB Getters", BlueprintPure)
	FORCEINLINE UPBPlayerMovement* GetMovementPtr() const
	{
		return MovementPtr;
	};
	UFUNCTION(Category = "PB Getters", BlueprintPure)
	bool IsSuitEquipped() const
	{
		return bSuitEquipped;
	}
	UFUNCTION(Category = "PB Setters", BlueprintCallable)
	void SetSuitEquipped(bool bEquipped, bool bAdmire = true)
	{
		bSuitEquipped = bEquipped;
	}

	UFUNCTION(Category = "PB Getters", BlueprintPure)
	float GetDefaultBaseEyeHeight() const
	{
		return DefaultBaseEyeHeight;
	}

	UFUNCTION(Category = "PB Getters", BlueprintPure)
	float GetMinSpeedForFallDamage() const
	{
		return MinSpeedForFallDamage;
	};
	UFUNCTION(Category = "PB Getters", BlueprintPure)
	float GetFatalFallSpeed() const
	{
		return FatalFallSpeed;
	};
	UFUNCTION(Category = "PB Getters", BlueprintPure)
	float GetMinLandBounceSpeed() const
	{
		return MinLandBounceSpeed;
	}

#pragma endregion Mutators

	UFUNCTION()
	void ToggleNoClip();

protected:
	/** Returns Mesh1P subobject **/
	FORCEINLINE USkeletalMeshComponent* GetMesh1P() const { return Mesh1P; }

	/** Default crouched eye height */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Camera)
	float FullCrouchedEyeHeight;

private:
	/** pawn mesh: 1st person view */
	UPROPERTY(VisibleDefaultsOnly, Category = Mesh)
	TObjectPtr<USkeletalMeshComponent> Mesh1P;

	/** cached default eye height */
	float DefaultBaseEyeHeight;

	/** throttle jump boost when going up a ramp, so we don't spam it */
	float LastJumpBoostTime;

	/** maximum time it takes to jump */
	float MaxJumpTime;

	/** Base turn rate, in deg/sec. Other scaling may affect final turn rate. */
	UPROPERTY(VisibleAnywhere, meta = (AllowPrivateAccess = "true"), Category = "PB Player|Camera")
	float BaseTurnRate;

	/** Base look up/down rate, in deg/sec. Other scaling may affect final rate.*/
	UPROPERTY(VisibleAnywhere, meta = (AllowPrivateAccess = "true"), Category = "PB Player|Camera")
	float BaseLookUpRate;

	/** Automatic bunnyhopping */
	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = "true"), Category = "PB Player|Gameplay")
	bool bAutoBunnyhop;

	/** Has the suit */
	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = "true"), Category = "PB Player|Gameplay")
	bool bSuitEquipped = true;

	/** Move step sounds by physical surface */
	UPROPERTY(EditDefaultsOnly, meta = (AllowPrivateAccess = "true"), Category = "PB Player|Sounds")
	TMap<TEnumAsByte<EPhysicalSurface>, TSubclassOf<UPBMoveStepSound>> MoveStepSounds;

	/** Minimum speed to play the camera shake for landing */
	UPROPERTY(EditDefaultsOnly, meta = (AllowPrivateAccess = "true"), Category = "PB Player|Damage")
	float MinLandBounceSpeed;

	/** Don't take damage below this speed - so jumping doesn't damage */
	UPROPERTY(EditDefaultsOnly, meta = (AllowPrivateAccess = "true"), Category = "PB Player|Damage")
	float MinSpeedForFallDamage;
	
	/** If you're going faster than this, you're dead */
	UPROPERTY(EditDefaultsOnly, meta = (AllowPrivateAccess = "true"), Category = "PB Player|Damage")
	float FatalFallSpeed;

	// In HL2, the player has the Z component for applying momentum to the capsule capped
	UPROPERTY(EditDefaultsOnly, meta = (AllowPrivateAccess = "true"), Category = "PB Player|Damage")
	float CapDamageMomentumZ = 0.f;

	/** Pointer to player movement component */
	UPROPERTY()
	TObjectPtr<UPBPlayerMovement> MovementPtr;

	/** True if we're sprinting*/
	UPROPERTY(Transient, Replicated)
	bool bIsSprinting;

	UPROPERTY(Transient, Replicated)
	bool bWantsToWalk;

	/** defer the jump stop for a frame (for early jumps) */
	bool bDeferJumpStop = false;
};
