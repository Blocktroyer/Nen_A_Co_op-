// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Nen : ModuleRules
{
	public Nen(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Nen",
			"Nen/Variant_Platforming",
			"Nen/Variant_Platforming/Animation",
			"Nen/Variant_Combat",
			"Nen/Variant_Combat/AI",
			"Nen/Variant_Combat/Animation",
			"Nen/Variant_Combat/Gameplay",
			"Nen/Variant_Combat/Interfaces",
			"Nen/Variant_Combat/UI",
			"Nen/Variant_SideScrolling",
			"Nen/Variant_SideScrolling/AI",
			"Nen/Variant_SideScrolling/Gameplay",
			"Nen/Variant_SideScrolling/Interfaces",
			"Nen/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
