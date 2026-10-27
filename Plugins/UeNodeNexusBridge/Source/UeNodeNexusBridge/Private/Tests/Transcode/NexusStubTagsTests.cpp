#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeExit.h"
#include "Transcode/Commit/NexusPackageFiles.h"
#include "Transcode/UeNodeNexusBridgeTranscode.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"

using namespace UeNodeNexusBridge;

namespace
{
FString StubTag(const TSharedPtr<FJsonObject>& Raw, const FString& Name)
{
    if (Raw.IsValid())
    {
        for (const auto& Value : Raw->GetArrayField(TEXT("tags")))
        {
            const auto Tag = Value->AsObject();
            if (Tag->GetStringField(TEXT("name")) == Name)
            {
                return Tag->GetStringField(TEXT("value"));
            }
        }
    }
    return FString();
}

FString TagDigest(const TSharedPtr<FJsonObject>& Raw)
{
    const auto Tags = MakeShared<FJsonObject>();
    Tags->SetArrayField(TEXT("tags"), Raw->GetArrayField(TEXT("tags")));
    return Collaboration::ContentDigest(Tags);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusStubTags, "Nexus.Issues2.StubTagsUseSavedFile", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusStubTags::RunTest(const FString& Parameters)
{
    UTexture2D* Source = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
    if (!TestNotNull(TEXT("load fixture source"), Source))
    {
        return false;
    }
    UPackage* Package = CreatePackage(*(TEXT("/Game/NexusStubTags_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    UTexture2D* Texture = DuplicateObject<UTexture2D>(Source, Package, TEXT("Texture"));
    Texture->SetFlags(RF_Public | RF_Standalone);
    Texture->CompressionSettings = TC_Default;
    FAssetRegistryModule::AssetCreated(Texture);
    const FString File = Collaboration::PackageFile(Package);
    ON_SCOPE_EXIT
    {
        ResetLoaders(Package);
        IFileManager::Get().Delete(*File);
        Texture->ClearFlags(RF_Public | RF_Standalone);
        Texture->MarkAsGarbage();
        Package->SetDirtyFlag(false);
    };
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    const FSoftObjectPath AssetPath(Texture);
    const FAssetData AssetData = Registry.GetAssetByObjectPath(AssetPath);
    const auto Unsaved = Transcode::BuildStubRaw(AssetData);
    TestEqual(TEXT("unsaved stub has no persisted tags"), Unsaved->GetArrayField(TEXT("tags")).Num(), 0);
    FString Error;
    if (!TestTrue(TEXT("save fixture"), Transcode::SavePackageTo(Package, Texture, File, false, Error)))
    {
        AddError(Error);
        return false;
    }
    Registry.ScanFilesSynchronous({File}, true);
    const auto Before = Transcode::BuildStubRaw(AssetData);
    if (!TestTrue(TEXT("saved stub has tags"), Before.IsValid() && Before->GetArrayField(TEXT("tags")).Num() > 0))
    {
        return false;
    }
    const FString DiskHash = Collaboration::FileHash(File);
    const FString OriginalCompression = StubTag(Before, TEXT("CompressionSettings"));
    TestFalse(TEXT("saved compression tag exists"), OriginalCompression.IsEmpty());

    // UE's load path can rewrite DiskGatheredData without saving the package.
    Texture->CompressionSettings = TC_Normalmap;
    Registry.AssetUpdateTags(Texture, EAssetRegistryTagsCaller::FullUpdate);
    FString CachedCompression;
    Registry.GetAssetByObjectPath(AssetPath, true).GetTagValue(TEXT("CompressionSettings"), CachedCompression);
    TestTrue(TEXT("registry disk cache reflects unsaved memory"), CachedCompression != OriginalCompression);
    const auto AfterLoad = Transcode::BuildStubRaw(AssetData);
    if (!TestTrue(TEXT("stub remains readable after registry update"), AfterLoad.IsValid()))
    {
        return false;
    }
    TestEqual(TEXT("stub tags remain exactly as saved"), TagDigest(AfterLoad), TagDigest(Before));
    TestEqual(TEXT("registry update leaves file unchanged"), Collaboration::FileHash(File), DiskHash);

    if (!TestTrue(TEXT("save changed compression"), Transcode::SavePackageTo(Package, Texture, File, false, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("second save changes file bytes"), Collaboration::FileHash(File) != DiskHash);
    const auto AfterSave = Transcode::BuildStubRaw(AssetData);
    TestEqual(TEXT("save invalidates persisted-tag cache"), StubTag(AfterSave, TEXT("CompressionSettings")), CachedCompression);
    return true;
}

#endif
