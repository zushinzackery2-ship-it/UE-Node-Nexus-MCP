#include "NexusNiagaraInputBinding.h"

#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraNodeFunctionCall.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"

namespace UeNodeNexusBridge::VfxTranscode
{
UEdGraphPin* FindModuleOverridePin(UNiagaraNodeFunctionCall& Module, const FName PinName)
{
    const FName StaticName = FNiagaraParameterHandle(PinName).GetName();
    UEdGraphPin* MapInput = nullptr;
    for (UEdGraphPin* Pin : Module.Pins)
    {
        if (!Pin || Pin->Direction != EGPD_Input)
        {
            continue;
        }
        if (Pin->PinName == PinName || Pin->PinName == StaticName)
        {
            return Pin;
        }
        if (UEdGraphSchema_Niagara::PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
        {
            MapInput = Pin;
        }
    }
    if (!MapInput || MapInput->LinkedTo.Num() != 1)
    {
        return nullptr;
    }
    const UEdGraphNode* Override = MapInput->LinkedTo[0]->GetOwningNode();
    if (!Override || Override->GetClass()->GetName() != TEXT("NiagaraNodeParameterMapSet"))
    {
        return nullptr;
    }
    for (UEdGraphPin* Pin : Override->Pins)
    {
        if (Pin && Pin->Direction == EGPD_Input && Pin->PinName == PinName)
        {
            return Pin;
        }
    }
    return nullptr;
}
}
