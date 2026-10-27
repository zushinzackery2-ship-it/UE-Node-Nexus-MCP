#include "UeNodeNexusBridgeOperations.h"

#include "Editor.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "UeNodeNexusBridgeAssetManagementShared.h"
#include "UeNodeNexusBridgeJson.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
static bool ReadDeleteCleanupField(const TSharedPtr<FJsonObject>& Payload)
{
    if (Payload->HasTypedField<EJson::Boolean>(TEXT("cleanup_after_delete")))
    {
        return ReadAssetBoolField(Payload, TEXT("cleanup_after_delete"), true);
    }
    return ReadAssetBoolField(Payload, TEXT("release_package_after_delete"), true);
}

static bool ReleasePackageAfterDelete(const FString& PackageName)
{
    if (PackageName.IsEmpty())
    {
        return false;
    }

    UPackage* Package = FindPackage(nullptr, *PackageName);
    if (Package == nullptr)
    {
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        return FindPackage(nullptr, *PackageName) == nullptr;
    }

    if (GEditor)
    {
        GEditor->ResetTransaction(NSLOCTEXT("UeNodeNexusBridge", "ReleasePackageAfterDelete", "UeNodeNexusBridge release deleted asset package"));
    }

    UPackageTools::FUnloadPackageParams UnloadParams({ Package });
    UnloadParams.bResetTransBuffer = false;
    UPackageTools::UnloadPackages(UnloadParams);
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    return FindPackage(nullptr, *PackageName) == nullptr;
}

TSharedPtr<FJsonObject> HandleAssetDelete(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        return MakeInvalidAssetRequest(Operation, RequestId, TEXT("asset_path is required"));
    }

    UObject* Asset = LoadAssetForManagement(AssetPath);
    if (Asset == nullptr)
    {
        return MakeAssetNotFoundResponse(Operation, RequestId, AssetPath);
    }

    const bool bDryRun = ReadAssetBoolField(Payload, TEXT("dry_run"), true);
    const bool bAllowReferenced = ReadAssetBoolField(Payload, TEXT("allow_referenced"), false);
    const bool bCleanupAfterDelete = ReadDeleteCleanupField(Payload);
    TArray<FName> Referencers;
    GetAssetReferencers(AssetPath, Referencers);
    if (Referencers.Num() > 0 && !bAllowReferenced)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_has_referencers"), TEXT("Asset has referencers; set allow_referenced to true to delete")));
        Response->SetArrayField(TEXT("diagnostics"), { MakeShared<FJsonValueObject>(UeNodeNexusBridge::MakeDiagnostic(TEXT("warning"), TEXT("asset_has_referencers"), TEXT("Asset has referencers"), AssetPath, TEXT("UeNodeNexusBridge"))) });
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetArrayField(TEXT("referencers"), PackageNamesToJson(Referencers));
        Response->SetObjectField(TEXT("data"), Data);
        return Response;
    }

    const FString PackageName = Asset->GetOutermost() ? Asset->GetOutermost()->GetName() : FString();
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("asset_path"), AssetPath);
    Item->SetArrayField(TEXT("referencers"), PackageNamesToJson(Referencers));
    TArray<UObject*> AssetsToDelete;
    AssetsToDelete.Add(Asset);
    const bool bDeleted = bDryRun || ObjectTools::DeleteObjectsUnchecked(AssetsToDelete) > 0;
    const bool bReleasedPackage = !bDryRun && bDeleted && bCleanupAfterDelete && ReleasePackageAfterDelete(PackageName);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bDeleted);
    TSharedPtr<FJsonObject> Data = MakeAssetWriteData(bDryRun, !bDryRun && bDeleted, bDeleted, MakeAssetOpDiff(TEXT("assets_deleted"), Item), PackageName, false, !bDryRun && bDeleted);
    Data->SetBoolField(TEXT("cleanup_after_delete"), bCleanupAfterDelete);
    Data->SetBoolField(TEXT("released_package"), bReleasedPackage);
    Data->SetBoolField(TEXT("package_still_loaded"), !PackageName.IsEmpty() && FindPackage(nullptr, *PackageName) != nullptr);
    Response->SetObjectField(TEXT("data"), Data);
    if (!bDeleted)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_delete_failed"), TEXT("Asset delete failed")));
    }
    return Response;
}
}
