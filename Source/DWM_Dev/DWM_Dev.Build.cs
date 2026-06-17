// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DWM_Dev : ModuleRules
{
	public DWM_Dev(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore","EnhancedInput", "HTTP" });
	}
}
