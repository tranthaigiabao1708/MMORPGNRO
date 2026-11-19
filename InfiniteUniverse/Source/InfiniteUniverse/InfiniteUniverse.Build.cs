// Copyright InfiniteUniverse Dev. All Rights Reserved.

using UnrealBuildTool;

public class InfiniteUniverse : ModuleRules
{
	public InfiniteUniverse(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Allow clean relative includes such as #include "Core/IU_GameMode.h"
		PublicIncludePaths.AddRange(new string[] {
			"InfiniteUniverse"
		});

		// Strictly scoped to Step 1 Core Gameplay Prototype (No GAS, No Networking/Backend plugins)
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"Niagara",
			"UMG"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate",
			"SlateCore"
		});
	}
}
