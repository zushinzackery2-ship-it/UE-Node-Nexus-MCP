#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && PLATFORM_WINDOWS

#include "AssetRegistry/AssetRegistryModule.h"
#include "Fixtures/NexusFileStamp.h"
#include "Fixtures/NexusStubCacheFixture.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Transcode/Commit/NexusPackageFiles.h"
#include "Transcode/UeNodeNexusBridgeTranscode.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusStubRapidSave, "Nexus.Review.StubTagsTrackRapidSameSizeSave", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusStubRapidSave::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    UPackage* Package = CreatePackage(*(TEXT("/Game/NexusStubCache_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* Asset = NewObject<UNexusStubCacheFixture>(Package, TEXT("Fixture"), RF_Public | RF_Standalone);
    const FString File = FPaths::ConvertRelativePathToFull(Collaboration::PackageFile(Package));
    ON_SCOPE_EXIT
    {
        ResetLoaders(Package);
        IFileManager::Get().Delete(*File);
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->MarkAsGarbage();
        Package->SetDirtyFlag(false);
    };
    const FDateTime Second(2026, 9, 27);
    const FAssetData AssetData(Asset);
    TArray<FString> Markers;
    int64 OriginalSize = 0;
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        Asset->Marker = Pass == 0 ? 11 : 22;
        FString Error;
        if (!TestTrue(TEXT("save fixture"), Transcode::SavePackageTo(Package, Asset, File, false, Error)))
        {
            AddError(Error);
            return false;
        }
        ResetLoaders(Package);
        IFileManager::Get().SetTimeStamp(*File, Second);
        if (!TestTrue(TEXT("assign distinct native subsecond timestamps"), NexusTests::AdvanceNativeFileStamp(File, (Pass + 1) * 1000)))
        {
            return false;
        }
        const int64 Size = IFileManager::Get().FileSize(*File);
        if (Pass == 0)
        {
            OriginalSize = Size;
        }
        TestEqual(TEXT("both saves have equal size"), Size, OriginalSize);
        TestEqual(TEXT("both saves have equal UE file stamp"), IFileManager::Get().GetTimeStamp(*File), Second);
        const auto Raw = Transcode::BuildStubRaw(AssetData, &Error);
        if (!TestTrue(TEXT("read saved tags"), Raw.IsValid()))
        {
            AddError(Error);
            return false;
        }
        FString Marker;
        for (const auto& Value : Raw->GetArrayField(TEXT("tags")))
        {
            const auto Tag = Value->AsObject();
            if (Tag->GetStringField(TEXT("name")) == TEXT("Marker"))
            {
                Marker = Tag->GetStringField(TEXT("value"));
            }
        }
        Markers.Add(Marker);
    }
    TestEqual(TEXT("first saved tag"), Markers[0], FString(TEXT("11")));
    TestEqual(TEXT("same-second save invalidates tag cache"), Markers[1], FString(TEXT("22")));
    return true;
}

#endif
