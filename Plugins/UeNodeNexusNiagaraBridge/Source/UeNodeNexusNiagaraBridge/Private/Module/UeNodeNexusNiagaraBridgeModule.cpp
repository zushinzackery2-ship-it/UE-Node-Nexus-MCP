#include "UeNodeNexusNiagaraBridgeModule.h"

#include "UeNodeNexusBridgeOperationRegistry.h"
#include "UeNodeNexusNiagaraOps.h"

IMPLEMENT_MODULE(FUeNodeNexusNiagaraBridgeModule, UeNodeNexusNiagaraBridge)

namespace
{
using FHandler = UeNodeNexusBridge::FBridgeOperationHandler;

struct FNiagaraOperation
{
    const TCHAR* Name;
    FHandler Handler;
};

// Single source for the Niagara op set: StartupModule registers every entry and
// ShutdownModule unregisters by name, so the two can never drift. The names stay
// as TEXT() literals so the offline contract-sync test still parses them.
const TArray<FNiagaraOperation>& NiagaraOperations()
{
    static const TArray<FNiagaraOperation> Operations = {
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
    };
    return Operations;
}
}

void FUeNodeNexusNiagaraBridgeModule::StartupModule()
{
    for (const FNiagaraOperation& Operation : NiagaraOperations())
    {
        UeNodeNexusBridge::RegisterOperationHandler(FString(Operation.Name), Operation.Handler);
    }
}

void FUeNodeNexusNiagaraBridgeModule::ShutdownModule()
{
    for (const FNiagaraOperation& Operation : NiagaraOperations())
    {
        UeNodeNexusBridge::UnregisterOperationHandler(FString(Operation.Name));
    }
}
