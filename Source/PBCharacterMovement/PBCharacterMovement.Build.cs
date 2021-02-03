// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PBCharacterMovement : ModuleRules
{
	public PBCharacterMovement(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"PhysicsCore"
			}
		);
	}
}
