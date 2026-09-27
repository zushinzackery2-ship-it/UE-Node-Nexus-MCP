#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Transcode/Commit/NexusPackageFiles.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"

using namespace UeNodeNexusBridge::Collaboration;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusMapCheckpoint, "Nexus.Issues2.CheckpointRestoresActiveMap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMapCheckpoint::RunTest(const FString& Parameters)
{
    const FString ApplyId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString PackageName = TEXT("/Game/NexusMapCheckpoint_") + ApplyId;
    const FString File = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetMapPackageExtension()));
    UWorld* World = GEditor->NewMap();
    ON_SCOPE_EXIT
    {
        GEditor->NewMap();
        if (UPackage* Loaded = FindPackage(nullptr, *PackageName))
        {
            ResetLoaders(Loaded);
            Loaded->SetDirtyFlag(false);
        }
        IFileManager::Get().Delete(*File);
    };
    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
    Actor->SetActorLocation(FVector(10, 20, 30));
    if (!TestTrue(TEXT("save original map"), FEditorFileUtils::SaveMap(World, File)))
    {
        return false;
    }
    TestEqual(TEXT("map keeps original package identity"), World->GetOutermost()->GetName(), PackageName);
    const FString ActorPath = Actor->GetPathName();
    const FString DiskHash = FileHash(File);
    Actor->SetActorLocation(FVector(40, 50, 60));
    World->GetOutermost()->SetDirtyFlag(true);
    const FJson Selector = MakeShared<FJsonObject>();
    Selector->SetStringField(TEXT("name"), TEXT("checkpoint"));
    Selector->SetStringField(TEXT("map_path"), PackageName);
    TArray<TSharedPtr<FJsonValue>> Actors;
    Actors.Add(MakeShared<FJsonValueString>(ActorPath));
    Selector->SetArrayField(TEXT("actor_paths"), Actors);
    const FJson Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("kind"), TEXT("scene"));
    Request->SetObjectField(TEXT("selector"), Selector);
    const FJson Before = Observe(Request);
    if (!TestTrue(TEXT("observe active scene"), Before.IsValid()))
    {
        return false;
    }
    const FJson Receipt = MakeShared<FJsonObject>();
    Receipt->SetStringField(TEXT("apply_id"), ApplyId);
    Receipt->SetObjectField(TEXT("request"), Request);
    Receipt->SetObjectField(TEXT("before"), Before);
    FString Error;
    if (!TestTrue(TEXT("capture dirty scene"), CapturePackage(World->GetOutermost(), Receipt, Error)))
    {
        AddError(Error);
        return false;
    }
    Actor->SetActorLocation(FVector(70, 80, 90));
    const bool bRestored = RestoreCheckpoint(Receipt, Error);
    TestTrue(TEXT("restore active map checkpoint"), bRestored);
    if (!bRestored)
    {
        AddError(Error);
        SaveReceipt(Receipt, TEXT("rejected"), Error);
        return false;
    }
    UWorld* RestoredWorld = GEditor->GetEditorWorldContext().World();
    TestEqual(TEXT("restored map is active at its original path"), RestoredWorld->GetOutermost()->GetName(), PackageName);
    AStaticMeshActor* RestoredActor = FindObject<AStaticMeshActor>(nullptr, *ActorPath);
    if (TestNotNull(TEXT("scene actor retains identity"), RestoredActor))
    {
        TestEqual(TEXT("scene retains unsaved pre-apply transform"), RestoredActor->GetActorLocation(), FVector(40, 50, 60));
    }
    TestTrue(TEXT("scene retains dirty state"), RestoredWorld->GetOutermost()->IsDirty());
    TestEqual(TEXT("original saved map bytes restored"), FileHash(File), DiskHash);
    return true;
}

#endif
