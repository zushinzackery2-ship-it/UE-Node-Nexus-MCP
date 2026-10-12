#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Control/State.h"
#include "Misc/ScopeLock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusCloseCommitReadiness,
    "Nexus.Safety.CloseCommitReadiness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusCloseCommitReadiness::RunTest(const FString& Parameters)
{
    using namespace NexusLifecycle;
    auto& S = State();
    FScopeLock Lock(&S.Mutex);
    TGuardValue<bool> Ready(S.bReady, false);
    TGuardValue<bool> Draining(S.bDraining, true);
    TGuardValue<bool> Stopping(S.bStopping, false);
    TGuardValue<bool> Queued(S.bCommitQueued, true);
    TGuardValue<FString> CloseId(S.CloseId, TEXT("readiness-test"));
    TGuardValue<TSharedPtr<FJsonObject>> Prepared(S.Prepared, MakeShared<FJsonObject>());
    auto Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("close_id"), S.CloseId);
    Payload->SetNumberField(TEXT("close_revision"), S.Revision);
    const auto Result = Close(TEXT("commit_close"), Payload);
    TestTrue(TEXT("prepared close remains committable while readiness changes"), Result->GetBoolField(TEXT("ok")));
    Payload->SetNumberField(TEXT("close_revision"), S.Revision + 1);
    TestFalse(TEXT("stale revision is still refused"), Close(TEXT("commit_close"), Payload)->GetBoolField(TEXT("ok")));
    return true;
}
#endif
