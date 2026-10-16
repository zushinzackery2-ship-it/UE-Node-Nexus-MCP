#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../../Validation/Environment.h"
#include "../../Validation/ConfigurationLayers.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
const TCHAR* SectionName = TEXT("Nexus.Issues12.ConfigurationReplay");

FString ConfigText(FConfigFile File)
{
    File.Branch = nullptr;
    FString Text;
    File.WriteToString(Text);
    return Text;
}

TArray<FString> Values(const TSharedPtr<FJsonObject>& Snapshot, const TCHAR* Key)
{
    TArray<FString> Result;
    for (const auto& Entry : Snapshot->GetObjectField(TEXT("Engine"))->GetArrayField(SectionName))
    {
        const auto Row = Entry->AsObject();
        if (Row->GetStringField(TEXT("key")) == Key)
        {
            Result.Add(Row->GetStringField(TEXT("value")));
        }
    }
    return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusConfigurationLayerReplay,
    "Nexus.Safety.ConfigurationLayerReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusConfigurationLayerReplay::RunTest(const FString& Parameters)
{
    using namespace NexusLifecycle::Validation;
    FConfigFile* Runtime = GConfig->FindConfigFile(GEngineIni);
    FConfigBranch& Branch = *Runtime->Branch;
    TGuardValue<FConfigFile> RuntimeGuard(*Runtime, *Runtime);
    TGuardValue<FConfigFile> BaselineGuard(Branch.FinalCombinedLayers, Branch.FinalCombinedLayers);
    TGuardValue<FConfigCommandStream> SavedGuard(Branch.SavedLayer, Branch.SavedLayer);
    TGuardValue<FConfigCommandStream> LaunchGuard(Branch.CommandLineOverrides, Branch.CommandLineOverrides);
    AddInfo(FString::Printf(TEXT("Actual known Engine branch: hierarchical=%d final_sections=%d runtime_sections=%d"),
        Branch.bIsHierarchical, Branch.FinalCombinedLayers.Num(), Runtime->Num()));
    Branch.FinalCombinedLayers.SetString(SectionName, TEXT("BootClass"), TEXT("/Script/AIModule.AIController"));
    Branch.FinalCombinedLayers.SetString(SectionName, TEXT("Deleted"), TEXT("old"));
    Branch.FinalCombinedLayers.SetString(SectionName, TEXT("Launch"), TEXT("base"));
    Branch.FinalCombinedLayers.AddToSection(SectionName, TEXT("Array"), TEXT("old-a"));
    Branch.FinalCombinedLayers.AddToSection(SectionName, TEXT("Array"), TEXT("old-b"));
    Runtime->Add(SectionName, *Branch.FinalCombinedLayers.FindSection(SectionName));
    Runtime->RemoveKeyFromSection(SectionName, TEXT("Deleted"));
    Branch.CommandLineOverrides.FindOrAdd(SectionName).Add(TEXT("Launch"),
        FConfigValue(TEXT("command-line"), FConfigValue::EValueType::Set));
    Runtime->ApplyFile(&Branch.CommandLineOverrides);
    Runtime->ResetKeyInSection(SectionName, TEXT("BootClass"));
    Runtime->ResetKeyInSection(SectionName, TEXT("Array"));
    FConfigSection RuntimeSection;
    for (const auto& Entry : *Runtime->FindSection(SectionName))
    {
        RuntimeSection.Add(Entry.Key, Entry.Value);
    }
    RuntimeSection.Add(TEXT("Array"), FConfigValue(TEXT("new-b")));
    RuntimeSection.Add(TEXT("Array"), FConfigValue(TEXT("new-a")));
    RuntimeSection.Add(TEXT("Array"), FConfigValue(TEXT("new-b")));
    Runtime->Add(SectionName, MoveTemp(RuntimeSection));
    Runtime->SetString(SectionName, TEXT("Live"), TEXT("runtime"));
    const FConfigFile Before = *Runtime;
    const FConfigCommandStream SavedBefore = Branch.SavedLayer;

    for (bool Hierarchical : {false, true})
    {
        TGuardValue<bool> HierarchyGuard(Branch.bIsHierarchical, Hierarchical);
        AddInfo(FString::Printf(TEXT("Repeated layer capture: hierarchical=%d"), Hierarchical));
        const auto Snapshot = Configuration();
        TestTrue(TEXT("a reset inherited startup class is recovered"),
            Values(Snapshot, TEXT("BootClass")) == TArray<FString>({TEXT("/Script/AIModule.AIController")}));
        TestTrue(TEXT("explicit clear remains removed"), Values(Snapshot, TEXT("Deleted")).IsEmpty());
        TestTrue(TEXT("command-line precedence survives reconstruction"),
            Values(Snapshot, TEXT("Launch")) == TArray<FString>({TEXT("command-line")}));
        TestTrue(TEXT("direct runtime changes survive reconstruction"),
            Values(Snapshot, TEXT("Live")) == TArray<FString>({TEXT("runtime")}));
        TestEqual(TEXT("runtime arrays preserve order and duplicates"),
            FString::Join(Values(Snapshot, TEXT("Array")), TEXT(",")), FString(TEXT("new-b,new-a,new-b")));
        TestEqual(TEXT("capture leaves the live cache unchanged"), ConfigText(*Runtime), ConfigText(Before));
        FConfigFile TrackedBefore = Branch.FinalCombinedLayers;
        TrackedBefore.Branch = nullptr;
        TrackedBefore.ApplyFile(&SavedBefore);
        FConfigFile TrackedAfter = Branch.FinalCombinedLayers;
        TrackedAfter.Branch = nullptr;
        TrackedAfter.ApplyFile(&Branch.SavedLayer);
        TestEqual(TEXT("capture leaves tracked changes unchanged"), ConfigText(TrackedAfter), ConfigText(TrackedBefore));
        const auto Repeat = Configuration();
        TestTrue(TEXT("repeated capture preserves restored inherited defaults"),
            Values(Repeat, TEXT("BootClass")) == TArray<FString>({TEXT("/Script/AIModule.AIController")}));
        TestEqual(TEXT("repeated capture preserves array order"),
            FString::Join(Values(Repeat, TEXT("Array")), TEXT(",")), FString(TEXT("new-b,new-a,new-b")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusStandaloneConfigurationReplay,
    "Nexus.Safety.ConfigurationStandaloneReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusStandaloneConfigurationReplay::RunTest(const FString& Parameters)
{
    FConfigFile Standalone;
    Standalone.SetString(SectionName, TEXT("Runtime"), TEXT("local"));
    Standalone.AddToSection(SectionName, TEXT("Array"), TEXT("second"));
    Standalone.AddToSection(SectionName, TEXT("Array"), TEXT("first"));
    Standalone.AddToSection(SectionName, TEXT("Array"), TEXT("second"));
    const FString Before = ConfigText(Standalone);
    const FConfigFile Effective = NexusLifecycle::Validation::EffectiveConfigurationFile(Standalone);
    TestEqual(TEXT("a standalone file keeps all local scalar and array data"), ConfigText(Effective), Before);
    TestEqual(TEXT("standalone capture preserves the original input"), ConfigText(Standalone), Before);
    TestNull(TEXT("standalone capture keeps independent ownership"), Effective.Branch);
    return true;
}
#endif
