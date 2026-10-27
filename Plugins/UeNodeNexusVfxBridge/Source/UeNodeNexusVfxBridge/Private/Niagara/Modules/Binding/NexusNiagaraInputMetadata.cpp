#include "NexusNiagaraInputMetadata.h"

#include "EdGraphSchema_Niagara.h"
#include "NiagaraScriptVariable.h"

namespace UeNodeNexusBridge::VfxTranscode
{
bool IsAuthoredModuleInput(const FNiagaraVariable& Variable, const UNiagaraScriptVariable* Metadata)
{
    return Variable.GetName().ToString().StartsWith(TEXT("Module."))
        || (Metadata && Metadata->GetIsStaticSwitch());
}

FString ModuleInputDefaultText(const FNiagaraVariable& Variable, const UNiagaraScriptVariable* Metadata)
{
    if (!Metadata)
    {
        return FString();
    }
    FNiagaraVariable Default(Variable.GetType(), Variable.GetName());
    if (Metadata->GetIsStaticSwitch())
    {
        const int32 Value = Metadata->GetStaticSwitchDefaultValue();
        if (Variable.GetType() == FNiagaraTypeDefinition::GetBoolDef())
        {
            const FNiagaraBool Boolean(Value != 0);
            Default.SetData(reinterpret_cast<const uint8*>(&Boolean));
        }
        else
        {
            Default.SetData(reinterpret_cast<const uint8*>(&Value));
        }
    }
    else if (const uint8* Data = Metadata->GetDefaultValueData())
    {
        Default.SetData(Data);
    }
    else
    {
        return FString();
    }
    FString Text;
    GetDefault<UEdGraphSchema_Niagara>()->TryGetPinDefaultValueFromNiagaraVariable(Default, Text);
    return Text;
}
}
