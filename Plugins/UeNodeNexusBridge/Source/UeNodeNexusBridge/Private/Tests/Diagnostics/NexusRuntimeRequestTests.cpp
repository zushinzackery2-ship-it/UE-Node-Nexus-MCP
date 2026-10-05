#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "UeNodeNexusBridgeOperations.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRuntimeRequestValidation,
    "Nexus.Diagnostics.Runtime.RequestBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRuntimeRequestValidation::RunTest(const FString& Parameters)
{
    const auto Reject = [this](const TCHAR* Field, const TSharedPtr<FJsonValue>& Value)
    {
        auto Payload = MakeShared<FJsonObject>();
        Payload->SetField(Field, Value);
        auto Response = UeNodeNexusBridge::HandleDiagnosticsGet(TEXT("diagnostics_get"), TEXT("bounds"), Payload);
        TestFalse(Field, Response->GetBoolField(TEXT("ok")));
        TestEqual(TEXT("bad inputs are rejected before inspection"), Response->GetObjectField(TEXT("error"))->GetStringField(TEXT("code")), FString(TEXT("invalid_request")));
    };
    Reject(TEXT("cursor"), MakeShared<FJsonValueNumber>(-1));
    Reject(TEXT("cursor"), MakeShared<FJsonValueNumber>(1.5));
    Reject(TEXT("cursor"), MakeShared<FJsonValueNumber>(1e20));
    Reject(TEXT("limit"), MakeShared<FJsonValueNumber>(0));
    Reject(TEXT("limit"), MakeShared<FJsonValueNumber>(2001));
    Reject(TEXT("include_assets"), MakeShared<FJsonValueString>(TEXT("false")));
    Reject(TEXT("severity"), MakeShared<FJsonValueString>(TEXT("unknown")));
    Reject(TEXT("session_id"), MakeShared<FJsonValueBoolean>(true));
    auto Payload = MakeShared<FJsonObject>();
    Payload->SetBoolField(TEXT("include_assets"), false);
    Payload->SetStringField(TEXT("session_id"), TEXT("expired-session"));
    auto Response = UeNodeNexusBridge::HandleDiagnosticsGet(TEXT("diagnostics_get"), TEXT("session"), Payload);
    TestEqual(TEXT("expired session cannot claim zero current errors"), Response->GetObjectField(TEXT("error"))->GetStringField(TEXT("code")), FString(TEXT("runtime_session_not_found")));
    return true;
}
#endif
