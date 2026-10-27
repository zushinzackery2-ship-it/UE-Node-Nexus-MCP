#include "UeNodeNexusBridgeOperations.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "UeNodeNexusBridgeAssetManagementShared.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleAssetDuplicate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString SourceAssetPath;
    FString DestinationAssetPath;
    if (!Payload->TryGetStringField(TEXT("source_asset_path"), SourceAssetPath) || !Payload->TryGetStringField(TEXT("destination_asset_path"), DestinationAssetPath) || SourceAssetPath.IsEmpty() || DestinationAssetPath.IsEmpty())
    {
        return MakeInvalidAssetRequest(Operation, RequestId, TEXT("source_asset_path and destination_asset_path are required"));
    }

    UObject* SourceAsset = LoadAssetForManagement(SourceAssetPath);
    if (SourceAsset == nullptr)
    {
        return MakeAssetNotFoundResponse(Operation, RequestId, SourceAssetPath);
    }

    FString DestinationPackageName;
    FString DestinationPackagePath;
    FString DestinationAssetName;
    FText Reason;
    if (!SplitAssetObjectPath(DestinationAssetPath, DestinationPackageName, DestinationPackagePath, DestinationAssetName, Reason))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_destination_path"), Reason.ToString()));
        return Response;
    }
    if (FPackageName::DoesPackageExist(DestinationPackageName) || LoadObject<UObject>(nullptr, *DestinationAssetPath) != nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("destination_asset_exists"), TEXT("Destination asset already exists")));
        return Response;
    }

    const bool bDryRun = ReadAssetBoolField(Payload, TEXT("dry_run"), true);
    const bool bSave = ReadAssetBoolField(Payload, TEXT("save"), false);
    UObject* DuplicatedAsset = nullptr;
    bool bSaved = false;
    if (!bDryRun)
    {
        FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
        DuplicatedAsset = AssetToolsModule.Get().DuplicateAsset(DestinationAssetName, DestinationPackagePath, SourceAsset);
        if (DuplicatedAsset != nullptr && bSave)
        {
            FString SaveError;
            bSaved = Transcode::SavePackageDirect(DuplicatedAsset, SaveError);
        }
    }

    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("source_asset_path"), SourceAssetPath);
    Item->SetStringField(TEXT("destination_asset_path"), DestinationAssetPath);

    const bool bDuplicated = bDryRun || DuplicatedAsset != nullptr;
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bDuplicated && (!bSave || bDryRun || bSaved));
    Response->SetObjectField(TEXT("data"), MakeAssetWriteData(bDryRun, !bDryRun && bDuplicated, bDuplicated, MakeAssetOpDiff(TEXT("assets_duplicated"), Item), DestinationPackageName, !bDryRun && bDuplicated && !bSaved, bSaved));
    if (!bDuplicated)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_duplicate_failed"), TEXT("Asset duplicate failed")));
    }
    else if (bSave && !bDryRun && !bSaved)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("save_failed"), TEXT("Asset was duplicated but save failed")));
    }
    return Response;
}
}
