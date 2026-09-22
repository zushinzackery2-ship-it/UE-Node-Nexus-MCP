#include "UeNodeNexusBridgeOperations.h"

#include "NexusBlueprintAssetType.h"
#include "UeNodeNexusBridgeAssetCreateHelpers.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleAssetCreate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString AssetPath;
    FString AssetKind;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || !Payload->TryGetStringField(TEXT("asset_kind"), AssetKind))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("asset_path and asset_kind are required")));
        return Response;
    }

    FString PackageName;
    FString AssetName;
    FText Reason;
    if (!ParseAssetPath(AssetPath, PackageName, AssetName, Reason))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_asset_path"), Reason.ToString()));
        return Response;
    }
    const FString ObjectPath = PackageName + TEXT(".") + AssetName;

    UObject* ExistingObject = FindObject<UObject>(nullptr, *ObjectPath);
    FString PackageFilename;
    bool bPackageExists = FPackageName::DoesPackageExist(PackageName, &PackageFilename);
    if (ExistingObject != nullptr && !bPackageExists && IsDiscardedAssetObject(ExistingObject))
    {
        const bool bReleasedObjectPath = ReleaseDiscardedAssetObject(ObjectPath);
        ExistingObject = FindObject<UObject>(nullptr, *ObjectPath);
        bPackageExists = FPackageName::DoesPackageExist(PackageName, &PackageFilename);
        if (!bReleasedObjectPath && ExistingObject != nullptr && IsDiscardedAssetObject(ExistingObject))
        {
            ExistingObject = nullptr;
        }
    }

    if (ExistingObject != nullptr || bPackageExists)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_already_exists"), TEXT("Asset package already exists")));
        Response->SetObjectField(TEXT("data"), BuildAssetCreateConflictData(PackageName, ObjectPath, ExistingObject, bPackageExists, PackageFilename));
        return Response;
    }

    bool bDryRun = true;
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);

    FString ParentAssetPath;
    FString ParentClassPath;
    FString BlueprintType;
    Payload->TryGetStringField(TEXT("parent_asset_path"), ParentAssetPath);
    Payload->TryGetStringField(TEXT("parent_class_path"), ParentClassPath);
    Payload->TryGetStringField(TEXT("blueprint_type"), BlueprintType);

    FString CreateError;
    if (!CheckAssetCreate(AssetKind, ParentAssetPath, ParentClassPath, BlueprintType, CreateError))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_create_failed"), CreateError));
        return Response;
    }

    if (bDryRun)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        Response->SetObjectField(TEXT("data"), MakeCreateData(ObjectPath, AssetKind, nullptr, true, false));
        return Response;
    }

    UPackage* Package = CreatePackage(*PackageName);
    UObject* Asset = nullptr;

    if (AssetKind.Equals(TEXT("material"), ESearchCase::IgnoreCase))
    {
        Asset = CreateMaterialAsset(Package, FName(*AssetName));
    }
    else if (AssetKind.Equals(TEXT("material_instance"), ESearchCase::IgnoreCase))
    {
        Asset = CreateMaterialInstanceAsset(Package, FName(*AssetName), ParentAssetPath);
    }
    else if (AssetKind.Equals(TEXT("blueprint"), ESearchCase::IgnoreCase))
    {
        Asset = CreateTypedBlueprintAsset(Package, FName(*AssetName), ParentClassPath, BlueprintType, CreateError);
    }
    else if (AssetKind.Equals(TEXT("material_function"), ESearchCase::IgnoreCase))
    {
        Asset = CreateMaterialFunctionAsset(Package, FName(*AssetName));
    }
    else if (AssetKind.Equals(TEXT("data_asset"), ESearchCase::IgnoreCase))
    {
        Asset = CreateDataAsset(Package, FName(*AssetName), ParentClassPath);
    }
    else if (AssetKind.Equals(TEXT("texture_render_target_2d"), ESearchCase::IgnoreCase))
    {
        Asset = CreateTextureRenderTarget2DAsset(Package, FName(*AssetName));
    }

    if (Asset == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(
            TEXT("asset_create_failed"),
            CreateError.IsEmpty() ? FString::Printf(TEXT("factory produced no %s asset"), *AssetKind) : CreateError));
        return Response;
    }

    FAssetRegistryModule::AssetCreated(Asset);
    Package->MarkPackageDirty();
    Asset->PostEditChange();

    FString SaveError;
    FString SaveCode;
    const bool bSaved = bSave && Transcode::SavePackageDirect(Package, Asset, SaveError, &SaveCode);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, !bSave || bSaved);
    Response->SetObjectField(TEXT("data"), MakeCreateData(Asset->GetPathName(), AssetKind, Asset, false, bSaved));
    if (bSave && !bSaved)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(SaveCode, FString::Printf(TEXT("Asset was created but package save failed: %s"), *SaveError)));
    }
    return Response;
}
}
