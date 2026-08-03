// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TeamProject_MOU : ModuleRules
{
    public TeamProject_MOU(ReadOnlyTargetRules Target) : base(Target)
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
            "GameplayAbilities",
            "GameplayTags",
            "GameplayTasks",
            "Niagara"
        });

        PrivateDependencyModuleNames.AddRange(new string[] { });

        PublicIncludePaths.AddRange(new string[] {
            "TeamProject_MOU",
            "TeamProject_MOU/Variant_Platforming",
            "TeamProject_MOU/Variant_Platforming/Animation",
            "TeamProject_MOU/Variant_Combat",
            "TeamProject_MOU/Variant_Combat/AI",
            "TeamProject_MOU/Variant_Combat/Animation",
            "TeamProject_MOU/Variant_Combat/Gameplay",
            "TeamProject_MOU/Variant_Combat/Interfaces",
            "TeamProject_MOU/Variant_Combat/UI",
            "TeamProject_MOU/Variant_SideScrolling",
            "TeamProject_MOU/Variant_SideScrolling/AI",
            "TeamProject_MOU/Variant_SideScrolling/Gameplay",
            "TeamProject_MOU/Variant_SideScrolling/Interfaces",
            "TeamProject_MOU/Variant_SideScrolling/UI"
        });

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}
