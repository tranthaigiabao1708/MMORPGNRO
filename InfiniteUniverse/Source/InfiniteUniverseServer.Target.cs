// Copyright InfiniteUniverse Dev. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class InfiniteUniverseServerTarget : TargetRules
{
	public InfiniteUniverseServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("InfiniteUniverse");
	}
}
