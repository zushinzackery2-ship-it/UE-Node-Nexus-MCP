#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Safety/Isolation/NexusSafetyBaseline.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusBaselineLeaseIdentity,
    "Nexus.Safety.BaselineLeaseIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusBaselineLeaseIdentity::RunTest(const FString& Parameters)
{
    auto Payload = MakeShared<FJsonObject>();
    const TArray<TSharedPtr<FJsonValue>> Paths
    {
        MakeShared<FJsonValueString>(TEXT("/Game/NexusBaselineLeaseProbe"))
    };
    Payload->SetArrayField(TEXT("asset_paths"), Paths);
    Payload->SetBoolField(TEXT("dry_run"), true);
    Payload->SetStringField(TEXT("baseline_id"), TEXT("../outside"));
    auto Rejected = UeNodeNexusBridge::Safety::ExportBaseline(TEXT("safety_baseline_export"), TEXT("lease-test"), Payload);
    TestFalse(TEXT("path traversal rejected"), Rejected->GetBoolField(TEXT("ok")));
    TestEqual(TEXT("identity error"), Rejected->GetObjectField(TEXT("error"))->GetStringField(TEXT("code")),
        FString(TEXT("isolation_baseline_identity")));

    const FString Identifier = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Payload->SetStringField(TEXT("baseline_id"), Identifier);
    auto Preview = UeNodeNexusBridge::Safety::ExportBaseline(TEXT("safety_baseline_export"), TEXT("lease-test"), Payload);
    if (!TestTrue(TEXT("owned preview succeeds"), Preview->GetBoolField(TEXT("ok"))))
    {
        return false;
    }
    const FString Root = Preview->GetObjectField(TEXT("data"))->GetStringField(TEXT("root"));
    TestTrue(TEXT("exact caller ID retained"), Root.EndsWith(Identifier));
    TestFalse(TEXT("preview creates no payload"), IFileManager::Get().DirectoryExists(*Root));
    IFileManager::Get().MakeDirectory(*Root, true);
    Payload->SetBoolField(TEXT("dry_run"), false);
    Rejected = UeNodeNexusBridge::Safety::ExportBaseline(TEXT("safety_baseline_export"), TEXT("lease-test"), Payload);
    TestFalse(TEXT("existing baseline cannot be overwritten"), Rejected->GetBoolField(TEXT("ok")));
    return true;
}

#endif
