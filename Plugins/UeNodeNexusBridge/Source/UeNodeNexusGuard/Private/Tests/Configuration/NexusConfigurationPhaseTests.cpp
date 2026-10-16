#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../../Validation/ConfigurationLayers.h"

namespace
{
const TCHAR* StartupSection = TEXT("Nexus.Tests.StartupConsumed");
const TCHAR* ValuesSection = TEXT("Nexus.Tests.AuthoritativeValues");
const TCHAR* StaleSection = TEXT("Nexus.Tests.WorkerOnly");

FString ConfigText(FConfigFile File)
{
    File.Branch = nullptr;
    FString Text;
    File.WriteToString(Text);
    return Text;
}

void SeedStartupConsumer(FConfigFile& Runtime)
{
    Runtime.Branch->FinalCombinedLayers.SetString(StartupSection, TEXT("Redirect"), TEXT("old->new"));
    Runtime.Add(StartupSection, *Runtime.Branch->FinalCombinedLayers.FindSection(StartupSection));
}

FConfigFile SourceSnapshot(const FConfigFile& Runtime)
{
    FConfigFile Snapshot = NexusLifecycle::Validation::EffectiveConfigurationFile(Runtime);
    Snapshot.Remove(StartupSection);
    Snapshot.Remove(StaleSection);
    FConfigSection Values;
    Values.Add(TEXT("BootClass"), FConfigValue(TEXT("/Script/AIModule.AIController")));
    Values.Add(TEXT("Array"), FConfigValue(TEXT("second")));
    Values.Add(TEXT("Array"), FConfigValue(TEXT("first")));
    Values.Add(TEXT("Array"), FConfigValue(TEXT("second")));
    Snapshot.Add(ValuesSection, MoveTemp(Values));
    return Snapshot;
}

struct FLayerScope
{
    FConfigFile& Runtime;
    FConfigBranch& Branch;
    TGuardValue<FConfigFile> RuntimeGuard;
    TGuardValue<FConfigFile> BaselineGuard;
    TGuardValue<FConfigCommandStream> SavedGuard;
    TGuardValue<FConfigCommandStream> LaunchGuard;

    explicit FLayerScope(FConfigFile& InRuntime)
        : Runtime(InRuntime), Branch(*InRuntime.Branch),
          RuntimeGuard(Runtime, Runtime), BaselineGuard(Branch.FinalCombinedLayers, Branch.FinalCombinedLayers),
          SavedGuard(Branch.SavedLayer, Branch.SavedLayer),
          LaunchGuard(Branch.CommandLineOverrides, Branch.CommandLineOverrides)
    {
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusConfigurationStartupConsumption,
    "Nexus.Safety.ConfigurationStartupConsumption",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusConfigurationStartupConsumption::RunTest(const FString& Parameters)
{
    using namespace NexusLifecycle::Validation;
    FConfigFile& Runtime = *GConfig->FindConfigFile(GEngineIni);
    FLayerScope Scope(Runtime);
    SeedStartupConsumer(Runtime);
    Runtime.SetString(ValuesSection, TEXT("WorkerOnlyKey"), TEXT("wrong"));
    const FConfigFile Snapshot = SourceSnapshot(Runtime);
    ApplyConfigurationFile(Runtime, Snapshot, EConfigurationPhase::Startup);
    TestNotNull(TEXT("startup retains inputs not present in the consumed source snapshot"), Runtime.FindSection(StartupSection));
    TestFalse(TEXT("source values replace represented sections during startup"),
        Runtime.FindSection(ValuesSection)->Contains(TEXT("WorkerOnlyKey")));
    TestTrue(TEXT("native consumer sees and removes its startup input"),
        GConfig->RemoveSectionFromBranch(StartupSection, *GEngineIni));
    TestNull(TEXT("native consumption removes the live input"), Runtime.FindSection(StartupSection));
    TestNull(TEXT("native consumption removes the reconstruction input"), Scope.Branch.FinalCombinedLayers.FindSection(StartupSection));
    ApplyConfigurationFile(Runtime, Snapshot, EConfigurationPhase::Runtime);
    TestEqual(TEXT("completed startup captures exactly the source state"),
        ConfigText(EffectiveConfigurationFile(Runtime)), ConfigText(Snapshot));
    ApplyConfigurationFile(Runtime, Snapshot, EConfigurationPhase::Runtime);
    TestEqual(TEXT("repeated completion is idempotent"),
        ConfigText(EffectiveConfigurationFile(Runtime)), ConfigText(Snapshot));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusConfigurationRuntimeSnapshot,
    "Nexus.Safety.ConfigurationRuntimeSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusConfigurationRuntimeSnapshot::RunTest(const FString& Parameters)
{
    using namespace NexusLifecycle::Validation;
    FConfigFile& Runtime = *GConfig->FindConfigFile(GEngineIni);
    FLayerScope Scope(Runtime);
    const FConfigFile Snapshot = SourceSnapshot(Runtime);
    SeedStartupConsumer(Runtime);
    Scope.Branch.FinalCombinedLayers.SetString(StaleSection, TEXT("Worker"), TEXT("default"));
    Scope.Branch.FinalCombinedLayers.SetString(ValuesSection, TEXT("Cleared"), TEXT("base"));
    Scope.Branch.SavedLayer.FindOrAdd(ValuesSection).Add(TEXT("Cleared"),
        FConfigValue(TEXT("saved"), FConfigValue::EValueType::Set));
    Scope.Branch.CommandLineOverrides.FindOrAdd(ValuesSection).Add(TEXT("Array"),
        FConfigValue(TEXT("command"), FConfigValue::EValueType::ArrayAdd));
    ApplyConfigurationFile(Runtime, Snapshot, EConfigurationPhase::Runtime);
    TestEqual(TEXT("runtime capture does not resurrect worker defaults, saved values or launch arrays"),
        ConfigText(EffectiveConfigurationFile(Runtime)), ConfigText(Snapshot));
    TestEqual(TEXT("runtime reconstruction uses the measured source baseline"),
        ConfigText(Scope.Branch.FinalCombinedLayers), ConfigText(Snapshot));
    TestEqual(TEXT("worker saved commands are folded into authoritative source state"), Scope.Branch.SavedLayer.Num(), 0);
    TestEqual(TEXT("worker launch commands cannot override source arrays"), Scope.Branch.CommandLineOverrides.Num(), 0);
    ApplyConfigurationFile(Runtime, Snapshot, EConfigurationPhase::Runtime);
    TestEqual(TEXT("repeated runtime replay preserves ordered duplicate arrays and explicit removals"),
        ConfigText(EffectiveConfigurationFile(Runtime)), ConfigText(Snapshot));
    FConfigFile Standalone;
    Standalone.SetString(StaleSection, TEXT("Old"), TEXT("value"));
    ApplyConfigurationFile(Standalone, Snapshot, EConfigurationPhase::Runtime);
    TestEqual(TEXT("runtime replay also replaces standalone snapshots"), ConfigText(Standalone), ConfigText(Snapshot));
    TestNull(TEXT("standalone replay keeps independent ownership"), Standalone.Branch);
    return true;
}
#endif
