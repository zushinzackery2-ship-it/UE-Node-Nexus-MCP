#include "UeNodeNexusVfxBridgeModule.h"

#include "UeNodeNexusBridgeOperationRegistry.h"
#include "UeNodeNexusNiagaraOps.h"
#include "UeNodeNexusVfxTranscode.h"
#include "BuildInfo/NexusVfxBuildInfo.h"
#include "UeNodeNexusBridgeBuildInfo.h"

IMPLEMENT_MODULE(FUeNodeNexusVfxBridgeModule, UeNodeNexusVfxBridge)

namespace
{
using FHandler = UeNodeNexusBridge::FBridgeOperationHandler;

struct FVfxOperation
{
    const TCHAR* Name;
    FHandler Handler;
};

const TArray<FVfxOperation>& VfxOperations()
{
    static const TArray<FVfxOperation> Operations = {
        { TEXT("cascade_system_summary_get"), UeNodeNexusBridge::HandleCascadeSystemSummaryGet },
        { TEXT("niagara_system_create"), UeNodeNexusBridge::HandleNiagaraSystemCreate },
        { TEXT("niagara_system_duplicate"), UeNodeNexusBridge::HandleNiagaraSystemDuplicate },
        { TEXT("niagara_system_summary_get"), UeNodeNexusBridge::HandleNiagaraSystemSummaryGet },
        { TEXT("niagara_emitters_list"), UeNodeNexusBridge::HandleNiagaraEmittersList },
        { TEXT("niagara_user_params_get"), UeNodeNexusBridge::HandleNiagaraUserParamsGet },
        { TEXT("niagara_user_params_set"), UeNodeNexusBridge::HandleNiagaraUserParamsSet },
        { TEXT("niagara_materials_get"), UeNodeNexusBridge::HandleNiagaraMaterialsGet },
        { TEXT("niagara_materials_set"), UeNodeNexusBridge::HandleNiagaraMaterialsSet },
        { TEXT("niagara_system_properties_get"), UeNodeNexusBridge::HandleNiagaraSystemPropertiesGet },
        { TEXT("niagara_system_properties_set"), UeNodeNexusBridge::HandleNiagaraSystemPropertiesSet },
        { TEXT("niagara_emitter_create"), UeNodeNexusBridge::HandleNiagaraEmitterCreate },
        { TEXT("niagara_emitter_properties_get"), UeNodeNexusBridge::HandleNiagaraEmitterPropertiesGet },
        { TEXT("niagara_emitter_properties_set"), UeNodeNexusBridge::HandleNiagaraEmitterPropertiesSet },
        { TEXT("niagara_modules_list"), UeNodeNexusBridge::HandleNiagaraModulesList },
        { TEXT("niagara_module_add"), UeNodeNexusBridge::HandleNiagaraModuleAdd },
        { TEXT("niagara_module_remove"), UeNodeNexusBridge::HandleNiagaraModuleRemove },
        { TEXT("niagara_module_set_enabled"), UeNodeNexusBridge::HandleNiagaraModuleSetEnabled },
        { TEXT("niagara_module_inputs_get"), UeNodeNexusBridge::HandleNiagaraModuleInputsGet },
        { TEXT("niagara_module_inputs_set"), UeNodeNexusBridge::HandleNiagaraModuleInputsSet },
        { TEXT("niagara_renderers_list"), UeNodeNexusBridge::HandleNiagaraRenderersList },
        { TEXT("niagara_renderer_create"), UeNodeNexusBridge::HandleNiagaraRendererCreate },
        { TEXT("niagara_renderer_properties_get"), UeNodeNexusBridge::HandleNiagaraRendererPropertiesGet },
        { TEXT("niagara_renderer_properties_set"), UeNodeNexusBridge::HandleNiagaraRendererPropertiesSet },
        { TEXT("niagara_compile"), UeNodeNexusBridge::HandleNiagaraCompile },
        { TEXT("niagara_asset_lint"), UeNodeNexusBridge::HandleNiagaraAssetLint },
        { TEXT("vfx_transcode_export"), UeNodeNexusBridge::HandleVfxTranscodeExport },
        { TEXT("vfx_transcode_apply"), UeNodeNexusBridge::HandleVfxTranscodeApply },
    };
    return Operations;
}
}

void FUeNodeNexusVfxBridgeModule::StartupModule()
{
    UeNodeNexusBridge::RegisterVfxBuildIdentity();
    for (const FVfxOperation& Operation : VfxOperations())
    {
        UeNodeNexusBridge::RegisterOperationHandler(FString(Operation.Name), Operation.Handler);
    }
}

void FUeNodeNexusVfxBridgeModule::ShutdownModule()
{
    UeNodeNexusBridge::UnregisterBuildIdentity(TEXT("UeNodeNexusVfxBridge"));
    for (const FVfxOperation& Operation : VfxOperations())
    {
        UeNodeNexusBridge::UnregisterOperationHandler(FString(Operation.Name));
    }
}
