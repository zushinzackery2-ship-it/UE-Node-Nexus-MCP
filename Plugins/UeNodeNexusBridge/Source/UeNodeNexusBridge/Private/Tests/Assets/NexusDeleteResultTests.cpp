#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Assets/Management/Deletion/NexusAssetDeleteResult.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/Fixtures/NexusDiskAssets.h"
#include "UeNodeNexusBridgeOperations.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusPartialDeleteReceipt,
    "Nexus.Issues26.Delete.PartialReceipt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusPartialDeleteReceipt::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    NexusTests::FDiskAssets Disk(TEXT("NexusDeleteResult"));
    ON_SCOPE_EXIT { Disk.Cleanup(); };
    UBlueprint* First = Disk.Blueprint(TEXT("First"), AActor::StaticClass());
    UBlueprint* Second = Disk.Blueprint(TEXT("Second"), AActor::StaticClass());
    const FString FirstPath = First->GetPathName();
    const FString SecondPath = Second->GetPathName();
    const FString FirstPackage = First->GetOutermost()->GetName();
    const FString SecondPackage = Second->GetOutermost()->GetName();
    FString Error;
    if (!Disk.Save(First->GetOutermost(), Error) || !Disk.Save(Second->GetOutermost(), Error))
    {
        AddError(Error);
        return false;
    }
    auto Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("asset_path"), FirstPath);
    Payload->SetBoolField(TEXT("dry_run"), false);
    Payload->SetBoolField(TEXT("cleanup_after_delete"), false);
    const auto Deleted = HandleAssetDelete(TEXT("asset_delete"), TEXT("first-only"), Payload);
    if (!TestTrue(TEXT("first asset is actually deleted"), Deleted->GetBoolField(TEXT("ok"))))
    {
        return false;
    }
    const TArray<AssetDeletion::FDeleteTarget> Targets {
        { FirstPath, FirstPackage, true }, { SecondPath, SecondPackage, false }
    };
    const auto Partial = AssetDeletion::Result(TEXT("asset_delete_batch"), TEXT("partial-observation"),
        Targets, {}, 1, false, MakeShared<FJsonObject>());
    const auto Data = Partial->GetObjectField(TEXT("data"));
    TestFalse(TEXT("a partial result is not success"), Partial->GetBoolField(TEXT("ok")));
    TestTrue(TEXT("a partial failure still reports the mutation"), Data->GetBoolField(TEXT("changed")));
    TestEqual(TEXT("one confirmed deleted asset"), Data->GetArrayField(TEXT("deleted_assets")).Num(), 1);
    TestEqual(TEXT("actual deleted path"), Data->GetArrayField(TEXT("deleted_assets"))[0]->AsString(), FirstPath);
    TestEqual(TEXT("remaining path"), Data->GetArrayField(TEXT("remaining_assets"))[0]->AsString(), SecondPath);
    TestTrue(TEXT("remaining asset stays on disk"), FPackageName::DoesPackageExist(SecondPackage));
    const auto Unchanged = AssetDeletion::Result(TEXT("asset_delete_batch"), TEXT("zero-observation"),
        { { SecondPath, SecondPackage, false } }, {}, 0, false, MakeShared<FJsonObject>());
    TestFalse(TEXT("zero deletions are unchanged"), Unchanged->GetObjectField(TEXT("data"))->GetBoolField(TEXT("changed")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusDeleteReceiptBypassesRegistryCache,
    "Nexus.Issues26.Delete.RegistryCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusDeleteReceiptBypassesRegistryCache::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    NexusTests::FDiskAssets Disk(TEXT("NexusDeleteCache"));
    ON_SCOPE_EXIT { Disk.Cleanup(); };
    UBlueprint* Asset = Disk.Blueprint(TEXT("Deleted"), AActor::StaticClass());
    const FString AssetPath = Asset->GetPathName();
    const FString PackageName = Asset->GetOutermost()->GetName();
    FString Error;
    if (!Disk.Save(Asset->GetOutermost(), Error))
    {
        AddError(Error);
        return false;
    }
    Disk.Rescan();
    const FString Filename = Disk.File(PackageName);
    Disk.Unload({ PackageName });
    if (!TestTrue(TEXT("delete the saved fixture file"), IFileManager::Get().Delete(*Filename)))
    {
        return false;
    }
    const bool bCachedExists = FPackageName::DoesPackageExist(PackageName);
    const bool bFileExists = IFileManager::Get().FileExists(*Filename);
    AddInfo(FString::Printf(TEXT("deletion observation: registry_exists=%d file_exists=%d"), bCachedExists, bFileExists));
    TestTrue(TEXT("the same-frame asset registry still knows the package"), bCachedExists);
    TestFalse(TEXT("the package file has actually been removed"), bFileExists);
    const auto Response = AssetDeletion::Result(TEXT("asset_delete_batch"), TEXT("registry-cache-observation"),
        { { AssetPath, PackageName, true } }, {}, 1, true, MakeShared<FJsonObject>());
    TestTrue(TEXT("actual deletion succeeds despite the registry cache"), Response->GetBoolField(TEXT("ok")));
    const auto Data = Response->GetObjectField(TEXT("data"));
    TestEqual(TEXT("actual deleted asset is recorded"), Data->GetArrayField(TEXT("deleted_assets")).Num(), 1);
    TestEqual(TEXT("no phantom remaining asset"), Data->GetArrayField(TEXT("remaining_assets")).Num(), 0);
    return true;
}

#endif
