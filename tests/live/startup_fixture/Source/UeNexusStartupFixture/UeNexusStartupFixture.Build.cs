using UnrealBuildTool;

public class UeNexusStartupFixture : ModuleRules
{
    public UeNexusStartupFixture(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Core"
        });
    }
}
