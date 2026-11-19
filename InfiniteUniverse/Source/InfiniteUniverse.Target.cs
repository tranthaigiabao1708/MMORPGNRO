// Copyright InfiniteUniverse Dev. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class InfiniteUniverseTarget : TargetRules
{
	public InfiniteUniverseTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("InfiniteUniverse");
	}
}
