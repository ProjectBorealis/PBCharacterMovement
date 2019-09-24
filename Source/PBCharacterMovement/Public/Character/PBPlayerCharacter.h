// Copyright 2017-2018 Project Borealis

#pragma once

#include "CoreMinimal.h"

#include "GameFramework/Character.h"

#include "PBPlayerCharacter.generated.h"

class USoundCue;
class UPBMoveStepSound;
class UPBPlayerMovement;

UCLASS(config = Game)
class PBCHARACTERMOVEMENT_API APBPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	virtual void ClearJumpInput(float DeltaTime) override;
	virtual void StopJumping() override;
	virtual void OnJumped_Implementation() override;
	virtual bool CanJumpInternal_Implementation() const override;

	/* Triggered when player's movement mode has changed */
	void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PrevCustomMode) override;

	class APBPlayerController* GetPBController()
	{
		return PBController;
	}

	float GetLastJumpTime()
	{
		return LastJumpTime;
	}

private:
	class APBPlayerController* PBController;

	/** when we last jumped */
	float LastJumpTime;

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

	/** Move step sounds by physical surface */
	UPROPERTY(EditDefaultsOnly, meta = (AllowPrivateAccess = "true"), Category = "PB Player|Sounds")
	TMap<TEnumAsByte<EPhysicalSurface>, TSubclassOf<UPBMoveStepSound>> MoveStepSounds;

	/** Pointer to player movement component */
	UPBPlayerMovement* MovementPtr;

	/** True if we're sprinting*/
	bool bIsSprinting;

	bool bWantsToWalk;

	virtual void ApplyDamageMomentum(float DamageTaken, FDamageEvent const& DamageEvent, APawn* PawnInstigator, AActor* DamageCauser) override;

protected:
	virtual void BeginPlay();

public:
	APBPlayerCharacter(const FObjectInitializer& ObjectInitializer);

#pragma region Mutators
	UFUNCTION()
	bool IsSprinting() const
	{
		return bIsSprinting;
	}
	UFUNCTION()
	bool DoesWantToWalk() const
	{
		return bWantsToWalk;
	}
	FORCEINLINE TSubclassOf<UPBMoveStepSound>* GetMoveStepSound(TEnumAsByte<EPhysicalSurface> Surface)
	{
		return MoveStepSounds.Find(Surface);
	};
	UFUNCTION(Category = "PB Getters", BlueprintPure) FORCEINLINE float GetBaseTurnRate() const
	{
		return BaseTurnRate;
	};
	UFUNCTION(Category = "PB Setters", BlueprintCallable) void SetBaseTurnRate(float val)
	{
		BaseTurnRate = val;
	};
	UFUNCTION(Category = "PB Getters", BlueprintPure) FORCEINLINE float GetBaseLookUpRate() const
	{
		return BaseLookUpRate;
	};
	UFUNCTION(Category = "PB Setters", BlueprintCallable) void SetBaseLookUpRate(float val)
	{
		BaseLookUpRate = val;
	};
	UFUNCTION(Category = "PB Getters", BlueprintPure) FORCEINLINE bool GetAutoBunnyhop() const
	{
		return bAutoBunnyhop;
	};
	UFUNCTION(Category = "PB Setters", BlueprintCallable) void SetAutoBunnyhop(bool val)
	{
		bAutoBunnyhop = val;
	};
	UFUNCTION(Category = "PB Getters", BlueprintPure) FORCEINLINE UPBPlayerMovement* GetMovementPtr() const
	{
		return MovementPtr;
	};

#pragma endregion Mutators

	UFUNCTION()
	void ToggleNoClip();

	/** Handles stafing movement, left and right */
	UFUNCTION()
	void Move(FVector Direction, float Value);

	/**
	 * Called via input to turn at a given rate.
	 * @param Rate	This is a normalized rate, i.e. 1.0 means 100% of desired
	 * turn rate
	 */
	UFUNCTION()
	void Turn(bool bIsPure, float Rate);

	/**
	 * Called via input to turn look up/down at a given rate.
	 * @param Rate	This is a normalized rate, i.e. 1.0 means 100% of desired
	 * turn rate
	 */
	UFUNCTION()
	void LookUp(bool bIsPure, float Rate);

	/** Change camera height */
	void RecalculateBaseEyeHeight() override
	{
		Super::Super::RecalculateBaseEyeHeight();
	}

	virtual bool CanCrouch() const override;
};
