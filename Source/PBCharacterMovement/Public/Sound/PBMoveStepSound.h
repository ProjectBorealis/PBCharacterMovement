// Copyright 2017-2019 Project Borealis

#pragma once

#include "CoreMinimal.h"

#include "PBMoveStepSound.generated.h"

class USoundCue;
/**
 * 
 */
UCLASS(Blueprintable)
class PBCHARACTERMOVEMENT_API UPBMoveStepSound : public UObject
{
	GENERATED_BODY()
public:
	UPBMoveStepSound();
	~UPBMoveStepSound();

	UFUNCTION()
	FORCEINLINE TEnumAsByte<enum EPhysicalSurface> GetSurfaceMaterial() const { return SurfaceMaterial;  }

	UFUNCTION()
	FORCEINLINE TArray<USoundCue*> GetStepLeftSounds() const { return StepLeftSounds;  }

	UFUNCTION()
	FORCEINLINE TArray<USoundCue*> GetStepRightSounds() const { return StepRightSounds; }

	UFUNCTION()
	FORCEINLINE TArray<USoundCue*> GetJumpSounds() const
	{
		return JumpSounds;
	}

	UFUNCTION()
	FORCEINLINE TArray<USoundCue*> GetLandSounds() const
	{
		return LandSounds;
	}

	UFUNCTION()
	FORCEINLINE float GetWalkVolume() const
	{
		return WalkVolume;
	}

	UFUNCTION()
	FORCEINLINE float GetSprintVolume() const
	{
		return SprintVolume;
	}

private:
	/** The physical material associated with this move step sound */
	UPROPERTY(EditDefaultsOnly, Category = "Sounds")
	TEnumAsByte<enum EPhysicalSurface> SurfaceMaterial;

	/** The list of sounds to randomly choose from when stepping left */
	UPROPERTY(EditDefaultsOnly, Category = "Sounds")
	TArray<USoundCue*> StepLeftSounds;

	/** The list of sounds to randomly choose from when stepping right */
	UPROPERTY(EditDefaultsOnly, Category = "Sounds")
	TArray<USoundCue*> StepRightSounds;

	/** The list of sounds to randomly choose from when jumping */
	UPROPERTY(EditDefaultsOnly, Category = "Sounds")
	TArray<USoundCue*> JumpSounds;

	/** The list of sounds to randomly choose from when landing */
	UPROPERTY(EditDefaultsOnly, Category = "Sounds")
	TArray<USoundCue*> LandSounds;

	UPROPERTY(EditDefaultsOnly, Category = "Sounds")
	float WalkVolume = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Sounds")
	float SprintVolume = 0.5f;
};
