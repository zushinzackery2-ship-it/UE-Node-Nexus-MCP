#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Diagnostics/Runtime/NexusRuntimeDiagnostics.h"
#include "Logging/MessageLog.h"
#include "UeNodeNexusBridgeOperations.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRuntimeStore,
    "Nexus.Diagnostics.Runtime.CursorSessionsUnicodeBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRuntimeStore::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::RuntimeDiagnostics;
    BeginSession(true);
    FString FirstId = Snapshot()->GetStringField(TEXT("session_id"));
    for (const FString Text : TArray<FString>
        {
            TEXT("第一个错误"), TEXT("第二个错误"), TEXT("第一个错误")
        })
    {
        FMessageLog Log(TEXT("PIE"));
        Log.SuppressLoggingToOutputLog().Error(FText::FromString(Text));
    }
    auto Payload = MakeShared<FJsonObject>();
    auto Data = Read(Payload);
    TestEqual(TEXT("distinct Unicode messages do not collide"), Data->GetArrayField(TEXT("items")).Num(), 2);
    TestEqual(TEXT("all occurrences counted"), Data->GetIntegerField(TEXT("error_count")), 3);
    Payload->SetNumberField(TEXT("cursor"), Data->GetNumberField(TEXT("next_cursor")));
    TestEqual(TEXT("incremental read has no unchanged items"), Read(Payload)->GetArrayField(TEXT("items")).Num(), 0);
    Payload->SetBoolField(TEXT("include_assets"), false);
    auto Response = UeNodeNexusBridge::HandleDiagnosticsGet(TEXT("diagnostics_get"), TEXT("regression"), Payload);
    TestEqual(TEXT("consuming a cursor does not erase session error counts"), Response->GetObjectField(TEXT("data"))->GetIntegerField(TEXT("error_count")), 3);
    BeginSession(true);
    TestEqual(TEXT("new PIE session starts with zero errors"), Snapshot()->GetIntegerField(TEXT("error_count")), 0);
    Payload->SetStringField(TEXT("session_id"), FirstId);
    Payload->SetNumberField(TEXT("cursor"), 0);
    TestEqual(TEXT("previous session remains readable"), Read(Payload)->GetIntegerField(TEXT("error_count")), 3);
    Payload->RemoveField(TEXT("session_id"));
    Payload->SetNumberField(TEXT("limit"), 512);
    for (int32 Index = 0; Index < 700; ++Index)
    {
        auto Event = MakeShared<FJsonObject>();
        Event->SetStringField(TEXT("severity"), TEXT("error"));
        Event->SetStringField(TEXT("message"), FString::Printf(TEXT("event %d"), Index));
        Event->SetStringField(TEXT("source"), TEXT("regression"));
        Record(Event);
    }
    Data = Read(Payload);
    TestTrue(TEXT("ring reports lost history"), Data->GetBoolField(TEXT("cursor_gap")));
    TestEqual(TEXT("session count survives eviction"), Data->GetIntegerField(TEXT("error_count")), 700);
    TestTrue(TEXT("entry count is bounded"), Data->GetArrayField(TEXT("items")).Num() <= 512);
    TestTrue(TEXT("byte count is bounded"), Data->GetIntegerField(TEXT("retained_bytes")) <= 2 * 1024 * 1024);
    BeginSession(true);
    FirstId = Snapshot()->GetStringField(TEXT("session_id"));
    const FString Asset = TEXT("/Game/Test/ABP_Derived.ABP_Derived");
    for (int32 Index = 0; Index < 600; ++Index)
    {
        auto Event = MakeShared<FJsonObject>();
        Event->SetStringField(TEXT("severity"), TEXT("error"));
        Event->SetStringField(TEXT("asset_path"), Asset);
        Event->SetStringField(TEXT("message"), FString::Printf(TEXT("derived event %d"), Index));
        Record(Event);
    }
    EndSession();
    BeginSession(false);
    TestEqual(TEXT("PIE errors survive the return to the editor"), Snapshot()->GetIntegerField(TEXT("error_count")), 600);
    Payload->SetStringField(TEXT("asset_path"), Asset);
    Payload->SetNumberField(TEXT("cursor"), Snapshot()->GetNumberField(TEXT("next_cursor")));
    Data = Read(Payload);
    TestEqual(TEXT("asset total survives cursor and eviction"), Data->GetIntegerField(TEXT("matched_error_count")), 600);
    TestEqual(TEXT("asset delta can be empty while its total remains nonzero"), Data->GetArrayField(TEXT("items")).Num(), 0);
    Payload->SetStringField(TEXT("session_id"), Snapshot()->GetStringField(TEXT("current_session_id")));
    TestEqual(TEXT("editor session remains separately accessible"), Read(Payload)->GetIntegerField(TEXT("error_count")), 0);
    EndSession();
    BeginSession(false);
    return true;
}
#endif
