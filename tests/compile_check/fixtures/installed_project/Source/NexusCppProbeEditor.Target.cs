using UnrealBuildTool;

public class NexusCppProbeEditorTarget : TargetRules
{
    public NexusCppProbeEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
        ExtraModuleNames.Add("NexusCppProbe");
    }
}
