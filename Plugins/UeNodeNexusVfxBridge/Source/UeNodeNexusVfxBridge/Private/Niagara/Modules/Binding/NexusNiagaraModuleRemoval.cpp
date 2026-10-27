#include "UeNodeNexusBridgeNiagaraModuleStack.h"

#include "NexusNiagaraInputBinding.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusNiagaraModuleRemoval, Log, All);

namespace UeNodeNexusBridge::NiagaraModuleStack
{
bool RemoveModuleFromNativeStack(UNiagaraSystem& System, FNiagaraEmitterHandle& Emitter,
    UNiagaraNodeFunctionCall& Module, FString& Error)
{
    VfxTranscode::FInputBindingContext Bindings;
    VfxTranscode::FInputBindingError Failure;
    UNiagaraStackEntry* Root = Bindings.ResolveRoot(System, Emitter, Failure);
    if (!Root)
    {
        Error = Failure.Message;
        UE_LOG(LogNexusNiagaraModuleRemoval, Error, TEXT("%s"), *Error);
        return false;
    }
    TArray<UNiagaraStackModuleItem*> Items;
    Root->GetUnfilteredChildrenOfType(Items, true);
    UNiagaraStackModuleItem* const* Found = Items.FindByPredicate([&Module](UNiagaraStackModuleItem* Item)
    {
        return &Item->GetModuleNode() == &Module;
    });
    FText Reason;
    if (!Found || !(*Found)->TestCanDeleteWithMessage(Reason))
    {
        Error = Found ? Reason.ToString() : TEXT("native stack module is unavailable");
        UE_LOG(LogNexusNiagaraModuleRemoval, Error, TEXT("module_guid=%s: %s"), *Module.NodeGuid.ToString(), *Error);
        return false;
    }
    UNiagaraGraph* Graph = Module.GetNiagaraGraph();
    const FString Guid = Module.NodeGuid.ToString();
    (*Found)->Delete();
    if (Graph->Nodes.Contains(&Module))
    {
        Error = TEXT("native module deletion readback mismatch");
        UE_LOG(LogNexusNiagaraModuleRemoval, Error, TEXT("module_guid=%s: %s"), *Guid, *Error);
        return false;
    }
    UE_LOG(LogNexusNiagaraModuleRemoval, Display, TEXT("removed module_guid=%s with native input cleanup"), *Guid);
    return true;
}
}
