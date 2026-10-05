#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Transport/Safety/NexusAsyncWork.h"
#include "Transport/UeNodeNexusBridgeRequestDispatch.h"
#include "Viewport/Capture/NexusCapture.h"
#include "UnrealClient.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusAsyncAdmission,
    "Nexus.Safety.AsyncAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusAsyncAdmission::RunTest(const FString& Parameters)
{
    const FString Id = TEXT("nexus-safety-async-test");
    TestTrue(TEXT("acquire async work"), Safety::BeginAsync(Id, TEXT("viewport_capture")));
    TestTrue(TEXT("save watchers see retained ownership"), IsBridgeRequestActive());
    TestFalse(TEXT("retained ownership is distinct from dispatch"), IsBridgeDispatchActive());
    TestFalse(TEXT("another async owner refused"), Safety::BeginAsync(TEXT("other"), TEXT("asset_compile")));
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("operation"), TEXT("project_context_get"));
    Request->SetStringField(TEXT("request_id"), TEXT("blocked-request"));
    Request->SetObjectField(TEXT("payload"), MakeShared<FJsonObject>());
    const FString Response = DispatchParsedRequest(Request);
    TestTrue(TEXT("actual dispatcher refuses until async work ends"), Response.Contains(TEXT("async_operation")));
    TestTrue(TEXT("request remains owned after refusal"), Safety::Holds(Id));
    Safety::FinishAsync(TEXT("other"), false, TEXT("not_owner"));
    TestTrue(TEXT("wrong owner cannot release work"), Safety::Holds(Id));
    Safety::FinishAsync(Id, true, FString());
    TestTrue(TEXT("admission available after finish"), Safety::AsyncBusyReason().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusExternalCaptureAdmission,
    "Nexus.Safety.ExternalScreenshotOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusExternalCaptureAdmission::RunTest(const FString& Parameters)
{
    if (FScreenshotRequest::IsScreenshotRequested())
    {
        AddError(TEXT("test requires an unoccupied screenshot request"));
        return false;
    }
    const bool Messages = GAreScreenMessagesEnabled;
    FScreenshotRequest::RequestScreenshot(TEXT("NexusExternalOwnership.png"), false, false);
    const FString Path = FScreenshotRequest::GetFilename();
    FString Error;
    auto Result = Capture::Submit(TEXT("nexus-rejected"), TEXT("test"), TEXT("level"), false, 10, false, Error);
    TestFalse(TEXT("external request blocks capture"), Result.IsValid());
    TestEqual(TEXT("explicit admission reason"), Error, FString(TEXT("capture_busy")));
    TestEqual(TEXT("external request identity preserved"), FScreenshotRequest::GetFilename(), Path);
    TestTrue(TEXT("external request remains pending"), FScreenshotRequest::IsScreenshotRequested());
    TestFalse(TEXT("rejected request owns no lifecycle"), Safety::Holds(TEXT("nexus-rejected")));
    FScreenshotRequest::Reset();
    GAreScreenMessagesEnabled = Messages;
    return true;
}
#endif
