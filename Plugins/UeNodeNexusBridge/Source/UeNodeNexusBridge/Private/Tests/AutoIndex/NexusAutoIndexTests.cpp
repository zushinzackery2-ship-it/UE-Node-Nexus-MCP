#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "AutoIndex/State/NexusAutoIndexState.h"
#include "AutoIndex/UeNodeNexusBridgeAutoIndex.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Misc/FileHelper.h"
#include "UObject/Package.h"

using namespace UeNodeNexusBridge;

namespace
{
using FAutoIndexHandler = TSharedPtr<FJsonObject> (*)(const FString&, const FString&, const TSharedPtr<FJsonObject>&);

// The handler's data object; null when it answered with an error.
TSharedPtr<FJsonObject> AutoIndexData(FAutoIndexHandler Handler, const TSharedPtr<FJsonObject>& Payload)
{
    const TSharedPtr<FJsonObject> Response = Handler(TEXT("auto_index"), TEXT("nexus-auto-index"), Payload);
    const TSharedPtr<FJsonObject>* Data = nullptr;
    return Response->GetBoolField(TEXT("ok")) && Response->TryGetObjectField(TEXT("data"), Data) ? *Data : nullptr;
}

TSharedPtr<FJsonObject> FieldPayload(const TCHAR* Field, const FString& Value)
{
    const TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(Field, Value);
    return Payload;
}

// Puts back the persisted index the test found and reloads it the way module startup does.
void RestoreAutoIndex(const FString& IndexPath, bool bHadFile, const TArray<uint8>& Saved)
{
    AutoIndexData(HandleAutoIndexDisable, MakeShared<FJsonObject>());
    if (!bHadFile)
    {
        IFileManager::Get().Delete(*IndexPath, false, true);
        AutoIndex::FState::Get().Clear(false);
        return;
    }
    FFileHelper::SaveArrayToFile(Saved, *IndexPath);
    StartupAutoIndex();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusAutoIndexOperations, "Nexus.AutoIndex.Operations", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusAutoIndexOperations::RunTest(const FString& Parameters)
{
    const TSharedPtr<FJsonObject> Empty = MakeShared<FJsonObject>();
    const TSharedPtr<FJsonObject> Before = AutoIndexData(HandleAutoIndexStatus, Empty);
    if (!TestTrue(TEXT("status answers"), Before.IsValid()))
    {
        return false;
    }
    const FString IndexPath = Before->GetStringField(TEXT("index_path"));
    TArray<uint8> Saved;
    const bool bHadFile = FFileHelper::LoadFileToArray(Saved, *IndexPath, FILEREAD_Silent);

    const TSharedPtr<FJsonObject> Enabled = AutoIndexData(HandleAutoIndexEnable, FieldPayload(TEXT("root_path"), TEXT("/Game/")));
    if (!TestTrue(TEXT("enable answers"), Enabled.IsValid()))
    {
        RestoreAutoIndex(IndexPath, bHadFile, Saved);
        return false;
    }
    TestTrue(TEXT("enabled index listens to the registry"), Enabled->GetBoolField(TEXT("listening")));
    TestEqual(TEXT("root path drops its trailing slash"), Enabled->GetStringField(TEXT("root_path")), FString(TEXT("/Game")));
    const TSharedPtr<FJsonObject> Diff = AutoIndexData(HandleAutoIndexDiffRegistry, Empty);
    TestTrue(TEXT("rebuild indexes exactly the registry assets"), Diff.IsValid()
        && Diff->GetIntegerField(TEXT("missing_in_index_count")) == 0 && Diff->GetIntegerField(TEXT("missing_in_registry_count")) == 0);

    UPackage* Package = CreatePackage(*(TEXT("/Game/NexusAutoIndex_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    UMaterial* Asset = NewObject<UMaterial>(Package, TEXT("NexusAutoIndexProbe"), RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(Asset);
    const FString AssetPath = Asset->GetPathName();

    const TSharedPtr<FJsonObject> Record = AutoIndexData(HandleAutoIndexGet, FieldPayload(TEXT("asset_path"), AssetPath));
    TestTrue(TEXT("the registry listener indexes a created asset"), Record.IsValid() && Record->GetStringField(TEXT("object_path")) == AssetPath);
    const TSharedPtr<FJsonObject> Resolved = AutoIndexData(HandleAutoIndexResolvePath, FieldPayload(TEXT("path"), AssetPath));
    TestTrue(TEXT("its object path resolves uniquely"), Resolved.IsValid() && Resolved->GetBoolField(TEXT("unique"))
        && Resolved->GetStringField(TEXT("resolved_asset_path")) == AssetPath);
    const TSharedPtr<FJsonObject> Queried = AutoIndexData(HandleAutoIndexQuery, FieldPayload(TEXT("text"), Package->GetName()));
    TestTrue(TEXT("a text query finds only it"), Queried.IsValid() && Queried->GetIntegerField(TEXT("total")) == 1);
    const TSharedPtr<FJsonObject> Overview = AutoIndexData(HandleAutoIndexOverview, Empty);
    TestTrue(TEXT("overview counts the created asset"), Overview.IsValid()
        && Overview->GetIntegerField(TEXT("total_assets")) == Enabled->GetIntegerField(TEXT("asset_count")) + 1);
    const TSharedPtr<FJsonObject> Tree = AutoIndexData(HandleAutoIndexTreeGet, Empty);
    TestTrue(TEXT("tree lists the root folder"), Tree.IsValid() && Tree->GetIntegerField(TEXT("returned_folders")) > 0);

    FAssetRegistryModule::AssetDeleted(Asset);
    TestFalse(TEXT("a deleted asset leaves the index"), AutoIndexData(HandleAutoIndexGet, FieldPayload(TEXT("asset_path"), AssetPath)).IsValid());
    Asset->ClearFlags(RF_Public | RF_Standalone);
    Asset->MarkAsGarbage();
    Package->SetDirtyFlag(false);

    RestoreAutoIndex(IndexPath, bHadFile, Saved);
    const TSharedPtr<FJsonObject> After = AutoIndexData(HandleAutoIndexStatus, Empty);
    TestTrue(TEXT("the index is restored"), After.IsValid() && After->GetBoolField(TEXT("enabled")) == Before->GetBoolField(TEXT("enabled"))
        && IFileManager::Get().FileExists(*IndexPath) == bHadFile);
    return true;
}

#endif
