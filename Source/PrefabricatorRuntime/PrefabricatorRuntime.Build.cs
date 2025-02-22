//$ Copyright 2015-23, Code Respawn Technologies Pvt Ltd - All Rights Reserved $//

namespace UnrealBuildTool.Rules
{
	public class PrefabricatorRuntime : ModuleRules
	{
		public PrefabricatorRuntime(ReadOnlyTargetRules Target) : base(Target)
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

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
				    "Engine",
                    "PropertyPath",
                    "DeveloperSettings"
					// ... add other public dependencies that you statically link with here ...
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"PropertyEditor"
					// ... add private dependencies that you statically link with here ...
				}
				);
			if(Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("UnrealEd");
			}

			DynamicallyLoadedModuleNames.AddRange(
				new string[]
				{
					// ... add any modules that your module loads dynamically here ...
				}
				);
		}
	}
}
