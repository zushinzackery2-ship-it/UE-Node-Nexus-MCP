#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/CoreDelegates.h"
#include "NexusLifecycle.h"

namespace
{
TSharedPtr<FJsonObject> LastNotice(const TSharedPtr<FJsonObject>& Status)
{
    const TArray<TSharedPtr<FJsonValue>>* Notices = nullptr;
    if (!Status.IsValid() || !Status->TryGetArrayField(TEXT("dialog_notices"), Notices) || Notices->IsEmpty())
    {
        return nullptr;
    }
    return Notices->Last()->AsObject();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusStartupAdvisory, "Nexus.Issues4.StartupAdvisoryAcknowledged", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusStartupAdvisory::RunTest(const FString& Parameters)
{
    const TSharedPtr<FJsonObject> Before = NexusLifecycle::Snapshot();
    const TSharedPtr<FJsonObject>* Startup = nullptr;
    if (!TestTrue(TEXT("status reports startup progress"), Before->TryGetObjectField(TEXT("startup_progress"), Startup)))
    {
        return false;
    }
    // The automation host runs without a window (-nullrhi): nobody can see an
    // advisory it opens, so the Guard is the one that confirms it.
    TestEqual(TEXT("launch window"), (*Startup)->GetStringField(TEXT("launch_window")), FString(TEXT("offscreen")));
    TestEqual(TEXT("phase once the engine loop runs"), (*Startup)->GetStringField(TEXT("phase")), FString(TEXT("loop_init_complete")));
    TestTrue(TEXT("latest log line is tracked"), (*Startup)->HasTypedField<EJson::Object>(TEXT("last_log")));
    if (!TestTrue(TEXT("editor dialog handler is bound"), FCoreDelegates::ModalMessageDialog.IsBound()))
    {
        return false;
    }

    // The exact dialog UUnrealEdEngine::ValidateFreeDiskSpace opens; its text key,
    // not its localized title, identifies it.
    const FText Title = NSLOCTEXT("DriveSpaceDialog", "LowHardDriveSpaceMsgTitle", "Warning: Low Drive Space");
    const FText Message = NSLOCTEXT("DriveSpaceDialog", "LowHardDriveSpaceMsgHeader",
        "The following locations have limited free space. To avoid potential problems, please consider freeing up at least the amounts recommended below.");
    const EAppReturnType::Type Result = FCoreDelegates::ModalMessageDialog.Execute(EAppMsgCategory::Warning, EAppMsgType::Ok, Message, Title);
    TestTrue(TEXT("the advisory is confirmed without a user"), Result == EAppReturnType::Ok);

    const TSharedPtr<FJsonObject> After = NexusLifecycle::Snapshot();
    const TSharedPtr<FJsonObject> Notice = LastNotice(After);
    if (!TestTrue(TEXT("the advisory is recorded"), Notice.IsValid()))
    {
        return false;
    }
    TestEqual(TEXT("code"), Notice->GetStringField(TEXT("code")), FString(TEXT("low_drive_space")));
    TestEqual(TEXT("resolution"), Notice->GetStringField(TEXT("resolution")), FString(TEXT("auto_acknowledged")));
    TestEqual(TEXT("result"), Notice->GetStringField(TEXT("result")), FString(LexToString(EAppReturnType::Ok)));
    TestEqual(TEXT("title"), Notice->GetStringField(TEXT("title")), Title.ToString());
    const TArray<TSharedPtr<FJsonValue>>* Locations = nullptr;
    if (TestTrue(TEXT("locations are recorded"), Notice->TryGetArrayField(TEXT("locations"), Locations)))
    {
        for (const TSharedPtr<FJsonValue>& Value : *Locations)
        {
            const TSharedPtr<FJsonObject> Location = Value->AsObject();
            TestFalse(TEXT("location role"), Location->GetStringField(TEXT("role")).IsEmpty());
            TestTrue(TEXT("a reported location is below its recommendation"),
                Location->GetNumberField(TEXT("free_mb")) < Location->GetNumberField(TEXT("recommended_mb")));
        }
    }
    TestFalse(TEXT("nothing is left waiting"), After->GetBoolField(TEXT("waiting_for_user")));
    TestFalse(TEXT("no blocking dialog"), After->HasField(TEXT("blocking_dialog")));
    TestFalse(TEXT("requests are admitted"), NexusLifecycle::OpenDialog().IsValid());
    return true;
}

#endif
