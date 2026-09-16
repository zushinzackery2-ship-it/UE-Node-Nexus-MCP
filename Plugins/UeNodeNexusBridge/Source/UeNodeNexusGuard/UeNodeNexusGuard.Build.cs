using UnrealBuildTool;

public class UeNodeNexusGuard : ModuleRules
{
    public UeNodeNexusGuard(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        UeNodeNexusBridge.ConfigureBuildIdentity(this);
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core", "Json"
        });
        PublicSystemLibraries.AddRange(new string[]
        {
            "bcrypt.lib", "advapi32.lib"
        });
    }
}
