using UnrealBuildTool;

public class UeNodeNexusBridge : ModuleRules
{
    public UeNodeNexusBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        IWYUSupport = IWYUSupport.Full;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AssetRegistry",
            "AssetTools",
            "BlueprintGraph",
            "HTTPServer",
            "InputCore",
            "Json",
            "JsonUtilities",
            "MaterialEditor",
            "UnrealEd"
        });
    }
}
