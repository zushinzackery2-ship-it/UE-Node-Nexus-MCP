#include "NexusNiagaraInputBinding.h"

#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScriptVariable.h"
#include "Niagara/Transcode/Values/NexusNiagaraValue.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"

namespace UeNodeNexusBridge::VfxTranscode
{
bool ApplyInlineInput(UNiagaraSystem& System, FNiagaraEmitterHandle& Emitter,
    UNiagaraNodeFunctionCall& Module, const FNiagaraVariable& Variable, const FString& Value,
    bool bReset, FInputBindingContext& Context, bool& bChanged, FInputBindingError& Error)
{
    const UNiagaraScriptVariable* Metadata = Module.GetCalledGraph()->GetScriptVariable(Variable.GetName());
    FParsedValue Parsed;
    if (!bReset && !ParseParameterValue(Variable.GetType(), Value, Parsed, Error.Message))
    {
        Error.Code = TEXT("inline_input_value_invalid");
        return false;
    }
    const bool bEnabled = bReset ? Metadata->GetStaticSwitchDefaultValue() != 0 : Parsed.Boolean;
    UNiagaraStackEntry* Root = Context.ResolveRoot(System, Emitter, Error);
    if (!Root)
    {
        return false;
    }
    TArray<UNiagaraStackFunctionInput*> Inputs;
    TArray<UNiagaraStackFunctionInput*> Owners;
    Root->GetUnfilteredChildrenOfType(Inputs, true);
    for (UNiagaraStackFunctionInput* Input : Inputs)
    {
        const auto Condition = Input->GetEditConditionVariable();
        if (&Input->GetInputFunctionCallNode() == &Module && Condition.IsSet()
            && Condition.GetValue() == Variable && Input->GetShowEditConditionInline())
        {
            Owners.Add(Input);
        }
    }
    if (Owners.IsEmpty())
    {
        Error.Code = TEXT("inline_input_owner_unresolved");
        Error.Message = TEXT("inline static input has no native edit-condition owner");
        return false;
    }
    bChanged = Owners[0]->GetEditConditionEnabled() != bEnabled;
    if (bChanged)
    {
        Owners[0]->SetEditConditionEnabled(bEnabled);
    }
    Root->RefreshChildren();
    const UEdGraphPin* Pin = FindModuleOverridePin(Module, Variable.GetName());
    if (!Pin)
    {
        Error.Code = TEXT("inline_input_pin_missing");
        Error.Message = TEXT("native edit-condition write did not preserve the function pin");
        return false;
    }
    const FNiagaraVariable Actual = UEdGraphSchema_Niagara::PinToNiagaraVariable(Pin, true);
    const auto* Boolean = reinterpret_cast<const FNiagaraBool*>(Actual.GetData());
    if (!Boolean || Boolean->GetValue() != bEnabled)
    {
        Error.Code = TEXT("inline_input_readback_mismatch");
        Error.Message = TEXT("native function pin differs from requested edit condition");
        return false;
    }
    return true;
}
}
