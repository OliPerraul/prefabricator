//$ Copyright 2015-23, Code Respawn Technologies Pvt Ltd - All Rights Reserved $//

namespace UnrealBuildTool.Rules
{
	public class PrefabricatorEditor : ModuleRules
	{
		public PrefabricatorEditor(ReadOnlyTargetRules Target) : base(Target)
        {
            bUseUnity = false;
            PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
			OptimizeCode = CodeOptimization.InShippingBuildsOnly;
			ShadowVariableWarningLevel = WarningLevel.Off;
			bWarningsAsErrors = false;
			PublicIncludePaths.AddRange(
				new string[] {
					// ... add public include paths required here ...
                }
				);

			PrivateIncludePaths.AddRange(
				new string[] {
					// ... add other private include paths required here ...
				}
				);

            PrivateIncludePathModuleNames.AddRange(
                new string[] {
                "Settings",
                "AssetTools",
            }
            );

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
                    "Engine",
                    "EditorStyle",
                    "UnrealEd",
                    "AdvancedPreviewScene",
                    "Kismet",
                    "PlacementMode",
                    "EditorWidgets",
                    "RHI",
                    "ToolMenus",
					"SceneOutliner"
                }
				);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
                    "InputCore",
                    "Slate",
                    "SlateCore",
				    "RenderCore",
                    "PropertyEditor",
                    "WorkspaceMenuStructure",
                    "LevelEditor",
				    "EditorStyle",
				    "ContentBrowser",
				    "ContentBrowserData",
                    "Projects",
                    "PrefabricatorRuntime",
					"SceneOutliner"
					// ... add private dependencies that you statically link with here ...
				}
				);

			DynamicallyLoadedModuleNames.AddRange(
				new string[]
				{
					// ... add any modules that your module loads dynamically here ...
                }
				);
		}
	}
}
