using UnrealBuildTool;

public class UeNodeNexusGuard : ModuleRules
{
    public UeNodeNexusGuard(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Resolve the sibling rules from this rules assembly. An installed Marketplace
        // assembly can contain the same type name when a project supplies its own plugin.
        var CoreRules = GetType().Assembly.GetType("UeNodeNexusBridge");
        var Configure = CoreRules?.GetMethod("ConfigureBuildIdentity");
        if (Configure == null)
        {
            throw new BuildException("Nexus Core build identity rules are missing from the plugin rules assembly");
        }
        Configure.Invoke(null, new object[]
        {
            this
        });
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
