using UnrealBuildTool;

public class UeNodeNexusBridge : ModuleRules
{
    public UeNodeNexusBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        IWYUSupport = IWYUSupport.Full;

        PrivateIncludePaths.AddRange(new string[]
        {
            System.IO.Path.Combine(ModuleDirectory, "Private", "Animation"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Audio"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Assets"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "AutoIndex"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Blueprint"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Blueprint", "NodeInterface"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Core"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Diagnostics"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Dispatch"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Graph"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Input"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Level"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Material"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Material", "NodeInterface"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Material", "Patch"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Module"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Object"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Texture"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Transcode"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Transport")
        });

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Json"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AnimGraph",
            "AssetRegistry",
            "AssetTools",
            "BlueprintGraph",
            "EditorFramework",
            "EnhancedInput",
            "InputCore",
            "JsonUtilities",
            "Landscape",
            "MaterialEditor",
            "MessageLog",
            "PhysicsCore",
            "Projects",
            "UnrealEd"
        });
    }
}
