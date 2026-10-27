#include "UeNodeNexusBridgeOperations.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "UeNodeNexusBridgeAssetManagementShared.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleFolderCreate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString FolderPath;
    if (!Payload->TryGetStringField(TEXT("folder_path"), FolderPath) || FolderPath.IsEmpty())
    {
        return MakeInvalidAssetRequest(Operation, RequestId, TEXT("folder_path is required"));
    }

    FText Reason;
    if (!FPackageName::IsValidLongPackageName(FolderPath, false, &Reason))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_folder_path"), Reason.ToString()));
        return Response;
    }

    const bool bDryRun = ReadAssetBoolField(Payload, TEXT("dry_run"), true);
    const FString Directory = FPackageName::LongPackageNameToFilename(FolderPath);
    const bool bExistsBefore = IFileManager::Get().DirectoryExists(*Directory);
    const bool bCreated = bDryRun || bExistsBefore || IFileManager::Get().MakeDirectory(*Directory, true);
    if (!bDryRun && bCreated)
    {
        FAssetRegistryModule::GetRegistry().ScanPathsSynchronous({ FolderPath }, true);
    }

    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("folder_path"), FolderPath);
    Item->SetStringField(TEXT("directory"), Directory);
    Item->SetBoolField(TEXT("existed_before"), bExistsBefore);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bCreated);
    Response->SetObjectField(TEXT("data"), MakeAssetWriteData(bDryRun, !bDryRun && bCreated && !bExistsBefore, bCreated && !bExistsBefore, MakeAssetOpDiff(TEXT("folders_created"), Item), FolderPath, false, false));
    if (!bCreated)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("folder_create_failed"), TEXT("Folder create failed")));
    }
    return Response;
}

TSharedPtr<FJsonObject> HandleFolderDelete(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString FolderPath;
    if (!Payload->TryGetStringField(TEXT("folder_path"), FolderPath) || FolderPath.IsEmpty())
    {
        return MakeInvalidAssetRequest(Operation, RequestId, TEXT("folder_path is required"));
    }

    FText Reason;
    if (!FPackageName::IsValidLongPackageName(FolderPath, false, &Reason))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_folder_path"), Reason.ToString()));
        return Response;
    }

    const bool bDryRun = ReadAssetBoolField(Payload, TEXT("dry_run"), true);
    const bool bRecursive = ReadAssetBoolField(Payload, TEXT("recursive"), true);
    const FString Directory = FPackageName::LongPackageNameToFilename(FolderPath);
    const bool bExistsBefore = IFileManager::Get().DirectoryExists(*Directory);

    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*FolderPath));
    Filter.bRecursivePaths = true;

    TArray<FAssetData> Assets;
    FAssetRegistryModule::GetRegistry().GetAssets(Filter, Assets);
    if (Assets.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> AssetPaths;
        for (const FAssetData& AssetData : Assets)
        {
            AssetPaths.Add(MakeShared<FJsonValueString>(AssetData.GetObjectPathString()));
        }

        TSharedPtr<FJsonObject> Data = MakeAssetWriteData(bDryRun, false, false, MakeEmptyDiff(), FolderPath, false, false);
        Data->SetNumberField(TEXT("asset_count"), Assets.Num());
        Data->SetArrayField(TEXT("assets"), AssetPaths);

        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("data"), Data);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("folder_not_empty"), TEXT("Folder contains assets; delete assets first")));
        return Response;
    }

    bool bDeleted = true;
    if (!bDryRun && bExistsBefore)
    {
        bDeleted = IFileManager::Get().DeleteDirectory(*Directory, false, bRecursive);
        FAssetRegistryModule::GetRegistry().ScanPathsSynchronous({ FolderPath }, true);
    }

    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("folder_path"), FolderPath);
    Item->SetStringField(TEXT("directory"), Directory);
    Item->SetBoolField(TEXT("existed_before"), bExistsBefore);
    Item->SetBoolField(TEXT("recursive"), bRecursive);

    const bool bChanged = !bDryRun && bExistsBefore && bDeleted;
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bDeleted);
    Response->SetObjectField(TEXT("data"), MakeAssetWriteData(bDryRun, bChanged, bChanged, MakeAssetOpDiff(TEXT("folders_deleted"), Item), FolderPath, false, false));
    if (!bDeleted)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("folder_delete_failed"), TEXT("Folder delete failed")));
    }
    return Response;
}
}
