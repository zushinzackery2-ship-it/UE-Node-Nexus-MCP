#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/UeNodeNexusBridgeOperations.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeExit.h"
#include "Transcode/Commit/NexusPackageFiles.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

using namespace UeNodeNexusBridge;
using namespace UeNodeNexusBridge::Collaboration;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusSavedRecreate, "Nexus.Issues2.RecreateSavedAssetAfterRollback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusSavedRecreate::RunTest(const FString& Parameters)
{
    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Name = TEXT("NexusSavedRecreate_") + Id;
    const FString PackageName = TEXT("/Game/") + Name;
    const FString AssetPath = PackageName + TEXT(".") + Name;
    ON_SCOPE_EXIT
    {
        if (UObject* Asset = FindObject<UObject>(nullptr, *AssetPath))
        {
            UPackage* Package = Asset->GetOutermost();
            ResetLoaders(Package);
            IFileManager::Get().Delete(*PackageFile(Package));
            Asset->ClearFlags(RF_Public | RF_Standalone);
            Asset->MarkAsGarbage();
            Package->SetDirtyFlag(false);
        }
    };
    const FJson Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("kind"), TEXT("material"));
    Request->SetStringField(TEXT("asset_path"), AssetPath);
    Request->SetBoolField(TEXT("expected_absent"), true);
    const FJson Missing = MakeShared<FJsonObject>();
    Missing->SetStringField(TEXT("asset_path"), AssetPath);
    Missing->SetBoolField(TEXT("exists"), false);
    const FJson Before = StampRaw(Missing);
    const FJson Receipt = MakeShared<FJsonObject>();
    Receipt->SetStringField(TEXT("apply_id"), Id);
    Receipt->SetObjectField(TEXT("request"), Request);
    Receipt->SetObjectField(TEXT("before"), Before);
    FString Error;
    if (!TestTrue(TEXT("capture prior absence"), Checkpoint(Request, Before, Receipt, Error)))
    {
        AddError(Error);
        return false;
    }
    const FJson Create = MakeShared<FJsonObject>();
    Create->SetStringField(TEXT("asset_path"), AssetPath);
    Create->SetStringField(TEXT("asset_kind"), TEXT("material"));
    Create->SetBoolField(TEXT("dry_run"), false);
    Create->SetBoolField(TEXT("save"), false);
    const FJson Created = HandleAssetCreate(TEXT("asset_create"), TEXT("initial"), Create);
    if (!TestTrue(TEXT("create first asset"), Created->GetBoolField(TEXT("ok"))))
    {
        return false;
    }
    UPackage* Package = FindPackage(nullptr, *PackageName);
    const FJson Row = Rows(Receipt, TEXT("packages"))[0]->AsObject();
    if (!TestTrue(TEXT("save first asset through publication staging"), SaveStagedPackage(Package, Receipt, Row, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("staged publication retains the real package path"), Package->GetLoadedPath().GetPackageName(), PackageName);
    TStrongObjectPtr<UObject> Retained(FindObject<UObject>(nullptr, *AssetPath));
    if (!TestTrue(TEXT("restore prior absence after successful save"), RestoreCheckpoint(Receipt, Error)))
    {
        AddError(Error);
        SaveReceipt(Receipt, TEXT("rejected"), Error);
        return false;
    }
    TestEqual(TEXT("rollback removes package file"), FileHash(Text(Row, TEXT("file"))), FString(TEXT("absent")));
    const FJson Recreated = HandleAssetCreate(TEXT("asset_create"), TEXT("retry"), Create);
    if (!TestTrue(TEXT("create replacement asset"), Recreated->GetBoolField(TEXT("ok"))))
    {
        return false;
    }
    Package = FindPackage(nullptr, *PackageName);
    TestTrue(TEXT("replacement has a new identity while old references survive"), FindObject<UObject>(nullptr, *AssetPath) != Retained.Get());
    TestTrue(TEXT("replacement package is fully in memory"), Package->IsFullyLoaded());
    const bool bSaved = SaveStagedPackage(Package, Receipt, Row, Error);
    TestTrue(TEXT("replacement asset can be saved"), bSaved);
    if (!bSaved)
    {
        AddError(Error);
    }
    SaveReceipt(Receipt, TEXT("rejected"), Error);
    return true;
}

#endif
