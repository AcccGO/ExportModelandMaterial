// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class GetModel : ModuleRules
{
    public GetModel(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public/"));
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Private/"));

        PublicIncludePaths.AddRange(
            new string[] {
                //"GetModel/Public"
				// ... add public include paths required here ...
			}
            );

        PrivateIncludePaths.AddRange(
            new string[] {
                //"GetModel/Private"
				// ... add other private include paths required here ...
			}
            );

        PublicIncludePathModuleNames.AddRange(
            new string[] {
               "HierarchicalLODUtilities",
               "MeshUtilities",
               "MeshReductionInterface",
            }
            );

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "HierarchicalLODUtilities",
                "MeshUtilities",
                "MeshReductionInterface",
                "MaterialBaking",
                "AssetRegistry",
                "ContentBrowser",
                "Documentation",
                "LevelEditor",
                "PropertyEditor",
                "RawMesh",
                "WorkspaceMenuStructure",
                "MeshMergeUtilities",
            }
            );

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
				// ... add other public dependencies that you statically link with here ...
                "MeshUtilities",
                "MaterialUtilities",
                "RawMesh",
                "MaterialBaking",
                "MeshMergeUtilities",
                "MeshDescription",
                "MeshDescriptionOperations"
            }
            );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Projects",
                "InputCore",
                "UnrealEd",
                "LevelEditor",
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
				// ... add private dependencies that you statically link with here ...
                "Core",
                "RenderCore",
                "Renderer",
                "RHI",
                "Landscape",
                //"ShaderCore",
                "MaterialUtilities",
                "StaticMeshEditor",
                "SkeletalMeshEditor",
                "MaterialBaking",
                "MeshMergeUtilities",
                "MeshUtilitiesCommon",
                "EditorStyle",
            }
            );

        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
				// ... add any modules that your module loads dynamically here ...
                "HierarchicalLODUtilities",
                "MeshReductionInterface",
                "AssetRegistry",
                "ContentBrowser",
                "Documentation",
            }
            );
    }
}