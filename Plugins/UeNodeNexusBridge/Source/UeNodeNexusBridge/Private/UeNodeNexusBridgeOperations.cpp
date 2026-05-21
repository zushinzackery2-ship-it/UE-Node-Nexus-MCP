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
    if (Operation == TEXT("asset_create"))
    {
        return HandleAssetCreate(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("asset_delete"))
    {
        return HandleAssetDelete(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("asset_move"))
    {
        return HandleAssetMove(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("asset_rename"))
    {
        return HandleAssetRename(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("asset_move_batch"))
    {
        return HandleAssetMoveBatch(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("asset_rename_batch"))
    {
        return HandleAssetRenameBatch(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("asset_duplicate"))
    {
        return HandleAssetDuplicate(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("folder_create"))
    {
        return HandleFolderCreate(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("folder_delete"))
    {
        return HandleFolderDelete(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("asset_redirectors_fixup"))
    {
        return HandleAssetRedirectorsFixup(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("level_current_get"))
    {
        return HandleLevelCurrentGet(Operation, RequestId);
    }
    if (Operation == TEXT("level_actors_list"))
    {
        return HandleLevelActorsList(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("level_actor_get"))
    {
        return HandleLevelActorGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("level_actor_transform_get"))
    {
        return HandleLevelActorTransformGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("level_actor_transform_set"))
    {
        return HandleLevelActorTransformSet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("object_properties_get"))
    {
        return HandleObjectPropertiesGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("object_properties_set"))
    {
        return HandleObjectPropertiesSet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("level_mesh_instances_list"))
    {
        return HandleLevelMeshInstancesList(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("component_materials_get"))
    {
        return HandleComponentMaterialsGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("component_materials_set"))
    {
        return HandleComponentMaterialsSet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("material_interface_resolve"))
    {
        return HandleMaterialInterfaceResolve(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("material_usage_find"))
    {
        return HandleMaterialUsageFind(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("component_material_instance_params_get"))
    {
        return HandleComponentMaterialInstanceParamsGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("component_material_instance_params_set"))
    {
        return HandleComponentMaterialInstanceParamsSet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("blueprint_details_get"))
    {
        return HandleBlueprintDetailsGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("anim_blueprint_summary_get"))
    {
        return HandleAnimBlueprintSummaryGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("graph_snapshot_get"))
    {
        return HandleGraphSnapshotGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("graph_node_info_get"))
    {
        return HandleGraphNodeInfoGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("graph_node_info_get_w_pos"))
    {
        return HandleGraphNodeInfoGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("node_class_params_get"))
    {
        return HandleNodeClassParamsGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("graph_patch_apply"))
    {
        return HandleGraphPatchApply(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("graph_build_apply"))
    {
        return HandleGraphBuildApply(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("node_info_get"))
    {
        return HandleNodeInfoGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("node_create"))
    {
        return HandleNodeCreate(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("node_position_get"))
    {
        return HandleNodePositionGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("node_position_set"))
    {
        return HandleNodePositionSet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("node_position_offset"))
    {
        return HandleNodePositionOffset(Operation, RequestId, Payload);
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
    if (Operation == TEXT("material_expression_classes_list"))
    {
        return HandleMaterialExpressionClassesList(Operation, RequestId, Payload);
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
    if (Operation == TEXT("editor_save_all"))
    {
        return HandleEditorSaveAll(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("editor_request_exit"))
    {
        return HandleEditorRequestExit(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_enable"))
    {
        return HandleAutoIndexEnable(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_disable"))
    {
        return HandleAutoIndexDisable(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_status"))
    {
        return HandleAutoIndexStatus(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_rebuild"))
    {
        return HandleAutoIndexRebuild(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_flush"))
    {
        return HandleAutoIndexFlush(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_clear"))
    {
        return HandleAutoIndexClear(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_overview"))
    {
        return HandleAutoIndexOverview(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_tree_get"))
    {
        return HandleAutoIndexTreeGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_query"))
    {
        return HandleAutoIndexQuery(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_get"))
    {
        return HandleAutoIndexGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_resolve_path"))
    {
        return HandleAutoIndexResolvePath(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_diff_registry"))
    {
        return HandleAutoIndexDiffRegistry(Operation, RequestId, Payload);
    }

    return UnsupportedOperation(Operation, RequestId);
}
}
