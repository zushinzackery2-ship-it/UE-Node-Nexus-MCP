#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Assets/Management/UeNodeNexusBridgeAssetManagementShared.h"
#include "Engine/Level.h"
#include "Engine/PrimaryAssetLabel.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/Fixtures/NexusDiskAssets.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusMapRedirectorFixup,
    "Nexus.Issues24.RedirectorFixup.MovedMapPackage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMapRedirectorFixup::RunTest(const FString& Parameters)
{
    NexusTests::FDiskAssets Disk(TEXT("NexusMovedMap"));
    TWeakObjectPtr<UWorld> CreatedWorld;
    ON_SCOPE_EXIT
    {
        if (CreatedWorld.IsValid() && CreatedWorld->IsRooted())
        {
            CreatedWorld->DestroyWorld(false);
            CreatedWorld->RemoveFromRoot();
        }
        Disk.Cleanup();
    };
    AddExpectedError(TEXT("package was marked as deleted in editor, but has been modified on disk"), EAutomationExpectedErrorFlags::Contains, 1);
    const FString Source = Disk.Package(TEXT("Src/Map"));
    const FString Destination = Disk.Package(TEXT("Dst/Map"));
    const FString UserPackage = Disk.Package(TEXT("User"));
    UWorld::InitializationValues Values = UWorld::InitializationValues()
        .AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("Map"), CreatePackage(*Source), true, ERHIFeatureLevel::Num, &Values);
    CreatedWorld = World;
    World->SetFlags(RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(World);
    FActorSpawnParameters Spawn;
    Spawn.Name = TEXT("KeptActor");
    World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
    auto* User = NewObject<UPrimaryAssetLabel>(CreatePackage(*UserPackage), TEXT("User"), RF_Public | RF_Standalone);
    User->ExplicitAssets.Add(World);
    FAssetRegistryModule::AssetCreated(User);
    FString Error;
    if (!Disk.Save(World->GetOutermost(), Error) || !Disk.Save(User->GetOutermost(), Error))
    {
        AddError(Error);
        return false;
    }
    Disk.Unload({ UserPackage });
    if (!Disk.Move(World, Destination, Error))
    {
        AddError(Error);
        return false;
    }
    World->DestroyWorld(false);
    World->RemoveFromRoot();
    Disk.Unload({ Source, Destination });
    Disk.Rescan();

    const auto Progress = UeNodeNexusBridge::FixRedirectorsUnderFolder(Disk.Package(TEXT("Src")), false);
    if (!TestTrue(*Progress.Error, Progress.Error.IsEmpty()))
    {
        return false;
    }
    TestEqual(TEXT("one map redirector package is removed"), Progress.DeletedPackages.Num(), 1);
    TestFalse(TEXT("the old umap file is gone"), FPackageName::DoesPackageExist(Source));
    Disk.Unload({ UserPackage, Destination });
    User = LoadObject<UPrimaryAssetLabel>(nullptr, *(UserPackage + TEXT(".User")));
    World = LoadObject<UWorld>(nullptr, *(Destination + TEXT(".Map")));
    TestTrue(TEXT("cold soft reference names the destination map"), User && User->ExplicitAssets.Num() == 1
        && User->ExplicitAssets[0].ToSoftObjectPath().ToString() == Destination + TEXT(".Map"));
    TestTrue(TEXT("the moved map preserves its actor"), World && World->PersistentLevel->Actors.ContainsByPredicate([](AActor* Actor)
    {
        return Actor && Actor->GetFName() == TEXT("KeptActor");
    }));
    return true;
}
#endif
