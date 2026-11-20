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

		// Step 1 Core Gameplay + Step 2 Native UE5 Replication + Step 3 Backend HTTP/JSON Bridge
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"NetCore",
			"HTTP",
			"Json",
			"JsonUtilities",
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
