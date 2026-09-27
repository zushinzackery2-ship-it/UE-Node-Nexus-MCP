#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Fixtures/NexusStubCacheFixture.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Fixtures/NexusFileStamp.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Transcode/UeNodeNexusBridgeTranscode.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"

namespace
{
bool CheckSubsecondChange(FAutomationTestBase& Test, const FString& PackageName, const FString& File)
{
#if PLATFORM_WINDOWS
    const FString Before = UeNodeNexusBridge::Transcode::PackageSavedHash(PackageName);
    const FDateTime Rounded = IFileManager::Get().GetTimeStamp(*File);
    if (!Test.TestTrue(TEXT("set submillisecond timestamp"), NexusTests::AdvanceNativeFileStamp(File, 1000)))
    {
        return false;
    }
    Test.TestEqual(TEXT("UE rounds both timestamps to the same second"), IFileManager::Get().GetTimeStamp(*File), Rounded);
    Test.TestTrue(TEXT("saved state preserves native timestamp precision"),
        Before != UeNodeNexusBridge::Transcode::PackageSavedHash(PackageName));
#endif
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusMapSavedHash, "Nexus.Review.MapSavedHashTracksFile", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMapSavedHash::RunTest(const FString& Parameters)
{
    const FString PackageName = TEXT("/Game/NexusSavedHash_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
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
    if (!TestTrue(TEXT("save real map"), FEditorFileUtils::SaveMap(World, File)))
    {
        return false;
    }
    ResetLoaders(World->GetOutermost());
    // File stamps can change before AssetRegistry processes the saved package.
    const FDateTime First(2026, 9, 27, 0, 0, 0);
    IFileManager::Get().SetTimeStamp(*File, First);
    const FString Before = UeNodeNexusBridge::Transcode::PackageSavedHash(PackageName);
    const FDateTime Second = First + FTimespan::FromSeconds(1);
    IFileManager::Get().SetTimeStamp(*File, Second);
    TestEqual(TEXT("filesystem accepted changed map stamp"), IFileManager::Get().GetTimeStamp(*File), Second);
    const FString After = UeNodeNexusBridge::Transcode::PackageSavedHash(PackageName);
    TestFalse(TEXT("saved map has an observable file state"), Before.IsEmpty());
    TestTrue(TEXT("map stamp changes independently of registry cache"), Before != After);

    return CheckSubsecondChange(*this, PackageName, File);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusAssetSavedHash, "Nexus.Review.AssetSavedHashTracksRapidChanges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusAssetSavedHash::RunTest(const FString& Parameters)
{
    const FString PackageName = TEXT("/Game/NexusAssetHash_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString File = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()));
    UPackage* Package = CreatePackage(*PackageName);
    auto* Asset = NewObject<UNexusStubCacheFixture>(Package, TEXT("Fixture"), RF_Public | RF_Standalone);
    ON_SCOPE_EXIT
    {
        ResetLoaders(Package);
        IFileManager::Get().Delete(*File);
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->MarkAsGarbage();
        Package->SetDirtyFlag(false);
    };
    FString Error;
    if (!TestTrue(TEXT("save real asset"), UeNodeNexusBridge::Transcode::SavePackageTo(Package, Asset, File, false, Error)))
    {
        AddError(Error);
        return false;
    }
    ResetLoaders(Package);
    IFileManager::Get().SetTimeStamp(*File, FDateTime(2026, 9, 27));
    return CheckSubsecondChange(*this, PackageName, File);
}

#endif
