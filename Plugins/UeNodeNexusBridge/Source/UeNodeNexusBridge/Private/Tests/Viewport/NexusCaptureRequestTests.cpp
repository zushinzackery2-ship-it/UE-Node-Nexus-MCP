#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Transport/UeNodeNexusBridgeRequestDispatch.h"
#include "Viewport/Capture/NexusCapture.h"

using namespace UeNodeNexusBridge;

static FString CaptureViolationField(const TSharedPtr<FJsonValue>& Value, const TCHAR* Field)
{
    TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetField(Field, Value);
    Capture::FRequest Request;
    FString Message;
    const TSharedPtr<FJsonObject> Violation = Capture::ReadRequest(*Payload, Request, Message);
    return Violation.IsValid() ? Violation->GetStringField(TEXT("field")) : FString();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusCaptureRequestFields,
    "Nexus.Issues21.Viewport.CaptureRequestFields",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusCaptureRequestFields::RunTest(const FString& Parameters)
{
    const FString Names[] =
    {
        TEXT("HeadV5_Before.png"), TEXT("Shots/HeadV5"), TEXT("C:\\Shots\\HeadV5"), FString(), TEXT("two words"),
        FString(TEXT("\u622A\u56FE")), FString::ChrN(Capture::MaxScreenshotNameLength + 1, TEXT('a'))
    };
    for (const FString& Name : Names)
    {
        TestEqual(*FString::Printf(TEXT("name '%s'"), *Name),
            CaptureViolationField(MakeShared<FJsonValueString>(Name), TEXT("filename")), FString(TEXT("filename")));
    }
    TestEqual(TEXT("numeric name"), CaptureViolationField(MakeShared<FJsonValueNumber>(7), TEXT("filename")), FString(TEXT("filename")));
    TestEqual(TEXT("unknown target"), CaptureViolationField(MakeShared<FJsonValueString>(TEXT("PIE")), TEXT("target")), FString(TEXT("target")));
    TestEqual(TEXT("null target"), CaptureViolationField(MakeShared<FJsonValueNull>(), TEXT("target")), FString(TEXT("target")));
    TestEqual(TEXT("short timeout"), CaptureViolationField(MakeShared<FJsonValueNumber>(0.5), TEXT("timeout_seconds")), FString(TEXT("timeout_seconds")));
    TestEqual(TEXT("long timeout"), CaptureViolationField(MakeShared<FJsonValueNumber>(121), TEXT("timeout_seconds")), FString(TEXT("timeout_seconds")));
    TestEqual(TEXT("text timeout"), CaptureViolationField(MakeShared<FJsonValueString>(TEXT("30")), TEXT("timeout_seconds")), FString(TEXT("timeout_seconds")));
    TestEqual(TEXT("numeric show_ui"), CaptureViolationField(MakeShared<FJsonValueNumber>(1), TEXT("show_ui")), FString(TEXT("show_ui")));
    TestEqual(TEXT("text dry_run"), CaptureViolationField(MakeShared<FJsonValueString>(TEXT("true")), TEXT("dry_run")), FString(TEXT("dry_run")));
    TestEqual(TEXT("null name keeps the default"), CaptureViolationField(MakeShared<FJsonValueNull>(), TEXT("filename")), FString());

    TSharedRef<FJsonObject> Valid = MakeShared<FJsonObject>();
    Valid->SetStringField(TEXT("filename"), FString::ChrN(Capture::MaxScreenshotNameLength, TEXT('z')));
    Valid->SetStringField(TEXT("target"), TEXT("pie"));
    Valid->SetNumberField(TEXT("timeout_seconds"), Capture::MaxCaptureTimeout);
    Valid->SetBoolField(TEXT("show_ui"), true);
    Valid->SetBoolField(TEXT("dry_run"), true);
    Capture::FRequest Request;
    FString Message;
    TestFalse(TEXT("valid payload accepted"), Capture::ReadRequest(*Valid, Request, Message).IsValid());
    TestEqual(TEXT("name"), Request.Name.Len(), Capture::MaxScreenshotNameLength);
    TestEqual(TEXT("target"), Request.Target, FString(TEXT("pie")));
    TestEqual(TEXT("timeout"), Request.Timeout, Capture::MaxCaptureTimeout);
    TestTrue(TEXT("flags"), Request.bShowUi && Request.bDryRun);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusCaptureRequestDispatch,
    "Nexus.Issues21.Viewport.CaptureRequestDispatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusCaptureRequestDispatch::RunTest(const FString& Parameters)
{
    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("filename"), TEXT("HeadV5_Before.png"));
    TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("operation"), TEXT("viewport_capture"));
    Request->SetStringField(TEXT("request_id"), TEXT("nexus-capture-request-field"));
    Request->SetObjectField(TEXT("payload"), Payload);
    TSharedPtr<FJsonObject> Response;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(DispatchParsedRequest(Request)), Response);
    const TSharedPtr<FJsonObject>* Error = nullptr;
    if (!Response.IsValid() || !Response->TryGetObjectField(TEXT("error"), Error))
    {
        AddError(TEXT("viewport_capture with an extension returned no error"));
        return false;
    }
    TestEqual(TEXT("code"), (*Error)->GetStringField(TEXT("code")), FString(TEXT("invalid_request")));
    const TSharedPtr<FJsonObject> Details = (*Error)->GetObjectField(TEXT("details"));
    TestEqual(TEXT("field"), Details->GetStringField(TEXT("field")), FString(TEXT("filename")));
    TestEqual(TEXT("reason"), Details->GetStringField(TEXT("reason")), FString(TEXT("extension")));
    TestEqual(TEXT("value"), Details->GetStringField(TEXT("value")), FString(TEXT("HeadV5_Before.png")));
    return true;
}
#endif
