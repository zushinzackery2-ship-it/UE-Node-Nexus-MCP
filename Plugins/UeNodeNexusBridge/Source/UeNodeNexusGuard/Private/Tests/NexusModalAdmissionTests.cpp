#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../Control/State.h"
#include "../Identity/Identity.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusModalScopeAdmission,
    "Nexus.Issues10.Lifecycle.ModalScopeAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNexusModalScopeAdmission::RunTest(const FString& Parameters)
{
    using namespace NexusLifecycle;
    const uint32 Peer = FPlatformProcess::GetCurrentProcessId();
    const auto Identity = ProcessIdentity(Peer);
    TSharedPtr<FJsonObject> Manager;
    FString Secret;
    uint64 Epoch;
    const auto Payload = MakeShared<FJsonObject>();
    {
        FScopeLock Lock(&State().Mutex);
        Manager = State().Manager;
        Secret = State().ManagerSecret;
        Epoch = State().ManagerEpoch;
        State().Manager = Identity;
        State().ManagerSecret = TEXT("owned-modal-test");
        State().ManagerEpoch = Epoch + 1;
        Payload->SetStringField(TEXT("instance_id"), State().Identity->GetStringField(TEXT("instance_id")));
    }
    Payload->SetObjectField(TEXT("manager_identity"), Identity);
    Payload->SetNumberField(TEXT("manager_epoch"), static_cast<double>(Epoch + 1));
    Payload->SetStringField(TEXT("manager_secret"), TEXT("owned-modal-test"));
    Payload->SetStringField(TEXT("scope_id"), TEXT("owned-modal-scope"));
    Payload->SetStringField(TEXT("client_session_id"), TEXT("owned-modal-client"));
    Payload->SetStringField(TEXT("lease_id"), TEXT("owned-modal-lease"));
    Payload->SetObjectField(TEXT("identity"), Identity);
    Payload->SetBoolField(TEXT("exclusive"), false);
    const auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("operation"), TEXT("scope_grant"));
    Request->SetObjectField(TEXT("payload"), Payload);
    auto& Slate = FSlateApplication::Get();
    const auto Window = SNew(SWindow).Title(FText::FromString(TEXT("Nexus admission choice")))
        [SNew(STextBlock).Text(FText::FromString(TEXT("Choose an editor action")))];
    int32 Loops = 0;
    bool bObserved = false;
    const auto Handle = Slate.GetOnModalLoopTickEvent().AddLambda([this, Window, Request, Peer, &Loops, &bObserved](float)
    {
        if (++Loops < 2)
        {
            return;
        }
        const auto Response = Dispatch(Request, Peer);
        TestFalse(TEXT("modal scope request is rejected"), Response->GetBoolField(TEXT("ok")));
        const auto Error = Response->GetObjectField(TEXT("error"));
        TestEqual(TEXT("authoritative producer preserves prompt classification"), Error->GetStringField(TEXT("code")), FString(TEXT("waiting_for_user")));
        const auto Dialog = Error->GetObjectField(TEXT("details"))->GetObjectField(TEXT("blocking_dialog"));
        TestEqual(TEXT("admission carries actionable prompt details"), Dialog->GetStringField(TEXT("title")), FString(TEXT("Nexus admission choice")));
        bObserved = true;
        FSlateApplication::Get().RequestDestroyWindow(Window);
    });
    Slate.AddModalWindow(Window, nullptr);
    Slate.GetOnModalLoopTickEvent().Remove(Handle);
    Slate.Tick();
    TestTrue(TEXT("the real modal admission path was observed"), bObserved);
    TestTrue(TEXT("scope admission resumes after the modal clears"), Dispatch(Request, Peer)->GetBoolField(TEXT("ok")));
    {
        FScopeLock Lock(&State().Mutex);
        State().Grants.Remove(TEXT("owned-modal-scope"));
        State().Manager = Manager;
        State().ManagerSecret = Secret;
        State().ManagerEpoch = Epoch;
    }
    return true;
}
#endif
