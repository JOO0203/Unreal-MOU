// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
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
            "SlateCore",
            "GameplayAbilities",
            "GameplayTags",
            "GameplayTasks",
            "Niagara",
            "Sockets",
            "Networking"
        });

        PrivateDependencyModuleNames.AddRange(new string[] { });

        PublicIncludePaths.AddRange(new string[] {
            "TeamProject_MOU"
        });

        PublicIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "MOU_ChatServer", "Shared")));

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}
