#include "UeNodeNexusBridgeOperations.h"

#include "UeNodeNexusBridgeAssetManagementShared.h"
#include "Deletion/NexusAssetDeletion.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleAssetDelete(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        return MakeInvalidAssetRequest(Operation, RequestId, TEXT("asset_path is required"));
    }
    return DeleteAssetSet(Operation, RequestId, { AssetPath }, Payload);
}

TSharedPtr<FJsonObject> HandleAssetDeleteBatch(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TArray<FString> AssetPaths;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (Payload->TryGetArrayField(TEXT("asset_paths"), Values) && Values != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Values)
        {
            FString Path;
            if (!Value->TryGetString(Path) || Path.IsEmpty())
            {
                return MakeInvalidAssetRequest(Operation, RequestId, TEXT("asset_paths must contain non-empty strings"));
            }
            AssetPaths.Add(Path);
        }
    }
    if (AssetPaths.IsEmpty())
    {
        return MakeInvalidAssetRequest(Operation, RequestId, TEXT("asset_paths is required"));
    }
    return DeleteAssetSet(Operation, RequestId, AssetPaths, Payload);
}
}
