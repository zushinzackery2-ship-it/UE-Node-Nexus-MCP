#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../../Validation/Environment.h"
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
    TGuardValue<bool> HierarchyGuard(Branch.bIsHierarchical, true);
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
    return true;
}
#endif
