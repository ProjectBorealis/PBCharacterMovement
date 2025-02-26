// Copyright Project Borealis

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
