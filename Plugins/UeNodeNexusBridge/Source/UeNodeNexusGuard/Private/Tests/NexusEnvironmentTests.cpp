#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../Validation/Environment.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusEffectiveConfiguration,
    "Nexus.Issues10.Safety.EffectiveConfiguration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusEffectiveConfiguration::RunTest(const FString& Parameters)
{
    using namespace NexusLifecycle::Validation;
    const FString Section = TEXT("Nexus.Issues10.GeneratedOverride");
    FConfigFile* Engine = GConfig->FindConfigFile(GEngineIni);
    const FConfigFile Before = *Engine;
    GConfig->SetString(*Section, TEXT("RenderFixture"), TEXT("generated-override"), GEngineIni);
    auto Expected = Configuration();
    const auto Entry = Expected->GetObjectField(TEXT("Engine"))->GetArrayField(Section)[0]->AsObject();
    TestEqual(TEXT("measurement reads the merged GConfig value"), Entry->GetStringField(TEXT("value")), FString(TEXT("generated-override")));
    GConfig->SetString(*Section, TEXT("RenderFixture"), TEXT("worker-default"), GEngineIni);
    FString Error;
    TestTrue(TEXT("all effective runtime categories are reproducible"), ValidateConfiguration(Expected, Error));
    ApplyConfiguration(Expected, EConfigurationPhase::Startup);
    FString Actual;
    GConfig->GetString(*Section, TEXT("RenderFixture"), Actual, GEngineIni);
    TestEqual(TEXT("bootstrap reproduces a source generated override"), Actual, FString(TEXT("generated-override")));
    *Engine = Before;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusEffectiveConsoleVariables,
    "Nexus.Issues10.Safety.EffectiveConsoleVariables",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusEffectiveConsoleVariables::RunTest(const FString& Parameters)
{
    using namespace NexusLifecycle::Validation;
    auto& Manager = IConsoleManager::Get();
    const TCHAR* Registered = TEXT("r.NexusIssues10.Registered");
    const TCHAR* Deferred = TEXT("r.NexusIssues10.Deferred");
    IConsoleVariable* Variable = Manager.RegisterConsoleVariable(Registered, 1, TEXT("owned test variable"), ECVF_ReadOnly);
    auto Expected = MakeShared<FJsonObject>();
    Expected->SetStringField(Registered, TEXT("3"));
    Expected->SetStringField(Deferred, TEXT("7"));
    FString Error;
    TestTrue(TEXT("source runtime rendering variables are reproducible"), ValidateConsoleVariables(Expected, Error));
    ApplyConsoleVariables(Expected);
    TestEqual(TEXT("bootstrap sets an already registered startup variable"), Variable->GetInt(), 3);
    IConsoleVariable* Late = Manager.RegisterConsoleVariable(Deferred, 1, TEXT("owned deferred variable"), ECVF_ReadOnly);
    TestEqual(TEXT("engine registration preserves the early ini assignment"), Late->GetInt(), 7);
    const auto Actual = ConsoleVariables();
    TestEqual(TEXT("measurement reads real runtime values"), Actual->GetStringField(Deferred), FString(TEXT("7")));
    Manager.UnregisterConsoleObject(Registered, false);
    Manager.UnregisterConsoleObject(Deferred, false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRenderingLaunch,
    "Nexus.Issues10.Safety.RenderingLaunch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRenderingLaunch::RunTest(const FString& Parameters)
{
    const FString Before = FCommandLine::Get();
    FCommandLine::Set(TEXT("-d3d12 -sm6 -ResX=720 -RenderOffscreen -gpuvalidation"));
    const auto Actual = NexusLifecycle::Validation::RenderingLaunch();
    TestEqual(TEXT("feature level, resolution and render surface are bound"), Actual->GetArrayField(TEXT("arguments")).Num(), 3);
    TestEqual(TEXT("unsupported render launch option is explicit"), Actual->GetArrayField(TEXT("unsupported"))[0]->AsString(), FString(TEXT("-gpuvalidation")));
    FCommandLine::Set(*Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusConsoleReplayCallbacks,
    "Nexus.Issues10.Safety.ConsoleReplayDefersCallbacks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusConsoleReplayCallbacks::RunTest(const FString& Parameters)
{
    using namespace NexusLifecycle::Validation;
    auto& Manager = IConsoleManager::Get();
    const TCHAR* Name = TEXT("r.NexusIssues10.BootstrapCallback");
    auto* Variable = Manager.RegisterConsoleVariable(Name, 1, TEXT("owned bootstrap callback probe"), ECVF_ReadOnly);
    int32 Callbacks = 0;
    Variable->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([&Callbacks](IConsoleVariable*)
    {
        ++Callbacks;
    }));
    auto Expected = MakeShared<FJsonObject>();
    Expected->SetStringField(Name, TEXT("3"));
    const auto Pending = ApplyConsoleVariables(Expected);
    TestEqual(TEXT("early bootstrap installs the measured value"), Variable->GetInt(), 3);
    TestEqual(TEXT("early bootstrap must not enter initialized-engine callbacks"), Callbacks, 0);
    TestEqual(TEXT("early value retains its priority through engine configuration"), Variable->GetFlags() & ECVF_SetByMask,
        static_cast<uint32>(ECVF_SetByConsole));
    FString Error;
    TestTrue(TEXT("engine-ready completion executes the deferred change"), CompleteConsoleVariables(Expected, Pending, Error));
    TestEqual(TEXT("the normal callback executes exactly once at completion"), Callbacks, 1);
    TestEqual(TEXT("completion retains the measured value"), Variable->GetInt(), 3);
    Manager.UnregisterConsoleObject(Name, false);
    return true;
}
#endif
