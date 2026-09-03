#include "UeNodeNexusBridgeCoreOperationsRegistry.h"

#include "UeNodeNexusBridgeOperationRegistry.h"
#include "UeNodeNexusBridgeOperations.h"

namespace UeNodeNexusBridge
{
namespace
{
void RegisterCoreOp(const TCHAR* Operation, FBridgeOperationHandler Handler)
{
    RegisterOperationHandler(FString(Operation), MoveTemp(Handler));
}
}

void RegisterCoreOperations()
{
    RegisterCoreOp(TEXT("sound_cue_summary_get"), HandleSoundCueSummaryGet);
    RegisterCoreOp(TEXT("texture_summary_get"), HandleTextureSummaryGet);
    RegisterCoreOp(TEXT("asset_list"), HandleAssetList);
    RegisterCoreOp(TEXT("asset_get"), HandleAssetGet);
    RegisterCoreOp(TEXT("asset_create"), HandleAssetCreate);
    RegisterCoreOp(TEXT("asset_delete"), HandleAssetDelete);
    RegisterCoreOp(TEXT("asset_move"), HandleAssetMove);
    RegisterCoreOp(TEXT("asset_rename"), HandleAssetRename);
    RegisterCoreOp(TEXT("asset_move_batch"), HandleAssetMoveBatch);
    RegisterCoreOp(TEXT("asset_rename_batch"), HandleAssetRenameBatch);
    RegisterCoreOp(TEXT("asset_duplicate"), HandleAssetDuplicate);
    RegisterCoreOp(TEXT("folder_create"), HandleFolderCreate);
    RegisterCoreOp(TEXT("folder_delete"), HandleFolderDelete);
    RegisterCoreOp(TEXT("asset_redirectors_fixup"), HandleAssetRedirectorsFixup);
    RegisterCoreOp(TEXT("asset_dependencies_get"), HandleAssetDependenciesGet);
    RegisterCoreOp(TEXT("asset_referencers_get"), HandleAssetReferencersGet);
    RegisterCoreOp(TEXT("level_open"), HandleLevelOpen);
    RegisterCoreOp(TEXT("level_actors_list"), HandleLevelActorsList);
    RegisterCoreOp(TEXT("level_actor_get"), HandleLevelActorGet);
    RegisterCoreOp(TEXT("level_actor_spawn"), HandleLevelActorSpawn);
    RegisterCoreOp(TEXT("level_actor_delete"), HandleLevelActorDelete);
    RegisterCoreOp(TEXT("level_actor_transform_get"), HandleLevelActorTransformGet);
    RegisterCoreOp(TEXT("level_actor_transform_set"), HandleLevelActorTransformSet);
    RegisterCoreOp(TEXT("object_properties_get"), HandleObjectPropertiesGet);
    RegisterCoreOp(TEXT("level_actor_properties_set"), HandleLevelActorPropertiesSet);
    RegisterCoreOp(TEXT("landscape_layer_info_set"), HandleLandscapeLayerInfoSet);
    RegisterCoreOp(TEXT("level_mesh_instances_list"), HandleLevelMeshInstancesList);
    RegisterCoreOp(TEXT("component_materials_get"), HandleComponentMaterialsGet);
    RegisterCoreOp(TEXT("component_materials_set"), HandleComponentMaterialsSet);
    RegisterCoreOp(TEXT("material_interface_resolve"), HandleMaterialInterfaceResolve);
    RegisterCoreOp(TEXT("material_usage_find"), HandleMaterialUsageFind);
    RegisterCoreOp(TEXT("component_material_instance_params_get"), HandleComponentMaterialInstanceParamsGet);
    RegisterCoreOp(TEXT("component_material_instance_params_set"), HandleComponentMaterialInstanceParamsSet);
    RegisterCoreOp(TEXT("project_input_mappings_get"), HandleProjectInputMappingsGet);
    RegisterCoreOp(TEXT("project_input_mappings_patch"), HandleProjectInputMappingsPatch);
    RegisterCoreOp(TEXT("input_action_create"), HandleInputActionCreate);
    RegisterCoreOp(TEXT("input_mapping_context_create"), HandleInputMappingContextCreate);
    RegisterCoreOp(TEXT("input_mapping_context_entry_add"), HandleInputMappingContextEntryAdd);
    RegisterCoreOp(TEXT("input_mapping_context_get"), HandleInputMappingContextGet);
    RegisterCoreOp(TEXT("blueprint_details_get"), HandleBlueprintDetailsGet);
    RegisterCoreOp(TEXT("blueprint_components_patch"), HandleBlueprintComponentsPatch);
    RegisterCoreOp(TEXT("anim_blueprint_summary_get"), HandleAnimBlueprintSummaryGet);
    RegisterCoreOp(TEXT("anim_state_machine_summary_get"), HandleAnimStateMachineSummaryGet);
    RegisterCoreOp(TEXT("anim_state_machine_state_add"), HandleAnimStateMachineStateAdd);
    RegisterCoreOp(TEXT("anim_state_machine_transition_add"), HandleAnimStateMachineTransitionAdd);
    RegisterCoreOp(TEXT("anim_montage_summary_get"), HandleAnimMontageSummaryGet);
    RegisterCoreOp(TEXT("blend_space_summary_get"), HandleBlendSpaceSummaryGet);
    RegisterCoreOp(TEXT("graph_snapshot_get"), HandleGraphSnapshotGet);
    RegisterCoreOp(TEXT("graph_node_search"), HandleGraphNodeSearch);
    RegisterCoreOp(TEXT("graph_node_info_get"), HandleGraphNodeInfoGet);
    RegisterCoreOp(TEXT("node_class_params_get"), HandleNodeClassParamsGet);
    RegisterCoreOp(TEXT("graph_patch_apply"), HandleGraphPatchApply);
    RegisterCoreOp(TEXT("graph_build_apply"), HandleGraphBuildApply);
    RegisterCoreOp(TEXT("node_info_get"), HandleNodeInfoGet);
    RegisterCoreOp(TEXT("node_create"), HandleNodeCreate);
    RegisterCoreOp(TEXT("node_position_get"), HandleNodePositionGet);
    RegisterCoreOp(TEXT("node_position_set"), HandleNodePositionSet);
    RegisterCoreOp(TEXT("node_params_get"), HandleNodeParamsGet);
    RegisterCoreOp(TEXT("node_params_set"), HandleNodeParamsSet);
    RegisterCoreOp(TEXT("material_instance_params_get"), HandleMaterialInstanceParamsGet);
    RegisterCoreOp(TEXT("material_expression_classes_list"), HandleMaterialExpressionClassesList);
    RegisterCoreOp(TEXT("material_instance_params_set"), HandleMaterialInstanceParamsSet);
    RegisterCoreOp(TEXT("asset_compile"), HandleAssetCompile);
    RegisterCoreOp(TEXT("asset_validate"), HandleAssetValidate);
    RegisterCoreOp(TEXT("asset_save"), HandleAssetSave);
    RegisterCoreOp(TEXT("viewport_capture"), HandleViewportCapture);
    RegisterCoreOp(TEXT("viewport_camera_get"), HandleViewportCameraGet);
    RegisterCoreOp(TEXT("viewport_camera_set"), HandleViewportCameraSet);
    RegisterCoreOp(TEXT("editor_save_all"), HandleEditorSaveAll);
    RegisterCoreOp(TEXT("editor_request_exit"), HandleEditorRequestExit);
    RegisterCoreOp(TEXT("bridge_capabilities_get"), HandleBridgeCapabilitiesGet);
    RegisterCoreOp(TEXT("transcode_root_set"), HandleTranscodeRootSet);
    RegisterCoreOp(TEXT("transcode_status"), HandleTranscodeStatus);
    RegisterCoreOp(TEXT("transcode_export"), HandleTranscodeExport);
    RegisterCoreOp(TEXT("transcode_apply"), HandleTranscodeApply);
    RegisterCoreOp(TEXT("schema_export"), HandleSchemaExport);
    RegisterCoreOp(TEXT("transcode_watch_set"), HandleTranscodeWatchSet);

    // Handlers that take no payload need a small adapter to match the registry
    // handler signature.
    RegisterCoreOp(TEXT("level_current_get"),
        [](const FString& Op, const FString& ReqId, const TSharedPtr<FJsonObject>&)
        {
            return HandleLevelCurrentGet(Op, ReqId);
        });
    RegisterCoreOp(TEXT("project_context_get"),
        [](const FString& Op, const FString& ReqId, const TSharedPtr<FJsonObject>&)
        {
            return HandleProjectContextGet(Op, ReqId);
        });
    RegisterCoreOp(TEXT("diagnostics_get"), HandleDiagnosticsGet);

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
