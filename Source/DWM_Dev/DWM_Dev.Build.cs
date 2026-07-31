// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DWM_Dev : ModuleRules
{
	public DWM_Dev(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore","EnhancedInput", "HTTP", "SQLiteCore", "SQLiteSupport", "UMG" });

		// The native fallback dialogue panel uses Slate font/style types at runtime.
		// These cannot be editor-only dependencies or packaged/game targets will fail to link.
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		if (Target.bBuildEditor)
		{
			// Editor-only dependencies for the selected-static-mesh-to-door conversion command.
			// Keeping these private and conditional prevents editor APIs from entering game builds.
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "ToolMenus" });
		}
	}
}
