#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Diagnostics/Compilation/NexusCompileFeedback.h"
#include "Misc/ScopedSlowTask.h"

namespace
{
class FFeedbackProbe final : public FFeedbackContext
{
public:
    int32 Reports = 0;
    int32 Messages = 0;
    void ForceProgress()
    {
        UpdateUI();
    }
    virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
    {
        ++Messages;
    }
protected:
    virtual void ProgressReported(float Progress, FText Message) override
    {
        ++Reports;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusCompileFeedback,
    "Nexus.Issues6.Material.CompileFeedback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusCompileFeedback::RunTest(const FString& Parameters)
{
    FFeedbackProbe Probe;
    TGuardValue<FFeedbackContext*> Previous(GWarn, &Probe);
    const bool bSlowBefore = GIsSlowTask;
    {
        FScopedSlowTask Outer(1);
        Probe.ForceProgress();
        const int32 ReportsBefore = Probe.Reports;
        {
            UeNodeNexusBridge::FCompileFeedback Scope(true);
            FScopedSlowTask Compile(1);
            Compile.EnterProgressFrame(1);
            GWarn->BeginSlowTask(FText::FromString(TEXT("compile wait")), false);
            GWarn->StatusForceUpdate(1, 1, FText::FromString(TEXT("compiled")));
            GWarn->EndSlowTask();
            GWarn->Serialize(TEXT("compiler diagnostic"), ELogVerbosity::Display, NAME_None);
            TestEqual(TEXT("editor progress callbacks are not reentered"), Probe.Reports, ReportsBefore);
            TestEqual(TEXT("diagnostics forwarded"), Probe.Messages, 1);
        }
        TestTrue(TEXT("feedback restored"), GWarn == &Probe);
        TestEqual(TEXT("outer scope retained"), Probe.GetScopeStack().Num(), 1);
        Probe.ForceProgress();
        TestTrue(TEXT("outer progress resumes"), Probe.Reports > ReportsBefore);
    }
    TestEqual(TEXT("slow task flag retained"), GIsSlowTask, bSlowBefore);
    return true;
}

#endif
