using UnrealBuildTool;

public class UeNodeNexusBridge : ModuleRules
{
    public UeNodeNexusBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        IWYUSupport = IWYUSupport.Full;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AssetRegistry",
            "CoreUObject",
            "Engine",
            "HTTPServer",
            "Json",
            "JsonUtilities",
            "MaterialEditor",
            "UnrealEd"
        });
    }
}
