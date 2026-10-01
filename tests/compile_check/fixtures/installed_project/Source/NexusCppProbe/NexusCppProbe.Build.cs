using UnrealBuildTool;

public class NexusCppProbe : ModuleRules
{
    public NexusCppProbe(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.Add("Core");
    }
}
