#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Misc/ScopeExit.h"
#include "Transcode/Commit/NexusPackageFiles.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

using namespace UeNodeNexusBridge::Collaboration;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusSerializedCheckpoint,
    "Nexus.Issues7.Recovery.SerializedCheckpointBaseline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusSerializedCheckpoint::RunTest(const FString& Parameters)
{
    const FString ApplyId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Package = CreatePackage(*(TEXT("/Game/NexusSaveBaseline_") + ApplyId));
    auto* Material = NewObject<UMaterial>(Package, TEXT("Material"), RF_Public | RF_Standalone);
    const FString Path = Material->GetPathName();
    ON_SCOPE_EXIT
    {
        if (auto* Current = FindObject<UObject>(nullptr, *Path))
        {
            auto* Owner = Current->GetOutermost();
            ResetLoaders(Owner);
            IFileManager::Get().Delete(*PackageFile(Owner));
            Owner->SetDirtyFlag(false);
            Current->ClearFlags(RF_Public | RF_Standalone);
        }
    };
    FString Error;
    Material->OpacityMaskClipValue = 0.25f;
    if (!TestTrue(TEXT("save original disk"), UeNodeNexusBridge::Transcode::SavePackageTo(
        Package, Material, PackageFile(Package), false, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString DiskHash = FileHash(PackageFile(Package));
    Material->OpacityMaskClipValue = 0.5f;
    Package->SetDirtyFlag(true);
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("kind"), TEXT("material"));
    Request->SetStringField(TEXT("asset_path"), Path);
    const auto Before = Observe(Request);
    auto Receipt = MakeShared<FJsonObject>();
    Receipt->SetStringField(TEXT("apply_id"), ApplyId);
    Receipt->SetObjectField(TEXT("request"), Request);
    Receipt->SetObjectField(TEXT("before"), Before);
    const auto Handle = FCoreUObjectDelegates::OnObjectPreSave.AddLambda(
        [Material](UObject* Object, FObjectPreSaveContext)
        {
            if (Object == Material)
            {
                Material->OpacityMaskClipValue = 0.625f;
            }
        });
    const bool bCaptured = Checkpoint(Request, Before, Receipt, Error);
    FCoreUObjectDelegates::OnObjectPreSave.Remove(Handle);
    if (!TestTrue(TEXT("capture save-normalized checkpoint"), bCaptured))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("normalization is recorded"), Receipt->GetBoolField(TEXT("checkpoint_changed_state")));
    TestEqual(TEXT("CAS before remains the caller's original observation"),
        Text(Object(Receipt, TEXT("before")), TEXT("content_revision")), Text(Before, TEXT("content_revision")));
    Material->OpacityMaskClipValue = 0.9f;
    if (!TestTrue(TEXT("restore verifies serialized baseline"), RestoreCheckpoint(Receipt, Error)))
    {
        AddError(Error);
        return false;
    }
    const auto* Restored = FindObject<UMaterial>(nullptr, *Path);
    if (!TestNotNull(TEXT("restored material exists"), Restored))
    {
        return false;
    }
    TestEqual(TEXT("serialized normalized value is restored"), Restored->OpacityMaskClipValue, 0.625f);
    TestTrue(TEXT("unsaved status is preserved"), Restored->GetOutermost()->IsDirty());
    TestEqual(TEXT("original disk is preserved"), FileHash(PackageFile(Restored->GetOutermost())), DiskHash);
    return true;
}
#endif
