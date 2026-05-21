#include "UeNodeNexusBridgeOperations.h"

#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "UeNodeNexusBridgeBlueprintNodeInterfaceOps.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialNodeInterfaceOps.h"

namespace UeNodeNexusBridge
{
static UObject* LoadGraphAssetOrError(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutError)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        OutError = MakeEnvelope(Operation, RequestId, false);
        OutError->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return nullptr;
    }

    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (Asset == nullptr)
    {
        OutError = MakeEnvelope(Operation, RequestId, false);
        OutError->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_not_found"), TEXT("Asset could not be loaded")));
    }
    return Asset;
}

static TSharedPtr<FJsonObject> DispatchNodeAsset(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload, bool bOffset)
{
    TSharedPtr<FJsonObject> Error;
    UObject* Asset = LoadGraphAssetOrError(Operation, RequestId, Payload, Error);
    if (Asset == nullptr)
    {
        return Error;
    }

    if (UMaterial* Material = Cast<UMaterial>(Asset))
    {
        if (Operation == TEXT("node_info_get"))
        {
            return HandleMaterialNodeInfoGet(Operation, RequestId, Material, Payload);
        }
        if (Operation == TEXT("node_position_get"))
        {
            return HandleMaterialNodePositionGet(Operation, RequestId, Material, Payload);
        }
        if (Operation == TEXT("node_create"))
        {
            return HandleMaterialNodeCreate(Operation, RequestId, Material, Payload);
        }
        return HandleMaterialNodePositionSet(Operation, RequestId, Material, Payload, bOffset);
    }

    if (UMaterialFunction* Function = Cast<UMaterialFunction>(Asset))
    {
        if (Operation == TEXT("node_info_get"))
        {
            return HandleMaterialFunctionNodeInfoGet(Operation, RequestId, Function, Payload);
        }
        if (Operation == TEXT("node_position_get"))
        {
            return HandleMaterialFunctionNodePositionGet(Operation, RequestId, Function, Payload);
        }
        if (Operation == TEXT("node_create"))
        {
            return HandleMaterialFunctionNodeCreate(Operation, RequestId, Function, Payload);
        }
        return HandleMaterialFunctionNodePositionSet(Operation, RequestId, Function, Payload, bOffset);
    }

    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        if (Operation == TEXT("node_info_get"))
        {
            return HandleBlueprintNodeInfoGet(Operation, RequestId, Blueprint, Payload);
        }
        if (Operation == TEXT("node_position_get"))
        {
            return HandleBlueprintNodePositionGet(Operation, RequestId, Blueprint, Payload);
        }
        if (Operation == TEXT("node_create"))
        {
            return HandleBlueprintNodeCreate(Operation, RequestId, Blueprint, Payload);
        }
        return HandleBlueprintNodePositionSet(Operation, RequestId, Blueprint, Payload, bOffset);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("unsupported_asset_class"), TEXT("Node interface supports Material, MaterialFunction, and Blueprint assets")));
    return Response;
}

TSharedPtr<FJsonObject> HandleNodeInfoGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return DispatchNodeAsset(Operation, RequestId, Payload, false);
}

TSharedPtr<FJsonObject> HandleNodePositionGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return DispatchNodeAsset(Operation, RequestId, Payload, false);
}

TSharedPtr<FJsonObject> HandleNodePositionSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return DispatchNodeAsset(Operation, RequestId, Payload, false);
}

TSharedPtr<FJsonObject> HandleNodePositionOffset(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return DispatchNodeAsset(Operation, RequestId, Payload, true);
}

TSharedPtr<FJsonObject> HandleNodeCreate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return DispatchNodeAsset(Operation, RequestId, Payload, false);
}
}
