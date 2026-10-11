#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../Control/State.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ScopeLock.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNormalFrameReadiness,
    "Nexus.Issues7.Startup.NormalFrameRequired",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNormalFrameReadiness::RunTest(const FString& Parameters)
{
    using namespace NexusLifecycle;
    FInspector Inspector;
    TSharedPtr<FJsonObject> Editor;
    bool bReady;
    {
        FScopeLock Lock(&State().Mutex);
        Inspector = State().Inspector;
        Editor = State().Editor;
        bReady = State().bReady;
    }
    Attach([](const TArray<FString>&, bool, const FSaveGuard&)
    {
        return MakeShared<FJsonObject>();
    });
    TestFalse(TEXT("attaching core does not make an initializing editor ready"), Snapshot()->GetBoolField(TEXT("ready")));
    auto Initializing = MakeShared<FJsonObject>();
    Initializing->SetBoolField(TEXT("normal_tick"), false);
    Publish(Initializing);
    TestFalse(TEXT("a startup modal tick cannot count as a normal frame"), Snapshot()->GetBoolField(TEXT("ready")));
    auto Running = MakeShared<FJsonObject>();
    Running->SetBoolField(TEXT("normal_tick"), true);
    Publish(Running);
    TestTrue(TEXT("a normal frame makes the attached core ready"), Snapshot()->GetBoolField(TEXT("ready")));
    {
        FScopeLock Lock(&State().Mutex);
        State().Inspector = MoveTemp(Inspector);
        State().Editor = Editor;
        State().bReady = bReady;
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusSlateModalReadiness,
    "Nexus.Issues7.Startup.SlateModalReadiness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNexusSlateModalReadiness::RunTest(const FString& Parameters)
{
    auto& Slate = FSlateApplication::Get();
    auto Window = SNew(SWindow)
        .Title(FText::FromString(TEXT("Nexus recovery modal fixture")))
        .SizingRule(ESizingRule::Autosized)
        [
            SNew(STextBlock).Text(FText::FromString(TEXT("Recovery requires an editor UI choice")))
        ];
    bool bObserved = false;
    int32 Loops = 0;
    const auto Handle = Slate.GetOnModalLoopTickEvent().AddLambda([this, Window, &bObserved, &Loops](float)
    {
        // Multicast listeners have no ordering guarantee. Read the cache after
        // the first complete modal broadcast has notified the Guard observer.
        if (++Loops == 1)
        {
            return;
        }
        TestTrue(TEXT("the fixture is inside a real Slate modal loop"), FSlateApplication::Get().GetActiveModalWindow() == Window);
        const auto Status = NexusLifecycle::Snapshot();
        TestTrue(TEXT("Slate modal waits for an editor UI choice"), Status->GetBoolField(TEXT("waiting_for_user")));
        TestFalse(TEXT("an active Slate modal prevents readiness"), Status->GetBoolField(TEXT("ready")));
        const auto Dialog = NexusLifecycle::OpenDialog();
        if (TestTrue(TEXT("blocking modal has an explicit record"), Dialog.IsValid()))
        {
            TestEqual(TEXT("source identifies the Slate modal"), Dialog->GetStringField(TEXT("source")), FString(TEXT("slate_modal")));
            TestEqual(TEXT("title is retained"), Dialog->GetStringField(TEXT("title")), FString(TEXT("Nexus recovery modal fixture")));
            TestFalse(TEXT("native handle is recorded"), Dialog->GetStringField(TEXT("native_handle")).IsEmpty());
        }
        bObserved = true;
        FSlateApplication::Get().RequestDestroyWindow(Window);
    });
    Slate.AddModalWindow(Window, nullptr);
    Slate.GetOnModalLoopTickEvent().Remove(Handle);
    Slate.Tick();
    TestTrue(TEXT("the real modal loop was observed"), bObserved);
    TestFalse(TEXT("closing the modal clears the blocking state"), NexusLifecycle::OpenDialog().IsValid());
    TestTrue(TEXT("normal-frame readiness resumes after closing"), NexusLifecycle::Snapshot()->GetBoolField(TEXT("ready")));
    return true;
}
#endif
