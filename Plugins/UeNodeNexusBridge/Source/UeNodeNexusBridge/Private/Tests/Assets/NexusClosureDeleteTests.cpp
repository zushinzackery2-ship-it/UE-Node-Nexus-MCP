#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/DataAsset.h"
#include "Engine/PrimaryAssetLabel.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/Fixtures/NexusDiskAssets.h"
#include "Transcode/Commit/NexusPackageFiles.h"
#include "UeNodeNexusBridgeOperations.h"

namespace
{
TArray<FString> Strings(const TSharedPtr<FJsonObject>& Item, const TCHAR* Field)
{
    TArray<FString> Result;
    for (const TSharedPtr<FJsonValue>& Value : Item->GetArrayField(Field))
    {
        Result.Add(Value->AsString());
    }
    return Result;
}

TSharedPtr<FJsonObject> Request(const TArray<FString>& Paths, bool bDryRun)
{
    auto Payload = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const FString& Path : Paths)
    {
        Values.Add(MakeShared<FJsonValueString>(Path));
    }
    Payload->SetArrayField(TEXT("asset_paths"), Values);
    Payload->SetBoolField(TEXT("dry_run"), bDryRun);
    Payload->SetBoolField(TEXT("allow_referenced"), false);
    Payload->SetBoolField(TEXT("cleanup_after_delete"), true);
    return Payload;
}

TSharedPtr<FJsonObject> DeletedItem(const TSharedPtr<FJsonObject>& Response)
{
    return Response->GetObjectField(TEXT("data"))->GetObjectField(TEXT("diff"))->GetArrayField(TEXT("assets_deleted"))[0]->AsObject();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusDeleteClosureTransaction,
    "Nexus.Issues24.DeleteClosure.Transaction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusDeleteClosureTransaction::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    NexusTests::FDiskAssets Disk(TEXT("NexusClosureDelete"));
    ON_SCOPE_EXIT
    {
        Disk.Cleanup();
    };
    UBlueprint* Base = Disk.Blueprint(TEXT("Closure/BP_Base"), AActor::StaticClass());
    UBlueprint* Child = Disk.Blueprint(TEXT("Closure/BP_Child"), Base->GeneratedClass);
    const FString BasePackage = Base->GetOutermost()->GetName();
    const FString ChildPackage = Child->GetOutermost()->GetName();
    const FString BasePath = Base->GetPathName();
    const FString ChildPath = Child->GetPathName();
    FString Error;
    if (!NexusTests::FDiskAssets::Save(Base->GetOutermost(), Error) || !NexusTests::FDiskAssets::Save(Child->GetOutermost(), Error))
    {
        AddError(Error);
        return false;
    }
    // A retirement closure is usually cold: nothing in it is resident yet.
    NexusTests::FDiskAssets::Unload({ ChildPackage, BasePackage });
    Disk.Rescan();

    const auto Partial = HandleAssetDeleteBatch(TEXT("asset_delete_batch"), TEXT("partial"), Request({ BasePath }, false));
    TestFalse(TEXT("a member referenced from outside the set is refused"), Partial->GetBoolField(TEXT("ok")));
    TestEqual(TEXT("refusal names the outside referencer"), Strings(Partial->GetObjectField(TEXT("data")), TEXT("referencers")), TArray<FString>{ ChildPackage });
    TestTrue(TEXT("refusal deletes nothing"), FPackageName::DoesPackageExist(BasePackage));

    auto SinglePayload = Request({ BasePath }, false);
    SinglePayload->SetStringField(TEXT("asset_path"), BasePath);
    const auto Single = HandleAssetDelete(TEXT("asset_delete"), TEXT("single"), SinglePayload);
    TestEqual(TEXT("single deletion keeps the referencer contract"), Single->GetObjectField(TEXT("error"))->GetStringField(TEXT("code")), FString(TEXT("asset_has_referencers")));

    const auto Preview = HandleAssetDeleteBatch(TEXT("asset_delete_batch"), TEXT("preview"), Request({ BasePath, ChildPath }, true));
    TestTrue(TEXT("the closure preview succeeds"), Preview->GetBoolField(TEXT("ok")));
    TestEqual(TEXT("dependents come first"), Strings(DeletedItem(Preview), TEXT("order")), TArray<FString>{ ChildPath, BasePath });
    TestTrue(TEXT("preview deletes nothing"), FPackageName::DoesPackageExist(BasePackage) && FPackageName::DoesPackageExist(ChildPackage));
    TestTrue(TEXT("preview records no actual deletions"), Strings(Preview->GetObjectField(TEXT("data")), TEXT("deleted_assets")).IsEmpty());

    const auto Deleted = HandleAssetDeleteBatch(TEXT("asset_delete_batch"), TEXT("delete"), Request({ BasePath, ChildPath }, false));
    if (!TestTrue(TEXT("the closure is deleted"), Deleted->GetBoolField(TEXT("ok"))))
    {
        return false;
    }
    const TSharedPtr<FJsonObject> Item = DeletedItem(Deleted);
    TestEqual(TEXT("receipt names the actual deleted assets"), Strings(Deleted->GetObjectField(TEXT("data")), TEXT("deleted_assets")), TArray<FString>{ ChildPath, BasePath });
    TestTrue(TEXT("receipt has no remaining targets"), Strings(Deleted->GetObjectField(TEXT("data")), TEXT("remaining_assets")).IsEmpty());
    TestFalse(TEXT("both files are gone"), FPackageName::DoesPackageExist(BasePackage) || FPackageName::DoesPackageExist(ChildPackage));
    TestTrue(TEXT("memory is released"), Deleted->GetObjectField(TEXT("data"))->GetBoolField(TEXT("memory_released")));
    TestTrue(TEXT("no package stays resident"), Strings(Item, TEXT("residual_packages")).IsEmpty());
    const TArray<TSharedPtr<FJsonValue>>& Loads = Item->GetArrayField(TEXT("loads"));
    TestEqual(TEXT("every member is loaded before deletion"), Loads.Num(), 2);
    TestTrue(TEXT("the first member load is a fresh audited load"), Loads.Num() == 2
        && Loads[0]->AsObject()->GetBoolField(TEXT("loaded_here")) && Loads[0]->AsObject()->GetBoolField(TEXT("complete")));
    TestTrue(TEXT("no loaded referencer is left dangling"), Item->GetArrayField(TEXT("loaded_external_referencers")).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusDeleteClosureIncompleteDependency,
    "Nexus.Issues24.DeleteClosure.RefusesIncompleteDependency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusDeleteClosureIncompleteDependency::RunTest(const FString& Parameters)
{
    NexusTests::FDiskAssets Disk(TEXT("NexusClosureIncomplete"));
    ON_SCOPE_EXIT
    {
        Disk.Cleanup();
    };
    UBlueprint* Gone = Disk.Blueprint(TEXT("Gone/BP_Gone"), UPrimaryDataAsset::StaticClass());
    UBlueprint* Base = Disk.Blueprint(TEXT("Closure/BP_Base"), AActor::StaticClass());
    UBlueprint* Child = Disk.Blueprint(TEXT("Closure/BP_Child"), Base->GeneratedClass);
    NewObject<UObject>(Base->GetOutermost(), Gone->GeneratedClass, TEXT("Orphan"), RF_Public | RF_Standalone);
    const FString GonePackage = Gone->GetOutermost()->GetName();
    const FString BasePackage = Base->GetOutermost()->GetName();
    const FString ChildPackage = Child->GetOutermost()->GetName();
    const TArray<FString> Paths { Base->GetPathName(), Child->GetPathName() };
    FString Error;
    if (!Disk.Save(Gone->GetOutermost(), Error) || !Disk.Save(Base->GetOutermost(), Error) || !Disk.Save(Child->GetOutermost(), Error))
    {
        AddError(Error);
        return false;
    }
    const FString GoneFile = Disk.File(GonePackage);
    const FString BaseFile = Disk.File(BasePackage);
    const FString Before = UeNodeNexusBridge::Collaboration::FileHash(BaseFile);
    Disk.Unload({ ChildPackage, BasePackage, GonePackage });
    IFileManager::Get().Delete(*GoneFile);
    Disk.Rescan();

    // Loading the dependent first also loads Base. Its missing export must be
    // audited even when Base is already resident by the time its turn arrives.
    AddExpectedError(TEXT("Unable to load Orphan"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("Gone/BP_Gone"), EAutomationExpectedErrorFlags::Contains, 0);
    const auto Response = UeNodeNexusBridge::HandleAssetDeleteBatch(TEXT("asset_delete_batch"), TEXT("incomplete"), Request(Paths, false));
    TestFalse(TEXT("a dropped dependency export refuses the whole deletion"), Response->GetBoolField(TEXT("ok")));
    TestTrue(TEXT("every closure file survives refusal"), FPackageName::DoesPackageExist(BasePackage) && FPackageName::DoesPackageExist(ChildPackage));
    TestEqual(TEXT("the incomplete file remains byte-identical"), UeNodeNexusBridge::Collaboration::FileHash(BaseFile), Before);
    if (!Response->GetBoolField(TEXT("ok")))
    {
        TestEqual(TEXT("the failure names the load boundary"), Response->GetObjectField(TEXT("error"))->GetStringField(TEXT("code")), FString(TEXT("asset_load_incomplete")));
        const auto Item = DeletedItem(Response);
        const auto& Incomplete = Item->GetArrayField(TEXT("incomplete_packages"));
        TestTrue(TEXT("the indirectly loaded parent is audited with its missing export"), Incomplete.ContainsByPredicate([&](const TSharedPtr<FJsonValue>& Row)
        {
            const auto Audit = Row->AsObject();
            return Audit->GetStringField(TEXT("package")) == BasePackage && Audit->GetBoolField(TEXT("audited"))
                && Strings(Audit, TEXT("failed_exports")).Contains(TEXT("Orphan"));
        }));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusDeleteClosureCycle,
    "Nexus.Issues24.DeleteClosure.ReferenceCycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusDeleteClosureCycle::RunTest(const FString& Parameters)
{
    NexusTests::FDiskAssets Disk(TEXT("NexusClosureCycle"));
    ON_SCOPE_EXIT { Disk.Cleanup(); };
    AddExpectedError(TEXT("package was marked as deleted in editor, but has been modified on disk"), EAutomationExpectedErrorFlags::Contains, 2);
    auto* First = NewObject<UPrimaryAssetLabel>(CreatePackage(*Disk.Package(TEXT("First"))), TEXT("First"), RF_Public | RF_Standalone);
    auto* Second = NewObject<UPrimaryAssetLabel>(CreatePackage(*Disk.Package(TEXT("Second"))), TEXT("Second"), RF_Public | RF_Standalone);
    First->ExplicitAssets.Add(Second);
    Second->ExplicitAssets.Add(First);
    FAssetRegistryModule::AssetCreated(First);
    FAssetRegistryModule::AssetCreated(Second);
    const TArray<FString> Paths { First->GetPathName(), Second->GetPathName() };
    const TArray<FString> Packages { First->GetOutermost()->GetName(), Second->GetOutermost()->GetName() };
    FString Error;
    if (!Disk.Save(First->GetOutermost(), Error) || !Disk.Save(Second->GetOutermost(), Error))
    {
        AddError(Error);
        return false;
    }
    Disk.Unload(Packages);
    Disk.Rescan();
    const auto Response = UeNodeNexusBridge::HandleAssetDeleteBatch(TEXT("asset_delete_batch"), TEXT("cycle"), Request(Paths, false));
    TestTrue(TEXT("a reference cycle is deleted as one set"), Response->GetBoolField(TEXT("ok")));
    TestEqual(TEXT("cycle members retain request order"), Strings(DeletedItem(Response), TEXT("order")), Paths);
    TestTrue(TEXT("both cycle packages are removed"), !FPackageName::DoesPackageExist(Packages[0]) && !FPackageName::DoesPackageExist(Packages[1]));
    TestTrue(TEXT("cycle packages are released"), Response->GetObjectField(TEXT("data"))->GetBoolField(TEXT("memory_released")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusDeleteUnsavedAsset,
    "Nexus.Issues24.DeleteClosure.UnsavedAsset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusDeleteUnsavedAsset::RunTest(const FString& Parameters)
{
    NexusTests::FDiskAssets Disk(TEXT("NexusDeleteUnsaved"));
    ON_SCOPE_EXIT { Disk.Cleanup(); };
    UBlueprint* Blueprint = Disk.Blueprint(TEXT("BP_Unsaved"), AActor::StaticClass());
    auto Payload = Request({}, false);
    Payload->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    const auto Response = UeNodeNexusBridge::HandleAssetDelete(TEXT("asset_delete"), TEXT("unsaved"), Payload);
    TestTrue(TEXT("a never-saved asset remains deletable"), Response->GetBoolField(TEXT("ok")));
    TestTrue(TEXT("the unsaved package is released"), Response->GetObjectField(TEXT("data"))->GetBoolField(TEXT("memory_released")));
    return true;
}
#endif
