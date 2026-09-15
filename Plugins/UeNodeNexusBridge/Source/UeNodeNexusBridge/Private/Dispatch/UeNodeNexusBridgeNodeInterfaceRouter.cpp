#include "UeNodeNexusBridgeOperations.h"

#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "NodeInterface/UeNodeNexusBridgeBlueprintNodeInterfaceOps.h"
#include "UeNodeNexusBridgeJson.h"
#include "NodeInterface/UeNodeNexusBridgeMaterialNodeInterfaceOps.h"

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

static TSharedPtr<FJsonObject> DispatchNodeAsset(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const bool bInfo = Operation == TEXT("node_info_get");
    const bool bCreate = Operation == TEXT("node_create");
    if (!bInfo && !bCreate)
    {
        TSharedPtr<FJsonObject> Unsupported = MakeEnvelope(Operation, RequestId, false);
        Unsupported->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), FString::Printf(TEXT("node interface does not handle %s"), *Operation)));
        return Unsupported;
    }

    TSharedPtr<FJsonObject> Error;
    UObject* Asset = LoadGraphAssetOrError(Operation, RequestId, Payload, Error);
    if (Asset == nullptr)
    {
        return Error;
    }

    if (UMaterial* Material = Cast<UMaterial>(Asset))
    {
        if (bInfo)
        {
            return HandleMaterialNodeInfoGet(Operation, RequestId, Material, Payload);
        }
        return HandleMaterialNodeCreate(Operation, RequestId, Material, Payload);
    }

    if (UMaterialFunction* Function = Cast<UMaterialFunction>(Asset))
    {
        if (bInfo)
        {
            return HandleMaterialFunctionNodeInfoGet(Operation, RequestId, Function, Payload);
        }
        return HandleMaterialFunctionNodeCreate(Operation, RequestId, Function, Payload);
    }

    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        if (bInfo)
        {
            return HandleBlueprintNodeInfoGet(Operation, RequestId, Blueprint, Payload);
        }
        return HandleBlueprintNodeCreate(Operation, RequestId, Blueprint, Payload);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("unsupported_asset_class"), TEXT("Node interface supports Material, MaterialFunction, and Blueprint assets")));
    return Response;
}

TSharedPtr<FJsonObject> HandleNodeInfoGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return DispatchNodeAsset(Operation, RequestId, Payload);
}

TSharedPtr<FJsonObject> HandleNodeCreate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return DispatchNodeAsset(Operation, RequestId, Payload);
}
}
