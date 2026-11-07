#include "NexusNiagaraInputMetadata.h"

#include "EdGraphSchema_Niagara.h"
#include "EdGraph/EdGraphPin.h"
#include "NiagaraGraph.h"
#include "NiagaraScriptVariable.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge::VfxTranscode
{
namespace
{
const UEdGraphPin* ParameterDefaultPin(const UEdGraphPin& Output)
{
    const auto* Node = Output.GetOwningNode();
    const auto* Property = FindFProperty<FMapProperty>(Node->GetClass(), TEXT("PinOutputToPinDefaultPersistentId"));
    const auto* Key = Property ? CastField<FStructProperty>(Property->KeyProp) : nullptr;
    const auto* Value = Property ? CastField<FStructProperty>(Property->ValueProp) : nullptr;
    if (!Key || !Value || Key->Struct != TBaseStructure<FGuid>::Get() || Value->Struct != Key->Struct)
    {
        return nullptr;
    }
    // UE's private, unexported GetDefaultPin uses this serialized GUID map.
    const auto* Pairs = Property->ContainerPtrToValuePtr<TMap<FGuid, FGuid>>(Node);
    const auto* Guid = Pairs->Find(Output.PersistentGuid);
    if (!Guid)
    {
        return nullptr;
    }
    const auto* Pin = Node->Pins.FindByPredicate([Guid](const UEdGraphPin* Candidate)
    {
        return Candidate && Candidate->Direction == EGPD_Input && Candidate->PersistentGuid == *Guid;
    });
    return Pin ? *Pin : nullptr;
}

FString DefaultPinText(const UEdGraphPin& Pin, bool bNeedsValue)
{
    if (Pin.LinkedTo.Num() == 1)
    {
        const auto* Source = Pin.LinkedTo[0];
        if (!Source || Source->GetOwningNode()->GetClass()->GetFName() != TEXT("NiagaraNodeParameterMapGet"))
        {
            return FString();
        }
        const auto* Next = ParameterDefaultPin(*Source);
        // A single linked default has one meaning. A chain selects a handle
        // against the caller's available parameters and is context dependent.
        return !Next || Next->LinkedTo.IsEmpty()
            ? FString::Printf(TEXT("@link(%s)"), *Source->PinName.ToString()) : FString();
    }
    if (!Pin.LinkedTo.IsEmpty())
    {
        return FString();
    }
    if (Pin.DefaultObject)
    {
        return Pin.DefaultObject->GetPathName();
    }
    const auto* Schema = GetDefault<UEdGraphSchema_Niagara>();
    const FNiagaraTypeDefinition Type = Schema->PinToTypeDefinition(&Pin);
    if (Type.IsDataInterface() || Type.IsUObject())
    {
        return FString();
    }
    const FNiagaraVariable Value = Schema->PinToNiagaraVariable(&Pin, bNeedsValue);
    FString Text;
    if (Value.IsDataAllocated())
    {
        // Repeated definitions can spell the same float with different precision.
        Schema->TryGetPinDefaultValueFromNiagaraVariable(Value, Text);
    }
    return Text;
}
}

FString ModuleInputPinValueText(const UEdGraphPin& Pin)
{
    // UE can serialize a false NiagaraBool pin as "()". Its typed value is
    // authoritative; comparing that spelling directly to "false" rejects a no-op.
    return Pin.LinkedTo.IsEmpty() ? DefaultPinText(Pin, false) : Pin.DefaultValue;
}

TArray<const UEdGraphPin*> FindModuleInputDefaultPins(const UNiagaraGraph& Graph, FName VariableName)
{
    TArray<const UEdGraphPin*> Result;
    for (const UEdGraphNode* Node : Graph.Nodes)
    {
        if (!Node || Node->GetClass()->GetFName() != TEXT("NiagaraNodeParameterMapGet"))
        {
            continue;
        }
        const auto* Output = Node->FindPin(VariableName, EGPD_Output);
        const auto* Pin = Output ? ParameterDefaultPin(*Output) : nullptr;
        if (Pin)
        {
            Result.Add(Pin);
        }
    }
    return Result;
}

bool IsAuthoredModuleInput(const FNiagaraVariable& Variable, const UNiagaraScriptVariable* Metadata)
{
    return Variable.GetName().ToString().StartsWith(TEXT("Module."))
        || (Metadata && Metadata->GetIsStaticSwitch());
}

bool CanUseRapidIterationValue(const FNiagaraTypeDefinition& Type, bool bStatic, const FString& DefaultText)
{
    // Niagara's stack and compiler use RI only for a non-static local default.
    // Linked/default-function inputs can retain stale storage that is never executed.
    return !bStatic && Type.IsValid() && !Type.IsDataInterface() && !Type.IsUObject() && !Type.IsEnum()
        && Type != FNiagaraTypeDefinition::GetBoolDef() && Type != FNiagaraTypeDefinition::GetParameterMapDef()
        && !DefaultText.IsEmpty() && !DefaultText.StartsWith(TEXT("@link("));
}

FString ModuleInputDefaultText(const FNiagaraVariable& Variable, const UNiagaraScriptVariable* Metadata,
    const UNiagaraGraph* Graph)
{
    if (!Metadata)
    {
        return FString();
    }
    if (!Metadata->GetIsStaticSwitch())
    {
        if (Metadata->DefaultMode == ENiagaraDefaultMode::Binding && Metadata->DefaultBinding.IsValid())
        {
            return FString::Printf(TEXT("@link(%s)"), *Metadata->DefaultBinding.GetName().ToString());
        }
        if (!Graph)
        {
            return FString();
        }
        // Metadata's buffer can be a zero placeholder. Read the defining pins;
        // retain a default only when every definition has the same typed meaning.
        TOptional<FString> Literal;
        for (const auto* Pin : FindModuleInputDefaultPins(*Graph, Variable.GetName()))
        {
            // A defining empty pin has Niagara's type default; an empty override
            // has no readable local value and uses the strict path above.
            const FString Text = DefaultPinText(*Pin, true);
            if (Text.IsEmpty() || (Literal.IsSet() && Literal.GetValue() != Text))
            {
                return FString();
            }
            Literal = Text;
        }
        return Literal.Get(FString());
    }
    FNiagaraVariable Default(Variable.GetType(), Variable.GetName());
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
    FString Text;
    GetDefault<UEdGraphSchema_Niagara>()->TryGetPinDefaultValueFromNiagaraVariable(Default, Text);
    return Text;
}
}
