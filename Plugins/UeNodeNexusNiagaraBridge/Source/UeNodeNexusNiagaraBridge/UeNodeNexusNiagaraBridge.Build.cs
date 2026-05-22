using UnrealBuildTool;

public class UeNodeNexusNiagaraBridge : ModuleRules
{
    public UeNodeNexusNiagaraBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        IWYUSupport = IWYUSupport.Full;

        PrivateIncludePaths.AddRange(new string[]
        {
            System.IO.Path.Combine(ModuleDirectory, "Private", "Module"),
            System.IO.Path.Combine(ModuleDirectory, "Private", "Niagara")
        });

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "UeNodeNexusBridge"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AssetRegistry",
            "Json",
            "JsonUtilities",
            "Niagara",
            "NiagaraCore",
            "NiagaraEditor",
            "Sequencer",
            "UnrealEd"
        });
    }
}
