using UnrealBuildTool;

public class UeNodeNexusBridge : ModuleRules
{
    public UeNodeNexusBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        IWYUSupport = IWYUSupport.Full;

        PrivateIncludePaths.AddRange(new string[]
        {
            System.IO.Path.Combine(ModuleDirectory, "Private", "Assets"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "AutoIndex"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Blueprint"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Core"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Dispatch"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Graph"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Level"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Material"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Module"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Object"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Transport")
        });

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
