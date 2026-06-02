#include "UeNodeNexusBridgeCoreOperationsRegistry.h"

#include "UeNodeNexusBridgeOperationRegistry.h"
#include "UeNodeNexusBridgeOperations.h"

namespace UeNodeNexusBridge
{
namespace
{
void Register(const TCHAR* Operation, FBridgeOperationHandler Handler)
{
    RegisterOperationHandler(FString(Operation), MoveTemp(Handler));
}
}

void RegisterCoreOperations()
{
    Register(TEXT("asset_list"), HandleAssetList);
    Register(TEXT("asset_get"), HandleAssetGet);
    Register(TEXT("asset_create"), HandleAssetCreate);
    Register(TEXT("asset_delete"), HandleAssetDelete);
    Register(TEXT("asset_move"), HandleAssetMove);
    Register(TEXT("asset_rename"), HandleAssetRename);
    Register(TEXT("asset_move_batch"), HandleAssetMoveBatch);
    Register(TEXT("asset_rename_batch"), HandleAssetRenameBatch);
    Register(TEXT("asset_duplicate"), HandleAssetDuplicate);
    Register(TEXT("folder_create"), HandleFolderCreate);
    Register(TEXT("folder_delete"), HandleFolderDelete);
    Register(TEXT("asset_redirectors_fixup"), HandleAssetRedirectorsFixup);
    Register(TEXT("level_actors_list"), HandleLevelActorsList);
    Register(TEXT("level_actor_get"), HandleLevelActorGet);
    Register(TEXT("level_actor_transform_get"), HandleLevelActorTransformGet);
    Register(TEXT("object_properties_get"), HandleObjectPropertiesGet);
    Register(TEXT("level_mesh_instances_list"), HandleLevelMeshInstancesList);
    Register(TEXT("component_materials_get"), HandleComponentMaterialsGet);
    Register(TEXT("component_materials_set"), HandleComponentMaterialsSet);
    Register(TEXT("material_interface_resolve"), HandleMaterialInterfaceResolve);
    Register(TEXT("material_usage_find"), HandleMaterialUsageFind);
    Register(TEXT("component_material_instance_params_get"), HandleComponentMaterialInstanceParamsGet);
    Register(TEXT("component_material_instance_params_set"), HandleComponentMaterialInstanceParamsSet);
    Register(TEXT("project_input_mappings_get"), HandleProjectInputMappingsGet);
    Register(TEXT("project_input_mappings_patch"), HandleProjectInputMappingsPatch);
    Register(TEXT("blueprint_details_get"), HandleBlueprintDetailsGet);
    Register(TEXT("blueprint_components_patch"), HandleBlueprintComponentsPatch);
    Register(TEXT("anim_blueprint_summary_get"), HandleAnimBlueprintSummaryGet);
    Register(TEXT("anim_montage_summary_get"), HandleAnimMontageSummaryGet);
    Register(TEXT("blend_space_summary_get"), HandleBlendSpaceSummaryGet);
    Register(TEXT("cascade_system_summary_get"), HandleCascadeSystemSummaryGet);
    Register(TEXT("graph_snapshot_get"), HandleGraphSnapshotGet);
    Register(TEXT("graph_node_info_get"), HandleGraphNodeInfoGet);
    Register(TEXT("node_class_params_get"), HandleNodeClassParamsGet);
    Register(TEXT("graph_patch_apply"), HandleGraphPatchApply);
    Register(TEXT("graph_build_apply"), HandleGraphBuildApply);
    Register(TEXT("node_info_get"), HandleNodeInfoGet);
    Register(TEXT("node_create"), HandleNodeCreate);
    Register(TEXT("node_position_get"), HandleNodePositionGet);
    Register(TEXT("node_position_set"), HandleNodePositionSet);
    Register(TEXT("node_params_get"), HandleNodeParamsGet);
    Register(TEXT("node_params_set"), HandleNodeParamsSet);
    Register(TEXT("material_instance_params_get"), HandleMaterialInstanceParamsGet);
    Register(TEXT("material_expression_classes_list"), HandleMaterialExpressionClassesList);
    Register(TEXT("material_instance_params_set"), HandleMaterialInstanceParamsSet);
    Register(TEXT("asset_compile"), HandleAssetCompile);
    Register(TEXT("asset_validate"), HandleAssetValidate);
    Register(TEXT("asset_save"), HandleAssetSave);
    Register(TEXT("editor_save_all"), HandleEditorSaveAll);
    Register(TEXT("editor_request_exit"), HandleEditorRequestExit);
    Register(TEXT("bridge_capabilities_get"), HandleBridgeCapabilitiesGet);

    // Handlers that take no payload need a small adapter to match the registry
    // handler signature.
    Register(TEXT("level_current_get"),
        [](const FString& Op, const FString& ReqId, const TSharedPtr<FJsonObject>&)
        {
            return HandleLevelCurrentGet(Op, ReqId);
        });
    Register(TEXT("project_context_get"),
        [](const FString& Op, const FString& ReqId, const TSharedPtr<FJsonObject>&)
        {
            return HandleProjectContextGet(Op, ReqId);
        });
    Register(TEXT("diagnostics_get"),
        [](const FString& Op, const FString& ReqId, const TSharedPtr<FJsonObject>&)
        {
            return HandleDiagnosticsGet(Op, ReqId);
        });

    // Completeness guard: every operation reported by GetCoreOperationNames must
    // actually have a registered handler, or capabilities would advertise an
    // operation that dispatches to "not implemented".
    for (const FString& Name : GetCoreOperationNames())
    {
        ensureMsgf(IsOperationRegistered(Name), TEXT("Core operation %s has no registered handler"), *Name);
    }
}

void UnregisterCoreOperations()
{
    for (const FString& Name : GetCoreOperationNames())
    {
        UnregisterOperationHandler(Name);
    }
}
}
