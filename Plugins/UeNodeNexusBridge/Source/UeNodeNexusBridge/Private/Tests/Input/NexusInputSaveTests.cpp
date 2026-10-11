#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Transcode/Commit/NexusPackageFiles.h"
#include "UeNodeNexusBridgeOperations.h"
#include "UeNodeNexusBridgeSaveResult.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusInputSaveFailure,
    "Nexus.Issues10.Input.SaveFailureAndDirtyRetry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusInputSaveFailure::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    const FString Prefix = TEXT("/Game/NexusSave_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Action = NewObject<UInputAction>(CreatePackage(*(Prefix + TEXT("Action"))), TEXT("Action"), RF_Public | RF_Standalone);
    auto* Context = NewObject<UInputMappingContext>(CreatePackage(*(Prefix + TEXT("Context"))), TEXT("Context"), RF_Public | RF_Standalone);
    const FString File = Collaboration::PackageFile(Context->GetOutermost());
    ON_SCOPE_EXIT
    {
        FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*File, false);
        UObject* Assets[]
        {
            Action, Context
        };
        for (UObject* Asset : Assets)
        {
            auto* Package = Asset->GetOutermost();
            ResetLoaders(Package);
            IFileManager::Get().Delete(*Collaboration::PackageFile(Package));
            Package->SetDirtyFlag(false);
            Asset->ClearFlags(RF_Public | RF_Standalone);
        }
    };
    FString Error;
    if (!TestTrue(TEXT("save fixture action"), Transcode::SavePackageDirect(Action, Error))
        || !TestTrue(TEXT("save fixture context"), Transcode::SavePackageDirect(Context, Error))
        || !TestTrue(TEXT("make fixture read-only"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*File, true)))
    {
        AddError(Error);
        return false;
    }
    const FString Before = Collaboration::FileHash(File);
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("asset_path"), Context->GetPathName());
    Request->SetStringField(TEXT("action_path"), Action->GetPathName());
    Request->SetStringField(TEXT("key"), TEXT("W"));
    Request->SetBoolField(TEXT("dry_run"), false);
    Request->SetBoolField(TEXT("save"), true);
    AddExpectedError(TEXT("source control checkout required"), EAutomationExpectedErrorFlags::Contains, 1);
    auto Response = HandleInputMappingContextEntryAdd(TEXT("input_mapping_context_entry_add"), TEXT("issues10-save"), Request);
    TestFalse(TEXT("requested failed save fails the envelope"), Response->GetBoolField(TEXT("ok")));
    auto Data = Response->GetObjectField(TEXT("data"));
    TestTrue(TEXT("in-memory mapping remains explicit"), Context->GetMappings().Num() == 1);
    TestFalse(TEXT("failed write is not saved"), Data->GetBoolField(TEXT("saved")));
    TestTrue(TEXT("save was attempted"), Data->GetBoolField(TEXT("save_attempted")));
    TestTrue(TEXT("in-memory result is partial"), Data->GetBoolField(TEXT("partial")));
    TestEqual(TEXT("specific save code is retained"), Data->GetStringField(TEXT("save_code")), FString(TEXT("save_blocked_read_only")));
    TestTrue(TEXT("failed filename is retained"), Data->GetStringField(TEXT("save_error")).Contains(FPaths::GetCleanFilename(File)));
    TestEqual(TEXT("read-only save preserves disk bytes"), Collaboration::FileHash(File), Before);
    FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*File, false);
    const auto Retry = SaveAssetWrite(Context, true, false, false);
    TestTrue(TEXT("unchanged call still saves the existing dirty package"), Retry.bAttempted && Retry.bSaved);
    TestFalse(TEXT("retry clears dirty state"), Context->GetOutermost()->IsDirty());
    const auto Preview = SaveAssetWrite(Context, true, true, true);
    TestFalse(TEXT("dry run never attempts a save"), Preview.bAttempted);
    return true;
}
#endif
