#include "UeNodeNexusBridgeNiagaraModuleStack.h"
#include "NexusNiagaraInputBinding.h"

#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusNiagaraModuleMovement, Log, All);

namespace UeNodeNexusBridge::NiagaraModuleStack
{
bool MoveModuleInNativeStack(UNiagaraSystem& System, FNiagaraEmitterHandle& Emitter,
    UNiagaraNodeOutput& Output, UNiagaraNodeFunctionCall& Module, int32 TargetIndex,
    VfxTranscode::FInputBindingContext& Bindings, bool& bChanged, FString& Error)
{
    bChanged = false;
    Error.Reset();
    TArray<UNiagaraNodeFunctionCall*> Modules;
    GetOrderedModules(&Output, Modules);
    const int32 SourceIndex = Modules.IndexOfByKey(&Module);
    if (SourceIndex == INDEX_NONE || !Modules.IsValidIndex(TargetIndex))
    {
        Error = TEXT("module index is outside the native stack");
        UE_LOG(LogNexusNiagaraModuleMovement, Error, TEXT("event=move_rejected source=%d target=%d modules=%d: %s"),
            SourceIndex, TargetIndex, Modules.Num(), *Error);
        return false;
    }
    if (SourceIndex == TargetIndex)
    {
        UE_LOG(LogNexusNiagaraModuleMovement, Verbose, TEXT("event=move_noop index=%d node_guid=%s"),
            SourceIndex, *Module.NodeGuid.ToString());
        return true;
    }
    VfxTranscode::FInputBindingError Failure;
    UNiagaraStackEntry* Root = Bindings.ResolveRoot(System, Emitter, Failure);
    if (!Root)
    {
        Error = Failure.Message;
        UE_LOG(LogNexusNiagaraModuleMovement, Error, TEXT("event=move_rejected: %s"), *Error);
        return false;
    }
    TArray<UNiagaraStackModuleItem*> Items;
    Root->GetUnfilteredChildrenOfType(Items, true);
    UNiagaraStackModuleItem* SourceItem = nullptr;
    UNiagaraStackModuleItem* TargetItem = nullptr;
    for (UNiagaraStackModuleItem* Item : Items)
    {
        if (Item->GetOutputNode() != &Output)
        {
            continue;
        }
        if (&Item->GetModuleNode() == &Module)
        {
            SourceItem = Item;
        }
        if (&Item->GetModuleNode() == Modules[TargetIndex])
        {
            TargetItem = Item;
        }
    }
    if (!SourceItem || !TargetItem || SourceItem->GetModuleIndex() != SourceIndex
        || TargetItem->GetModuleIndex() != TargetIndex)
    {
        Error = TEXT("native stack module identity or index is unavailable");
        UE_LOG(LogNexusNiagaraModuleMovement, Error, TEXT("event=move_rejected source=%d target=%d: %s"),
            SourceIndex, TargetIndex, *Error);
        return false;
    }
    const EItemDropZone Zone = SourceIndex < TargetIndex ? EItemDropZone::BelowItem : EItemDropZone::AboveItem;
    TArray<UNiagaraStackEntry*> Dragged = { SourceItem };
    const auto Operation = MakeShared<FNiagaraStackEntryDragDropOp>(Dragged);
    // Overview is Niagara's data-operation admission flag; no widget or keyboard state is involved.
    const UNiagaraStackEntry::FDropRequest Request(Operation, Zone,
        UNiagaraStackEntry::EDragOptions::None, UNiagaraStackEntry::EDropOptions::Overview);
    const auto Admission = TargetItem->CanDrop(Request);
    if (!Admission.IsSet() || !Admission->DropZone.IsSet() || Admission->DropZone.GetValue() != Zone)
    {
        Error = Admission.IsSet() ? Admission->DropMessage.ToString() : TEXT("native stack refused module movement");
        UE_LOG(LogNexusNiagaraModuleMovement, Error, TEXT("event=move_rejected source=%d target=%d: %s"),
            SourceIndex, TargetIndex, *Error);
        return false;
    }
    const FGuid Guid = Module.NodeGuid;
    const auto Result = TargetItem->Drop(Request);
    if (!Result.IsSet() || !Result->DropZone.IsSet() || Result->DropZone.GetValue() != Zone)
    {
        Error = Result.IsSet() ? Result->DropMessage.ToString() : TEXT("native stack did not complete module movement");
        UE_LOG(LogNexusNiagaraModuleMovement, Error, TEXT("event=move_failed source=%d target=%d: %s"),
            SourceIndex, TargetIndex, *Error);
        return false;
    }
    TArray<UNiagaraNodeFunctionCall*> After;
    GetOrderedModules(&Output, After);
    if (After.Num() != Modules.Num() || !After.IsValidIndex(TargetIndex)
        || After[TargetIndex] != &Module || Module.NodeGuid != Guid)
    {
        Error = TEXT("native module movement readback mismatch");
        UE_LOG(LogNexusNiagaraModuleMovement, Error, TEXT("event=move_failed source=%d target=%d node_guid=%s: %s"),
            SourceIndex, TargetIndex, *Guid.ToString(), *Error);
        return false;
    }
    bChanged = true;
    MarkSystemEdited(&System);
    UE_LOG(LogNexusNiagaraModuleMovement, Display, TEXT("event=module_moved source=%d target=%d node_guid=%s modules=%d"),
        SourceIndex, TargetIndex, *Guid.ToString(), After.Num());
    return true;
}
}

