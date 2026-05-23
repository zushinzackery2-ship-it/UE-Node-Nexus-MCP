#include "UeNodeNexusNiagaraOps.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"
#include "UeNodeNexusBridgeNiagaraModuleStack.h"

#include "NiagaraEmitterHandle.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleNiagaraModuleAdd(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (!System)
    {
        return EarlyResponse;
    }
    FNiagaraEmitterHandle* Handle = NiagaraModuleStack::ResolveEmitter(System, Payload, EarlyResponse, Operation, RequestId);
    if (!Handle)
    {
        return EarlyResponse;
    }

    FString ModuleScriptPath;
    FString UsageText = TEXT("ParticleUpdateScript");
    FString SuggestedName;
    int32 TargetIndex = INDEX_NONE;
    bool bDryRun = true;
    bool bSave = false;
    if (!Payload->TryGetStringField(TEXT("module_script_path"), ModuleScriptPath) || ModuleScriptPath.IsEmpty())
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("module_script_path is required")));
        return Response;
    }
    Payload->TryGetStringField(TEXT("usage"), UsageText);
    Payload->TryGetStringField(TEXT("suggested_name"), SuggestedName);
    NiagaraModuleStack::ReadIndex(Payload, TEXT("target_index"), TargetIndex);
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);

    UNiagaraNodeOutput* Output = NiagaraModuleStack::ResolveOutputNode(Handle, UsageText);
    UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, *ModuleScriptPath);
    if (!Output || !Script || Script->Usage != ENiagaraScriptUsage::Module)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_module_target"), TEXT("usage must resolve to an emitter output and module_script_path must load a Niagara Module script")));
        return Response;
    }

    TArray<UNiagaraNodeFunctionCall*> Before;
    NiagaraModuleStack::GetOrderedModules(Output, Before);
    UNiagaraNodeFunctionCall* Added = nullptr;
    if (!bDryRun)
    {
        Added = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Script, *Output, TargetIndex, SuggestedName);
        if (Added)
        {
            NiagaraModuleStack::MarkSystemEdited(System);
        }
    }
    TArray<UNiagaraNodeFunctionCall*> After;
    NiagaraModuleStack::GetOrderedModules(Output, After);
    const bool bChanged = !bDryRun && After.Num() > Before.Num();
    const bool bSaved = bSave && bChanged && SaveAssetPackage(System);

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetSummaryData(System);
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetBoolField(TEXT("changed"), bChanged);
    Data->SetBoolField(TEXT("saved"), bSaved);
    Data->SetStringField(TEXT("usage"), NiagaraModuleStack::UsageToString(Output->GetUsage()));
    Data->SetNumberField(TEXT("module_count_before"), Before.Num());
    Data->SetNumberField(TEXT("module_count_after"), After.Num());
    Data->SetStringField(TEXT("node_guid"), Added ? Added->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) : FString());
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleNiagaraModuleRemove(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (!System)
    {
        return EarlyResponse;
    }
    FNiagaraEmitterHandle* Handle = NiagaraModuleStack::ResolveEmitter(System, Payload, EarlyResponse, Operation, RequestId);
    if (!Handle)
    {
        return EarlyResponse;
    }
    UNiagaraNodeFunctionCall* Module = NiagaraModuleStack::ResolveModule(Handle, Payload, EarlyResponse, Operation, RequestId);
    if (!Module)
    {
        return EarlyResponse;
    }

    bool bDryRun = true;
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);

    const FString FunctionName = Module->GetFunctionName();
    const bool bRemoved = !bDryRun && NiagaraModuleStack::RemoveModulePreservingStack(Module);
    if (bRemoved)
    {
        NiagaraModuleStack::MarkSystemEdited(System);
    }
    const bool bSaved = bSave && bRemoved && SaveAssetPackage(System);

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetSummaryData(System);
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetBoolField(TEXT("changed"), bRemoved);
    Data->SetBoolField(TEXT("saved"), bSaved);
    Data->SetStringField(TEXT("removed_function_name"), FunctionName);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleNiagaraModuleSetEnabled(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (!System)
    {
        return EarlyResponse;
    }
    FNiagaraEmitterHandle* Handle = NiagaraModuleStack::ResolveEmitter(System, Payload, EarlyResponse, Operation, RequestId);
    if (!Handle)
    {
        return EarlyResponse;
    }
    UNiagaraNodeFunctionCall* Module = NiagaraModuleStack::ResolveModule(Handle, Payload, EarlyResponse, Operation, RequestId);
    if (!Module)
    {
        return EarlyResponse;
    }

    bool bEnabled = true;
    bool bDryRun = true;
    bool bSave = false;
    if (!Payload->TryGetBoolField(TEXT("enabled"), bEnabled))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("enabled is required")));
        return Response;
    }
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);

    const bool bBefore = Module->IsNodeEnabled();
    if (!bDryRun && bBefore != bEnabled)
    {
        FNiagaraStackGraphUtilities::SetModuleIsEnabled(*Module, bEnabled);
        NiagaraModuleStack::MarkSystemEdited(System);
    }
    const bool bChanged = !bDryRun && bBefore != bEnabled;
    const bool bSaved = bSave && bChanged && SaveAssetPackage(System);

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetSummaryData(System);
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetBoolField(TEXT("changed"), bChanged);
    Data->SetBoolField(TEXT("enabled_before"), bBefore);
    Data->SetBoolField(TEXT("enabled_after"), bDryRun ? bBefore : Module->IsNodeEnabled());
    Data->SetBoolField(TEXT("saved"), bSaved);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
