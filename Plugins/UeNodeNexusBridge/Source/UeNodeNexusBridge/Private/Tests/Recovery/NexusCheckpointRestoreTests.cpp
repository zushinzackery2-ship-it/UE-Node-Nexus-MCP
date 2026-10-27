#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "Transcode/Commit/NexusPackageFiles.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"

using namespace UeNodeNexusBridge::Collaboration;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusCheckpointReferences, "Nexus.Issues2.CheckpointRestoresReferences", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusCheckpointReferences::RunTest(const FString& Parameters)
{
    const FString ApplyId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString PackageName = TEXT("/Game/NexusCheckpoint_") + ApplyId;
    UPackage* Package = CreatePackage(*PackageName);
    UMaterial* Material = NewObject<UMaterial>(Package, TEXT("Material"), RF_Public | RF_Standalone | RF_Transactional);
    FAssetRegistryModule::AssetCreated(Material);
    const FString AssetPath = Material->GetPathName();
    UMaterialInstanceConstant* Referencer = NewObject<UMaterialInstanceConstant>();
    Referencer->Parent = Material;
    Referencer->AddToRoot();
    ON_SCOPE_EXIT
    {
        Referencer->Parent = nullptr;
        Referencer->RemoveFromRoot();
        if (UObject* Current = FindObject<UObject>(nullptr, *AssetPath))
        {
            UPackage* CurrentPackage = Current->GetOutermost();
            ResetLoaders(CurrentPackage);
            IFileManager::Get().Delete(*PackageFile(CurrentPackage));
            CurrentPackage->SetDirtyFlag(false);
            Current->ClearFlags(RF_Standalone | RF_Public);
            Current->MarkAsGarbage();
        }
    };

    FString Error;
    const FString File = PackageFile(Package);
    Material->OpacityMaskClipValue = 0.25f;
    if (!TestTrue(TEXT("save initial disk version"), UeNodeNexusBridge::Transcode::SavePackageTo(Package, Material, File, false, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString DiskHash = FileHash(File);
    Material->OpacityMaskClipValue = 0.5f;
    Package->SetDirtyFlag(true);
    const FJson Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("kind"), TEXT("material"));
    Request->SetStringField(TEXT("asset_path"), AssetPath);
    const FJson Receipt = MakeShared<FJsonObject>();
    Receipt->SetStringField(TEXT("apply_id"), ApplyId);
    Receipt->SetObjectField(TEXT("request"), Request);
    Receipt->SetObjectField(TEXT("before"), Observe(Request));
    if (!TestTrue(TEXT("capture dirty memory and original disk"), CapturePackage(Package, Receipt, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("private checkpoint preserves package source path"), Package->GetLoadedPath().GetPackageName(), PackageName);
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Nexus checkpoint regression")));
        Material->Modify();
        Material->OpacityMaskClipValue = 0.75f;
    }
    const TWeakObjectPtr<UMaterial> Previous = Material;
    const bool bRestored = RestoreCheckpoint(Receipt, Error);
    TestTrue(TEXT("restore succeeds with material and undo references"), bRestored);
    if (!bRestored)
    {
        AddError(Error);
        SaveReceipt(Receipt, TEXT("rejected"), Error);
        return false;
    }
    UMaterial* Restored = FindObject<UMaterial>(nullptr, *AssetPath);
    if (!TestNotNull(TEXT("restored object keeps its original path"), Restored))
    {
        return false;
    }
    TestTrue(TEXT("restoration replaces the applied object"), Previous != Restored);
    TestEqual(TEXT("material instance now references the restored object"), Referencer->Parent.Get(), static_cast<UMaterialInterface*>(Restored));
    TestEqual(TEXT("restored memory retains the user's unsaved value"), Restored->OpacityMaskClipValue, 0.5f);
    TestTrue(TEXT("restored memory remains dirty"), Restored->GetOutermost()->IsDirty());
    TestEqual(TEXT("original disk bytes remain unchanged"), FileHash(File), DiskHash);
    TestEqual(TEXT("checkpoint is durably rolled back"), Text(Receipt, TEXT("phase")), FString(TEXT("rolled_back")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusCorruptCheckpoint, "Nexus.Issues2.CorruptCheckpointPreservesState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusCorruptCheckpoint::RunTest(const FString& Parameters)
{
    const FString ApplyId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*(TEXT("/Game/NexusCorrupt_") + ApplyId));
    UMaterial* Material = NewObject<UMaterial>(Package, TEXT("Material"), RF_Public | RF_Standalone);
    const FString File = PackageFile(Package);
    ON_SCOPE_EXIT
    {
        ResetLoaders(Package);
        IFileManager::Get().Delete(*File);
        Package->SetDirtyFlag(false);
        Material->ClearFlags(RF_Standalone | RF_Public);
        Material->MarkAsGarbage();
    };
    FString Error;
    if (!TestTrue(TEXT("save original file"), UeNodeNexusBridge::Transcode::SavePackageTo(Package, Material, File, false, Error)))
    {
        return false;
    }
    const FString OriginalHash = FileHash(File);
    const FJson Receipt = MakeShared<FJsonObject>();
    Receipt->SetStringField(TEXT("apply_id"), ApplyId);
    if (!TestTrue(TEXT("capture checkpoint"), CapturePackage(Package, Receipt, Error)))
    {
        return false;
    }
    const FJson Row = Rows(Receipt, TEXT("packages"))[0]->AsObject();
    const FJson MainFile = Rows(Row, TEXT("files"))[0]->AsObject();
    MainFile->SetStringField(TEXT("memory_hash"), TEXT("invalid-checkpoint-hash"));
    Material->OpacityMaskClipValue = 0.75f;
    Package->SetDirtyFlag(true);
    TestFalse(TEXT("corrupt snapshot is refused"), RestoreCheckpoint(Receipt, Error));
    TestTrue(TEXT("error identifies corrupt checkpoint"), Error.Contains(TEXT("corrupt")));
    TestEqual(TEXT("failed restore preserves applied memory"), Material->OpacityMaskClipValue, 0.75f);
    TestTrue(TEXT("failed restore preserves dirty state"), Package->IsDirty());
    TestEqual(TEXT("failed restore preserves disk bytes"), FileHash(File), OriginalHash);
    SaveReceipt(Receipt, TEXT("rejected"), Error);
    return true;
}

#endif
