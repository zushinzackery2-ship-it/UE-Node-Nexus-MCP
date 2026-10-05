#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Diagnostics/Runtime/NexusRuntimeDiagnostics.h"
#include "Transport/UeNodeNexusBridgeRequestDispatch.h"
#include "UeNodeNexusBridgeJson.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonSerializer.h"

namespace
{
TSharedPtr<FJsonObject> NoticeEvent(const FString& Severity, const FString& Message)
{
    auto Event = MakeShared<FJsonObject>();
    Event->SetStringField(TEXT("severity"), Severity);
    Event->SetStringField(TEXT("message"), Message);
    Event->SetStringField(TEXT("source"), TEXT("notice-regression"));
    return Event;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRuntimeNotice,
    "Nexus.Diagnostics.Runtime.NotificationSeverityBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRuntimeNotice::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::RuntimeDiagnostics;
    BeginSession(true);
    for (int32 Index = 0; Index < 3; ++Index)
    {
        Record(NoticeEvent(TEXT("warning"), FString::Printf(TEXT("startup warning %d"), Index)));
    }
    const TArray<int32> Counts
    {
        2, 4, 297
    };
    for (int32 Count : Counts)
    {
        auto Event = NoticeEvent(TEXT("error"), FString::Printf(TEXT("Accessed None %d"), Count));
        for (int32 Index = 0; Index < Count; ++Index)
        {
            Record(Event);
        }
    }
    EndSession();
    auto Value = Snapshot();
    TestEqual(TEXT("all runtime occurrences visible"), Value->GetIntegerField(TEXT("error_count")), 303);
    const TArray<TSharedPtr<FJsonValue>>* Samples = nullptr;
    const bool bSamples = Value->TryGetArrayField(TEXT("samples"), Samples);
    TestTrue(TEXT("ordinary notices contain severe error examples"), bSamples);
    if (bSamples)
    {
        TestEqual(TEXT("only three bounded examples"), Samples->Num(), 3);
        for (const auto& Sample : *Samples)
        {
            TestEqual(TEXT("warnings cannot displace an error"), Sample->AsObject()->GetStringField(TEXT("severity")), FString(TEXT("error")));
        }
    }
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("operation"), TEXT("project_context_get"));
    Request->SetStringField(TEXT("request_id"), TEXT("busy-notice"));
    Request->SetObjectField(TEXT("payload"), MakeShared<FJsonObject>());
    {
        UeNodeNexusBridge::FBridgeWorkScope Scope(TEXT("owning-request"));
        TSharedPtr<FJsonObject> Response;
        const FString Text = UeNodeNexusBridge::DispatchParsedRequest(Request);
        TestTrue(TEXT("busy response parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Response));
        TestTrue(TEXT("busy response retains runtime notice"), Response && Response->HasTypedField<EJson::Object>(TEXT("runtime_diagnostics")));
    }
    BeginSession(true);
    auto Event = NoticeEvent(TEXT("error"), TEXT("repeated performance event"));
    const double RecordStart = FPlatformTime::Seconds();
    for (int32 Index = 0; Index < 10000; ++Index)
    {
        Record(Event);
    }
    const double RecordUs = (FPlatformTime::Seconds() - RecordStart) * 1000000 / 10000;
    const double SnapshotStart = FPlatformTime::Seconds();
    for (int32 Index = 0; Index < 1000; ++Index)
    {
        Value = Snapshot();
    }
    const double SnapshotUs = (FPlatformTime::Seconds() - SnapshotStart) * 1000000 / 1000;
    AddInfo(FString::Printf(TEXT("RuntimeNoticeBenchmark record_us=%.3f snapshot_us=%.3f bytes=%d"),
        RecordUs, SnapshotUs, UeNodeNexusBridge::SerializeJsonObjectToString(Value).Len() * sizeof(TCHAR)));
    TestEqual(TEXT("repeat count retained"), Value->GetIntegerField(TEXT("error_count")), 10000);
    EndSession();
    BeginSession(false);
    return true;
}
#endif
