#include "UeNodeNexusBridgeOperations.h"

#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> UnsupportedOperation(const FString& Operation, const FString& RequestId)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(TEXT("operation_not_implemented"), TEXT("Operation is known by the MCP contract but not implemented in this bridge build")));
    return Response;
}

TSharedPtr<FJsonObject> HandleDiagnosticsGet(const FString& Operation, const FString& RequestId)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("items"), TArray<TSharedPtr<FJsonValue>>());
    Data->SetStringField(TEXT("source"), TEXT("UeNodeNexusBridge"));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> DispatchOperation(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    if (Operation == TEXT("asset_list"))
    {
        return HandleAssetList(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("asset_get"))
    {
        return HandleAssetGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("level_current_get"))
    {
        return HandleLevelCurrentGet(Operation, RequestId);
    }
    if (Operation == TEXT("level_actors_list"))
    {
        return HandleLevelActorsList(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("graph_snapshot_get"))
    {
        return HandleGraphSnapshotGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("graph_patch_apply"))
    {
        return HandleGraphPatchApply(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("node_params_get"))
    {
        return HandleNodeParamsGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("node_params_set"))
    {
        return HandleNodeParamsSet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("material_instance_params_get"))
    {
        return HandleMaterialInstanceParamsGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("material_instance_params_set"))
    {
        return HandleMaterialInstanceParamsSet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("diagnostics_get"))
    {
        return HandleDiagnosticsGet(Operation, RequestId);
    }
    if (Operation == TEXT("asset_compile"))
    {
        return HandleAssetCompile(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("asset_validate"))
    {
        return HandleAssetValidate(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("asset_save"))
    {
        return HandleAssetSave(Operation, RequestId, Payload);
    }

    return UnsupportedOperation(Operation, RequestId);
}
}
