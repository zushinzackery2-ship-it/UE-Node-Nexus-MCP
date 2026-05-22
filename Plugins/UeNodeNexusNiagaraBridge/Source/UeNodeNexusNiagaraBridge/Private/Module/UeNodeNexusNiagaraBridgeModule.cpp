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
    RegisterNiagaraOperation(TEXT("niagara_template_duplicate"), UeNodeNexusBridge::HandleNiagaraTemplateDuplicate);
    RegisterNiagaraOperation(TEXT("niagara_system_summary_get"), UeNodeNexusBridge::HandleNiagaraSystemSummaryGet);
    RegisterNiagaraOperation(TEXT("niagara_emitters_list"), UeNodeNexusBridge::HandleNiagaraEmittersList);
    RegisterNiagaraOperation(TEXT("niagara_user_params_get"), UeNodeNexusBridge::HandleNiagaraUserParamsGet);
    RegisterNiagaraOperation(TEXT("niagara_user_params_set"), UeNodeNexusBridge::HandleNiagaraUserParamsSet);
    RegisterNiagaraOperation(TEXT("niagara_materials_get"), UeNodeNexusBridge::HandleNiagaraMaterialsGet);
    RegisterNiagaraOperation(TEXT("niagara_materials_set"), UeNodeNexusBridge::HandleNiagaraMaterialsSet);
    RegisterNiagaraOperation(TEXT("niagara_compile"), UeNodeNexusBridge::HandleNiagaraCompile);
}

void FUeNodeNexusNiagaraBridgeModule::ShutdownModule()
{
    UnregisterNiagaraOperation(TEXT("niagara_system_create"));
    UnregisterNiagaraOperation(TEXT("niagara_system_duplicate"));
    UnregisterNiagaraOperation(TEXT("niagara_template_duplicate"));
    UnregisterNiagaraOperation(TEXT("niagara_system_summary_get"));
    UnregisterNiagaraOperation(TEXT("niagara_emitters_list"));
    UnregisterNiagaraOperation(TEXT("niagara_user_params_get"));
    UnregisterNiagaraOperation(TEXT("niagara_user_params_set"));
    UnregisterNiagaraOperation(TEXT("niagara_materials_get"));
    UnregisterNiagaraOperation(TEXT("niagara_materials_set"));
    UnregisterNiagaraOperation(TEXT("niagara_compile"));
}
