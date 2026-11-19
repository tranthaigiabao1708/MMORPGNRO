// Copyright InfiniteUniverse Dev. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class InfiniteUniverseEditorTarget : TargetRules
{
	public InfiniteUniverseEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("InfiniteUniverse");
	}
}
