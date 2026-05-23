#include "UeNodeNexusBridgeOperations.h"

#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "UeNodeNexusBridgeBlueprintPatchOps.h"
#include "UeNodeNexusBridgeJson.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchOps.h"

namespace UeNodeNexusBridge
{
static UObject* LoadGraphAsset(const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutResponse, const FString& Operation, const FString& RequestId)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return nullptr;
    }

    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (Asset == nullptr)
    {
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), MakeError(TEXT("asset_not_found"), TEXT("Asset could not be loaded")));
        return nullptr;
    }
    return Asset;
}

TSharedPtr<FJsonObject> HandleGraphPatchApply(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UObject* Asset = LoadGraphAsset(Payload, EarlyResponse, Operation, RequestId);
    if (Asset == nullptr)
    {
        return EarlyResponse;
    }
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        return HandleBlueprintGraphPatch(Operation, RequestId, Blueprint, Payload);
    }
    if (UMaterial* Material = Cast<UMaterial>(Asset))
    {
        return HandleMaterialGraphPatch(Operation, RequestId, Material, Payload);
    }
    if (UMaterialFunction* Function = Cast<UMaterialFunction>(Asset))
    {
        return HandleMaterialFunctionGraphPatch(Operation, RequestId, Function, Payload);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(TEXT("unsupported_asset_class"), TEXT("graph_patch_apply supports Blueprint, Material, and MaterialFunction assets")));
    return Response;
}

TSharedPtr<FJsonObject> HandleGraphBuildApply(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UObject* Asset = LoadGraphAsset(Payload, EarlyResponse, Operation, RequestId);
    if (Asset == nullptr)
    {
        return EarlyResponse;
    }
    if (UMaterial* Material = Cast<UMaterial>(Asset))
    {
        return HandleMaterialGraphBuild(Operation, RequestId, Material, Payload);
    }
    if (UMaterialFunction* Function = Cast<UMaterialFunction>(Asset))
    {
        return HandleMaterialFunctionGraphBuild(Operation, RequestId, Function, Payload);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(TEXT("unsupported_asset_class"), TEXT("graph_build_apply supports Material and MaterialFunction assets")));
    return Response;
}

TSharedPtr<FJsonObject> HandleNodeParamsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UObject* Asset = LoadGraphAsset(Payload, EarlyResponse, Operation, RequestId);
    if (Asset == nullptr)
    {
        return EarlyResponse;
    }
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        return HandleBlueprintNodeParamsGet(Operation, RequestId, Blueprint, Payload);
    }
    if (UMaterial* Material = Cast<UMaterial>(Asset))
    {
        return HandleMaterialNodeParamsGet(Operation, RequestId, Material, Payload);
    }
    if (UMaterialFunction* Function = Cast<UMaterialFunction>(Asset))
    {
        return HandleMaterialFunctionNodeParamsGet(Operation, RequestId, Function, Payload);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(TEXT("unsupported_asset_class"), TEXT("node_params_get supports Blueprint, Material, and MaterialFunction assets")));
    return Response;
}

TSharedPtr<FJsonObject> HandleNodeParamsSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UObject* Asset = LoadGraphAsset(Payload, EarlyResponse, Operation, RequestId);
    if (Asset == nullptr)
    {
        return EarlyResponse;
    }
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        return HandleBlueprintNodeParamsSet(Operation, RequestId, Blueprint, Payload);
    }
    if (UMaterial* Material = Cast<UMaterial>(Asset))
    {
        return HandleMaterialNodeParamsSet(Operation, RequestId, Material, Payload);
    }
    if (UMaterialFunction* Function = Cast<UMaterialFunction>(Asset))
    {
        return HandleMaterialFunctionNodeParamsSet(Operation, RequestId, Function, Payload);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(TEXT("unsupported_asset_class"), TEXT("node_params_set supports Blueprint, Material, and MaterialFunction assets")));
    return Response;
}
}
