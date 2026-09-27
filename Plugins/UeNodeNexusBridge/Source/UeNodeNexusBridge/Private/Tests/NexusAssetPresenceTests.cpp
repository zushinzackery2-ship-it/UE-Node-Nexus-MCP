#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Misc/Paths.h"
#include "Transcode/UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusCollaboration.h"
#include "UObject/Package.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusAssetPresence, "Nexus.Issues2.DeletedAssetObservation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusAssetPresence::RunTest(const FString& Parameters)
{
    const FString PreviousRoot = Transcode::GetMirrorRoot();
    const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NexusPresence"));
    IFileManager::Get().MakeDirectory(*Root, true);
    FString Error;
    if (!TestTrue(TEXT("configure isolated export directory"), Transcode::SetMirrorRoot(Root, Error)))
    {
        return false;
    }
    const FString PackageName = TEXT("/Game/NexusPresence_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*PackageName);
    TArray<UObject*> Assets;
    Assets.Add(NewObject<UMaterial>(Package, TEXT("Material"), RF_Public | RF_Standalone));
    Assets.Add(NewObject<UTexture2D>(Package, TEXT("Texture"), RF_Public | RF_Standalone));
    for (UObject* Asset : Assets)
    {
        FAssetRegistryModule::AssetCreated(Asset);
        const FAssetData Registered(Asset);
        TestTrue(TEXT("registered asset exists"), Transcode::IsAssetDataCurrent(Registered));
        const FString Kind = Transcode::KindForClass(Asset->GetClass());
        Asset->ClearFlags(RF_Public | RF_Standalone);
        TestTrue(TEXT("deleted object remains loaded"), LoadObject<UObject>(nullptr, *Asset->GetPathName()) == Asset);
        TestFalse(TEXT("retained registry row is no longer an asset"), Transcode::IsAssetDataCurrent(Registered));
        TestFalse(TEXT("raw builder rejects deleted objects"), Transcode::BuildRawForAsset(Asset, Kind).IsValid());

        const auto Payload = MakeShared<FJsonObject>();
        Payload->SetStringField(TEXT("asset_path"), Asset->GetPathName());
        Payload->SetStringField(TEXT("kind"), Kind);
        Payload->SetBoolField(TEXT("include_stubs"), true);
        Payload->SetStringField(TEXT("out_dir"), Root);
        TArray<TSharedPtr<FJsonValue>> Paths;
        Paths.Add(MakeShared<FJsonValueString>(Asset->GetPathName()));
        Payload->SetArrayField(TEXT("asset_paths"), Paths);

        const auto Observed = Collaboration::Observe(Payload);
        TestTrue(TEXT("observation returns a tombstone"), Observed.IsValid() && Observed->HasField(TEXT("exists")) && !Observed->GetBoolField(TEXT("exists")));
        const auto Status = HandleTranscodeStatus(TEXT("transcode_status"), TEXT("presence"), Payload)->GetObjectField(TEXT("data"));
        TestEqual(TEXT("status excludes the retained object"), Status->GetArrayField(TEXT("assets")).Num(), 0);
        const auto Exported = HandleTranscodeExport(TEXT("transcode_export"), TEXT("presence"), Payload)->GetObjectField(TEXT("data"));
        TestEqual(TEXT("export emits no deleted asset"), Exported->GetArrayField(TEXT("assets")).Num(), 0);
        const auto& Skipped = Exported->GetArrayField(TEXT("skipped"));
        if (TestEqual(TEXT("export explicitly reports the missing asset"), Skipped.Num(), 1))
        {
            TestEqual(TEXT("absence has a distinct reason"), Skipped[0]->AsObject()->GetStringField(TEXT("reason")), FString(TEXT("asset_not_found")));
        }
        FAssetRegistryModule::AssetDeleted(Asset);
        Asset->MarkAsGarbage();
    }
    Package->SetDirtyFlag(false);
    if (!PreviousRoot.IsEmpty())
    {
        Transcode::SetMirrorRoot(PreviousRoot, Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusAssetReferencePresence, "Nexus.Issues2.NewAssetReferenceQueries", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusAssetReferencePresence::RunTest(const FString& Parameters)
{
    UPackage* Package = CreatePackage(*(TEXT("/Game/NexusReferences_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    UMaterial* Asset = NewObject<UMaterial>(Package, TEXT("Material"), RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(Asset);
    TArray<FAssetData> DiskAssets;
    FAssetRegistryModule::GetRegistry().GetAssetsByPackageName(Package->GetFName(), DiskAssets, true);
    TestEqual(TEXT("new asset is not in the disk discovery cache"), DiskAssets.Num(), 0);
    const auto Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("asset_path"), Asset->GetPathName());
    const auto Referencers = HandleAssetReferencersGet(TEXT("asset_referencers_get"), TEXT("new"), Payload);
    const auto Dependencies = HandleAssetDependenciesGet(TEXT("asset_dependencies_get"), TEXT("new"), Payload);
    TestTrue(TEXT("reference query recognizes newly registered asset"), Referencers->GetBoolField(TEXT("ok")));
    TestTrue(TEXT("dependency query recognizes newly registered asset"), Dependencies->GetBoolField(TEXT("ok")));
    FAssetRegistryModule::AssetDeleted(Asset);
    Asset->ClearFlags(RF_Public | RF_Standalone);
    const auto Deleted = HandleAssetReferencersGet(TEXT("asset_referencers_get"), TEXT("deleted"), Payload);
    TestFalse(TEXT("reference query rejects a deleted retained object"), Deleted->GetBoolField(TEXT("ok")));
    TestEqual(TEXT("deleted query reports absence"), Deleted->GetObjectField(TEXT("error"))->GetStringField(TEXT("code")), FString(TEXT("asset_not_found")));
    Asset->MarkAsGarbage();
    Package->SetDirtyFlag(false);
    return true;
}

#endif
