// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LLMDrivenPlayablePCG : ModuleRules
{
	public LLMDrivenPlayablePCG(ReadOnlyTargetRules Target) : base(Target)
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
			"Slate",
            "PCG",
            "Json",
			"JsonUtilities",
			"HTTP"
        });

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"LLMDrivenPlayablePCG",
			"LLMDrivenPlayablePCG/Variant_Platforming",
			"LLMDrivenPlayablePCG/Variant_Platforming/Animation",
			"LLMDrivenPlayablePCG/Variant_Combat",
			"LLMDrivenPlayablePCG/Variant_Combat/AI",
			"LLMDrivenPlayablePCG/Variant_Combat/Animation",
			"LLMDrivenPlayablePCG/Variant_Combat/Gameplay",
			"LLMDrivenPlayablePCG/Variant_Combat/Interfaces",
			"LLMDrivenPlayablePCG/Variant_Combat/UI",
			"LLMDrivenPlayablePCG/Variant_SideScrolling",
			"LLMDrivenPlayablePCG/Variant_SideScrolling/AI",
			"LLMDrivenPlayablePCG/Variant_SideScrolling/Gameplay",
			"LLMDrivenPlayablePCG/Variant_SideScrolling/Interfaces",
			"LLMDrivenPlayablePCG/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
