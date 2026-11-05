#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/DataAsset.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Assets/Management/UeNodeNexusBridgeAssetManagementShared.h"
#include "Tests/Fixtures/NexusDiskAssets.h"
#include "Transcode/Commit/NexusPackageFiles.h"
#include "UObject/ObjectRedirector.h"

namespace
{
// A Blueprint moved from Src to Dst, a child Blueprint on disk that still names
// the old location, and the redirector package the move left behind.
struct FMovedBlueprint
{
    NexusTests::FDiskAssets Disk;
    FString Source;
    FString Destination;
    FString User;

    explicit FMovedBlueprint(const TCHAR* Label)
        : Disk(Label), Source(Disk.Package(TEXT("Src/BP_Moved"))), Destination(Disk.Package(TEXT("Dst/BP_Moved"))), User(Disk.Package(TEXT("User/BP_User")))
    {
    }

    // Writes the fixture; Extra may add objects to the user package before it is saved.
    bool Build(FAutomationTestBase& Test, TFunctionRef<void(UPackage*)> Extra)
    {
        UBlueprint* Moved = Disk.Blueprint(TEXT("Src/BP_Moved"), AActor::StaticClass());
        UBlueprint* Child = Disk.Blueprint(TEXT("User/BP_User"), Moved->GeneratedClass);
        Extra(Child->GetOutermost());
        FString Error;
        if (!Disk.Save(Moved->GetOutermost(), Error) || !Disk.Save(Child->GetOutermost(), Error))
        {
            Test.AddError(Error);
            return false;
        }
        if (!Disk.Move(Moved, Destination, Error))
        {
            Test.AddError(Error);
            return false;
        }
        // The rename fixed the child in memory only; its file still names Src.
        NexusTests::FDiskAssets::Unload({ User });
        Disk.Rescan();
        return true;
    }
};

int32 RegistryRedirectorRows(const FString& PackageName)
{
    TArray<FAssetData> Rows;
    FAssetRegistryModule::GetRegistry().GetAssetsByPackageName(FName(*PackageName), Rows, true);
    return Rows.FilterByPredicate([](const FAssetData& Row)
    {
        return Row.AssetClassPath == UObjectRedirector::StaticClass()->GetClassPathName();
    }).Num();
}

bool HasPackage(const TArray<TSharedPtr<FJsonValue>>& Rows, const FString& PackageName, bool bComplete)
{
    return Rows.ContainsByPredicate([&](const TSharedPtr<FJsonValue>& Row)
    {
        const TSharedPtr<FJsonObject> Item = Row->AsObject();
        return Item->GetStringField(TEXT("package")) == PackageName && Item->GetBoolField(TEXT("complete")) == bComplete;
    });
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRedirectorFixupBlueprintPackage,
    "Nexus.Issues24.RedirectorFixup.BlueprintPackage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRedirectorFixupBlueprintPackage::RunTest(const FString& Parameters)
{
    FMovedBlueprint Fixture(TEXT("NexusFixupPackage"));
    // The fixture writes and deletes its source within one editor tick; the
    // directory watcher subsequently delivers that earlier write notification.
    AddExpectedError(TEXT("package was marked as deleted in editor, but has been modified on disk"), EAutomationExpectedErrorFlags::Contains, 1);
    ON_SCOPE_EXIT
    {
        Fixture.Disk.Cleanup();
    };
    if (!Fixture.Build(*this, [](UPackage*) {}))
    {
        return false;
    }
    TestTrue(TEXT("the registry lists class and CDO redirectors beside the asset redirector"), RegistryRedirectorRows(Fixture.Source) > 1);

    const UeNodeNexusBridge::FRedirectorFixupProgress Preview = UeNodeNexusBridge::FixRedirectorsUnderFolder(Fixture.Disk.Package(TEXT("Src")), true);
    TestTrue(TEXT("preview resolves every redirector in the package"), Preview.Error.IsEmpty() && Preview.Count > 1);
    TestTrue(TEXT("preview changes nothing on disk"), FPackageName::DoesPackageExist(Fixture.Source));

    const UeNodeNexusBridge::FRedirectorFixupProgress Progress = UeNodeNexusBridge::FixRedirectorsUnderFolder(Fixture.Disk.Package(TEXT("Src")), false);
    if (!TestTrue(TEXT("fixup completes"), Progress.Error.IsEmpty()))
    {
        AddError(Progress.Error);
        return false;
    }
    TestEqual(TEXT("every redirector of the package is deleted"), Progress.DeletedRedirectors.Num(), Progress.Count);
    TestEqual(TEXT("the package is the deleted unit"), Progress.DeletedPackages.Num(), 1);
    TestFalse(TEXT("the redirector package is gone from disk"), FPackageName::DoesPackageExist(Fixture.Source));
    TestTrue(TEXT("preview audited the cold referencer"), HasPackage(Preview.Referencers, Fixture.User, true)
        && Preview.Referencers[0]->AsObject()->GetBoolField(TEXT("audited")));
    TestEqual(TEXT("the source has no remaining registry redirectors"), RegistryRedirectorRows(Fixture.Source), 0);
    TestEqual(TEXT("the referencer was saved"), Progress.SavedPackages.Num(), 1);

    NexusTests::FDiskAssets::Unload({ Fixture.User });
    UBlueprint* Child = LoadObject<UBlueprint>(nullptr, *(Fixture.User + TEXT(".BP_User")));
    UBlueprint* Moved = LoadObject<UBlueprint>(nullptr, *(Fixture.Destination + TEXT(".BP_Moved")));
    TestTrue(TEXT("the saved child names the moved parent class"), Child && Moved && Child->ParentClass == Moved->GeneratedClass);
    TArray<FName> Dependencies;
    FAssetRegistryModule::GetRegistry().GetDependencies(FName(*Fixture.User), Dependencies);
    TestTrue(TEXT("the child depends on the destination"), Dependencies.Contains(FName(*Fixture.Destination)));
    TestFalse(TEXT("the child no longer depends on the redirector"), Dependencies.Contains(FName(*Fixture.Source)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRedirectorFixupIncompleteReferencer,
    "Nexus.Issues24.RedirectorFixup.RefusesIncompleteReferencer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRedirectorFixupIncompleteReferencer::RunTest(const FString& Parameters)
{
    FMovedBlueprint Fixture(TEXT("NexusFixupIncomplete"));
    ON_SCOPE_EXIT
    {
        Fixture.Disk.Cleanup();
    };
    // The user package also holds an object of a Blueprint class that is then
    // deleted, like a map actor whose class no longer loads.
    UBlueprint* Gone = Fixture.Disk.Blueprint(TEXT("Gone/BP_Gone"), UPrimaryDataAsset::StaticClass());
    const FString GonePackage = Gone->GetOutermost()->GetName();
    FString Error;
    if (!TestTrue(TEXT("save the soon missing class"), NexusTests::FDiskAssets::Save(Gone->GetOutermost(), Error)))
    {
        AddError(Error);
        return false;
    }
    if (!Fixture.Build(*this, [Gone](UPackage* User)
        {
            NewObject<UObject>(User, Gone->GeneratedClass, TEXT("Orphan"), RF_Public | RF_Standalone);
        }))
    {
        return false;
    }
    const FString GoneFile = NexusTests::FDiskAssets::File(GonePackage);
    NexusTests::FDiskAssets::Unload({ GonePackage, Fixture.User });
    IFileManager::Get().Delete(*GoneFile);
    Fixture.Disk.Rescan();
    const FString UserFile = NexusTests::FDiskAssets::File(Fixture.User);
    const FString Before = UeNodeNexusBridge::Collaboration::FileHash(UserFile);

    AddExpectedError(TEXT("Unable to load Orphan"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("Gone/BP_Gone"), EAutomationExpectedErrorFlags::Contains, 0);
    const UeNodeNexusBridge::FRedirectorFixupProgress Progress = UeNodeNexusBridge::FixRedirectorsUnderFolder(Fixture.Disk.Package(TEXT("Src")), false);
    TestTrue(TEXT("an incomplete referencer refuses the fixup"), Progress.Error.StartsWith(TEXT("referencer_load_incomplete")));
    TestTrue(TEXT("the refused referencer names its dropped export"), Progress.IncompleteReferencers.ContainsByPredicate([&](const TSharedPtr<FJsonValue>& Row)
    {
        const TSharedPtr<FJsonObject> Item = Row->AsObject();
        return Item->GetStringField(TEXT("package")) == Fixture.User
            && Item->GetArrayField(TEXT("failed_exports")).ContainsByPredicate([](const TSharedPtr<FJsonValue>& Name)
            {
                return Name->AsString() == TEXT("Orphan");
            });
    }));
    TestTrue(TEXT("nothing was saved"), Progress.SavedPackages.IsEmpty());
    TestEqual(TEXT("the referencer file is untouched"), UeNodeNexusBridge::Collaboration::FileHash(UserFile), Before);
    TestTrue(TEXT("the redirector package is kept"), FPackageName::DoesPackageExist(Fixture.Source));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRedirectorFixupMapActor,
    "Nexus.Issues24.RedirectorFixup.PreservesMapActor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRedirectorFixupMapActor::RunTest(const FString& Parameters)
{
    FMovedBlueprint Fixture(TEXT("NexusFixupMap"));
    ON_SCOPE_EXIT { Fixture.Disk.Cleanup(); };
    AddExpectedError(TEXT("package was marked as deleted in editor, but has been modified on disk"), EAutomationExpectedErrorFlags::Contains, 1);
    const FString MapPackage = Fixture.Disk.Package(TEXT("User/Map"));
    bool bMapSaved = false;
    const bool bBuilt = Fixture.Build(*this, [&](UPackage* User)
    {
        auto* Child = Cast<UBlueprint>(FindObjectWithOuter(User, UBlueprint::StaticClass()));
        UWorld::InitializationValues Values = UWorld::InitializationValues()
            .AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
        UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("Map"), CreatePackage(*MapPackage), true, ERHIFeatureLevel::Num, &Values);
        World->SetFlags(RF_Public | RF_Standalone);
        FAssetRegistryModule::AssetCreated(World);
        FActorSpawnParameters Spawn;
        Spawn.Name = TEXT("KeptActor");
        AActor* Actor = World->SpawnActor<AActor>(Child->ParentClass, FTransform::Identity, Spawn);
        if (TestNotNull(TEXT("the map holds an actor of the moved class"), Actor))
        {
            FString Error;
            bMapSaved = Fixture.Disk.Save(World->GetOutermost(), Error);
            TestTrue(*Error, bMapSaved);
        }
        World->DestroyWorld(false);
        World->RemoveFromRoot();
    });
    if (!bBuilt || !bMapSaved)
    {
        return false;
    }
    Fixture.Disk.Unload({ MapPackage });
    Fixture.Disk.Rescan();
    const auto Progress = UeNodeNexusBridge::FixRedirectorsUnderFolder(Fixture.Disk.Package(TEXT("Src")), false);
    if (!TestTrue(*Progress.Error, Progress.Error.IsEmpty()))
    {
        return false;
    }
    TestTrue(TEXT("the map load was audited before saving"), HasPackage(Progress.Referencers, MapPackage, true));
    TestEqual(TEXT("both the map and child blueprint were saved"), Progress.SavedPackages.Num(), 2);
    Fixture.Disk.Unload({ MapPackage });
    UWorld* World = LoadObject<UWorld>(nullptr, *(MapPackage + TEXT(".Map")));
    UBlueprint* Moved = LoadObject<UBlueprint>(nullptr, *(Fixture.Destination + TEXT(".BP_Moved")));
    TestTrue(TEXT("cold readback preserves the actor and its resolved class"), World && Moved && World->PersistentLevel->Actors.ContainsByPredicate([&](AActor* Actor)
    {
        return Actor && Actor->GetFName() == TEXT("KeptActor") && Actor->GetClass() == Moved->GeneratedClass;
    }));
    TestFalse(TEXT("the redirector file is deleted"), FPackageName::DoesPackageExist(Fixture.Source));
    return true;
}
#endif
