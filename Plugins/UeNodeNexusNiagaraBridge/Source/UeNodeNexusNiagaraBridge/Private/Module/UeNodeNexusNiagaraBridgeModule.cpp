#include "UeNodeNexusNiagaraBridgeModule.h"

#include "UeNodeNexusBridgeOperationRegistry.h"
#include "UeNodeNexusNiagaraOps.h"

IMPLEMENT_MODULE(FUeNodeNexusNiagaraBridgeModule, UeNodeNexusNiagaraBridge)

namespace
{
using FHandler = UeNodeNexusBridge::FBridgeOperationHandler;

void RegisterNiagaraOperation(const TCHAR* Operation, FHandler Handler)
{
    UeNodeNexusBridge::RegisterOperationHandler(FString(Operation), MoveTemp(Handler));
}

void UnregisterNiagaraOperation(const TCHAR* Operation)
{
    UeNodeNexusBridge::UnregisterOperationHandler(FString(Operation));
}
}

void FUeNodeNexusNiagaraBridgeModule::StartupModule()
{
    RegisterNiagaraOperation(TEXT("niagara_system_create"), UeNodeNexusBridge::HandleNiagaraSystemCreate);
    RegisterNiagaraOperation(TEXT("niagara_system_duplicate"), UeNodeNexusBridge::HandleNiagaraSystemDuplicate);
    RegisterNiagaraOperation(TEXT("niagara_system_summary_get"), UeNodeNexusBridge::HandleNiagaraSystemSummaryGet);
    RegisterNiagaraOperation(TEXT("niagara_emitters_list"), UeNodeNexusBridge::HandleNiagaraEmittersList);
    RegisterNiagaraOperation(TEXT("niagara_user_params_get"), UeNodeNexusBridge::HandleNiagaraUserParamsGet);
    RegisterNiagaraOperation(TEXT("niagara_user_params_set"), UeNodeNexusBridge::HandleNiagaraUserParamsSet);
    RegisterNiagaraOperation(TEXT("niagara_materials_get"), UeNodeNexusBridge::HandleNiagaraMaterialsGet);
    RegisterNiagaraOperation(TEXT("niagara_materials_set"), UeNodeNexusBridge::HandleNiagaraMaterialsSet);
    RegisterNiagaraOperation(TEXT("niagara_system_properties_get"), UeNodeNexusBridge::HandleNiagaraSystemPropertiesGet);
    RegisterNiagaraOperation(TEXT("niagara_system_properties_set"), UeNodeNexusBridge::HandleNiagaraSystemPropertiesSet);
    RegisterNiagaraOperation(TEXT("niagara_emitter_create"), UeNodeNexusBridge::HandleNiagaraEmitterCreate);
    RegisterNiagaraOperation(TEXT("niagara_emitter_properties_get"), UeNodeNexusBridge::HandleNiagaraEmitterPropertiesGet);
    RegisterNiagaraOperation(TEXT("niagara_emitter_properties_set"), UeNodeNexusBridge::HandleNiagaraEmitterPropertiesSet);
    RegisterNiagaraOperation(TEXT("niagara_modules_list"), UeNodeNexusBridge::HandleNiagaraModulesList);
    RegisterNiagaraOperation(TEXT("niagara_module_add"), UeNodeNexusBridge::HandleNiagaraModuleAdd);
    RegisterNiagaraOperation(TEXT("niagara_module_remove"), UeNodeNexusBridge::HandleNiagaraModuleRemove);
    RegisterNiagaraOperation(TEXT("niagara_module_set_enabled"), UeNodeNexusBridge::HandleNiagaraModuleSetEnabled);
    RegisterNiagaraOperation(TEXT("niagara_module_inputs_get"), UeNodeNexusBridge::HandleNiagaraModuleInputsGet);
    RegisterNiagaraOperation(TEXT("niagara_module_inputs_set"), UeNodeNexusBridge::HandleNiagaraModuleInputsSet);
    RegisterNiagaraOperation(TEXT("niagara_renderers_list"), UeNodeNexusBridge::HandleNiagaraRenderersList);
    RegisterNiagaraOperation(TEXT("niagara_renderer_create"), UeNodeNexusBridge::HandleNiagaraRendererCreate);
    RegisterNiagaraOperation(TEXT("niagara_renderer_properties_get"), UeNodeNexusBridge::HandleNiagaraRendererPropertiesGet);
    RegisterNiagaraOperation(TEXT("niagara_renderer_properties_set"), UeNodeNexusBridge::HandleNiagaraRendererPropertiesSet);
    RegisterNiagaraOperation(TEXT("niagara_compile"), UeNodeNexusBridge::HandleNiagaraCompile);
    RegisterNiagaraOperation(TEXT("niagara_asset_lint"), UeNodeNexusBridge::HandleNiagaraAssetLint);
}

void FUeNodeNexusNiagaraBridgeModule::ShutdownModule()
{
    UnregisterNiagaraOperation(TEXT("niagara_system_create"));
    UnregisterNiagaraOperation(TEXT("niagara_system_duplicate"));
    UnregisterNiagaraOperation(TEXT("niagara_system_summary_get"));
    UnregisterNiagaraOperation(TEXT("niagara_emitters_list"));
    UnregisterNiagaraOperation(TEXT("niagara_user_params_get"));
    UnregisterNiagaraOperation(TEXT("niagara_user_params_set"));
    UnregisterNiagaraOperation(TEXT("niagara_materials_get"));
    UnregisterNiagaraOperation(TEXT("niagara_materials_set"));
    UnregisterNiagaraOperation(TEXT("niagara_system_properties_get"));
    UnregisterNiagaraOperation(TEXT("niagara_system_properties_set"));
    UnregisterNiagaraOperation(TEXT("niagara_emitter_create"));
    UnregisterNiagaraOperation(TEXT("niagara_emitter_properties_get"));
    UnregisterNiagaraOperation(TEXT("niagara_emitter_properties_set"));
    UnregisterNiagaraOperation(TEXT("niagara_modules_list"));
    UnregisterNiagaraOperation(TEXT("niagara_module_add"));
    UnregisterNiagaraOperation(TEXT("niagara_module_remove"));
    UnregisterNiagaraOperation(TEXT("niagara_module_set_enabled"));
    UnregisterNiagaraOperation(TEXT("niagara_module_inputs_get"));
    UnregisterNiagaraOperation(TEXT("niagara_module_inputs_set"));
    UnregisterNiagaraOperation(TEXT("niagara_renderers_list"));
    UnregisterNiagaraOperation(TEXT("niagara_renderer_create"));
    UnregisterNiagaraOperation(TEXT("niagara_renderer_properties_get"));
    UnregisterNiagaraOperation(TEXT("niagara_renderer_properties_set"));
    UnregisterNiagaraOperation(TEXT("niagara_compile"));
    UnregisterNiagaraOperation(TEXT("niagara_asset_lint"));
}
