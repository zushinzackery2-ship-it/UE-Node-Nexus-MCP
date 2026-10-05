#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Diagnostics/Runtime/NexusRuntimeDiagnostics.h"
#include "Diagnostics/Runtime/Validation/Verdict.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRuntimeCoverage,
    "Nexus.Diagnostics.Runtime.ExplicitWindowCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRuntimeCoverage::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    RuntimeDiagnostics::BeginSession(true);
    auto Snapshot = RuntimeDiagnostics::Snapshot();
    const FString Id = Snapshot->GetStringField(TEXT("session_id"));
    TestFalse(TEXT("an active window cannot pass"), RuntimeSmoke::Evaluate(Snapshot, Id)->GetBoolField(TEXT("passed")));
    RuntimeDiagnostics::EndSession();
    Snapshot = RuntimeDiagnostics::Snapshot();
    TestTrue(TEXT("completed observed window passes"), RuntimeSmoke::Evaluate(Snapshot, Id)->GetBoolField(TEXT("passed")));
    TestFalse(TEXT("a different window cannot pass"), RuntimeSmoke::Evaluate(Snapshot, TEXT("another"))->GetBoolField(TEXT("passed")));
    Snapshot->RemoveField(TEXT("error_count"));
    TestFalse(TEXT("missing required counts cannot pass"), RuntimeSmoke::Evaluate(Snapshot, Id)->GetBoolField(TEXT("passed")));
    RuntimeDiagnostics::BeginSession(true);
    RuntimeDiagnostics::SetCaptureReady(false);
    RuntimeDiagnostics::SetCaptureReady(true);
    RuntimeDiagnostics::EndSession();
    Snapshot = RuntimeDiagnostics::Snapshot();
    TestFalse(TEXT("a collection gap remains incomplete after collection resumes"), Snapshot->GetBoolField(TEXT("sources_complete")));
    TestFalse(TEXT("an incomplete window cannot pass"), RuntimeSmoke::Evaluate(Snapshot, Snapshot->GetStringField(TEXT("session_id")))->GetBoolField(TEXT("passed")));
    RuntimeDiagnostics::BeginSession(true);
    TArray<TFuture<void>> Workers;
    for (int32 Worker = 0; Worker < 4; ++Worker)
    {
        Workers.Add(Async(EAsyncExecution::ThreadPool, [Worker]()
        {
            auto Event = MakeShared<FJsonObject>();
            Event->SetStringField(TEXT("severity"), TEXT("error"));
            Event->SetStringField(TEXT("message"), FString::Printf(TEXT("worker %d"), Worker));
            for (int32 Index = 0; Index < 1000; ++Index)
            {
                RuntimeDiagnostics::Record(Event);
                RuntimeDiagnostics::Snapshot();
            }
        }));
    }
    for (auto& Worker : Workers)
    {
        Worker.Wait();
    }
    Snapshot = RuntimeDiagnostics::Snapshot();
    TestEqual(TEXT("concurrent record/read keeps all counts"), Snapshot->GetIntegerField(TEXT("error_count")), 4000);
    TestEqual(TEXT("concurrent notices retain bounded examples"), Snapshot->GetArrayField(TEXT("samples")).Num(), 3);
    for (int32 Index = 0; Index < 700; ++Index)
    {
        auto Event = MakeShared<FJsonObject>();
        Event->SetStringField(TEXT("severity"), TEXT("error"));
        Event->SetStringField(TEXT("message"), FString::Printf(TEXT("eviction %d"), Index));
        RuntimeDiagnostics::Record(Event);
    }
    Snapshot = RuntimeDiagnostics::Snapshot();
    TestTrue(TEXT("lost events are explicitly reported"), Snapshot->GetIntegerField(TEXT("dropped_count")) > 0);
    TestEqual(TEXT("samples survive eviction without retaining extra entries"), Snapshot->GetArrayField(TEXT("samples")).Num(), 3);
    RuntimeDiagnostics::EndSession();
    RuntimeDiagnostics::BeginSession(false);
    return true;
}
#endif
