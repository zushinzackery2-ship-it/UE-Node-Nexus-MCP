#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../Control/State.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusSlowTaskProgress,
    "Nexus.Startup.SlowTaskIsProgress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNexusSlowTaskProgress::RunTest(const FString& Parameters)
{
    auto& Slate = FSlateApplication::Get();
    {
        auto Progress = SNew(SWindow)
            .Title(FText::FromString(TEXT("Nexus progress classification fixture")))
            .ActivationPolicy(EWindowActivationPolicy::Never)
            .FocusWhenFirstShown(false);
        // FeedbackContextEditor uses this Slate path with bSlowTaskWindow=true.
        // Automation replaces GWarn, so FScopedSlowTask cannot open editor UI.
        Slate.AddModalWindow(Progress, nullptr, true);
        TestTrue(TEXT("Slate registers its real nonblocking slow-task window"), Slate.GetActiveModalWindow() == Progress);
        Slate.Tick();
        TestFalse(TEXT("a slow task does not request a user decision"), NexusLifecycle::OpenDialog().IsValid());
        TestFalse(TEXT("progress is not waiting for user"), NexusLifecycle::Snapshot()->GetBoolField(TEXT("waiting_for_user")));
        auto Decision = SNew(SWindow)
            .Title(FText::FromString(TEXT("Nexus decision during progress fixture")))
            .ActivationPolicy(EWindowActivationPolicy::Never)
            .FocusWhenFirstShown(false);
        bool bObservedDecision = false;
        int32 Loops = 0;
        const auto Handle = Slate.GetOnModalLoopTickEvent().AddLambda([this, Decision, &bObservedDecision, &Loops](float)
        {
            if (++Loops < 2)
            {
                return;
            }
            TestTrue(TEXT("a real decision during progress waits for its user"),
                NexusLifecycle::Snapshot()->GetBoolField(TEXT("waiting_for_user")));
            bObservedDecision = true;
            FSlateApplication::Get().RequestDestroyWindow(Decision);
        });
        Slate.AddModalWindow(Decision, Progress);
        Slate.GetOnModalLoopTickEvent().Remove(Handle);
        Slate.Tick();
        TestTrue(TEXT("the real decision loop was observed"), bObservedDecision);
        TestTrue(TEXT("closing the decision restores its progress parent"), Slate.GetActiveModalWindow() == Progress);
        TestFalse(TEXT("returning to progress clears the user decision"), NexusLifecycle::OpenDialog().IsValid());
        Slate.RequestDestroyWindow(Progress);
    }
    Slate.Tick();
    TestFalse(TEXT("progress window closes with its owner"), NexusLifecycle::OpenDialog().IsValid());
    return true;
}
#endif
