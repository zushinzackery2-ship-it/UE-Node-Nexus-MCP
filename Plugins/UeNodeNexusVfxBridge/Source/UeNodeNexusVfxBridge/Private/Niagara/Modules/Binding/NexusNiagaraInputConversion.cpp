#include "NexusNiagaraInputBinding.h"
#include "NexusNiagaraInputMetadata.h"

#include "EdGraph/EdGraphPin.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScript.h"
#include "NiagaraScriptVariable.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"

namespace UeNodeNexusBridge::VfxTranscode
{
bool ResolveNativeConversion(UNiagaraStackFunctionInput& Input, const FString& Parameter,
    FNiagaraVariable& Source, UNiagaraScript*& Script, FInputBindingError& Error)
{
    TArray<FNiagaraParameterHandle> Handles;
    TMap<FNiagaraVariable, UNiagaraScript*> Conversions;
    Input.GetAvailableParameterHandles(Handles, Conversions, true);
    int32 Matches = 0;
    for (const auto& Pair : Conversions)
    {
        if (Pair.Key.GetName() == FName(*Parameter))
        {
            Source = Pair.Key;
            Script = Pair.Value;
            ++Matches;
        }
    }
    if (Matches != 1 || !Script || !Script->GetPathName().StartsWith(TEXT("/Niagara/")))
    {
        Error.Code = TEXT("native_conversion_unavailable");
        Error.Message = FString::Printf(TEXT("parameter=%s target_type=%s native_conversion_matches=%d"),
            *Parameter, *Input.GetInputType().GetName(), Matches);
        return false;
    }
    const auto Candidates = Input.GetPossibleConversionScripts(Source.GetType());
    if (Candidates.Num() != 1 || Candidates[0] != Script)
    {
        Error.Code = TEXT("native_conversion_ambiguous");
        Error.Message = FString::Printf(TEXT("source_type=%s target_type=%s conversion_scripts=%d"),
            *Source.GetType().GetName(), *Input.GetInputType().GetName(), Candidates.Num());
        return false;
    }
    return true;
}

bool MatchesNativeConversion(UNiagaraStackFunctionInput& Input, const FNiagaraVariable& Source, UNiagaraScript* Script)
{
    auto* Conversion = Input.GetDynamicInputNode();
    if (Input.GetValueMode() != UNiagaraStackFunctionInput::EValueMode::Dynamic
        || !Conversion || Conversion->FunctionScript != Script)
    {
        return false;
    }
    int32 Matches = 0;
    bool bLinked = false;
    for (UNiagaraStackFunctionInput* Child : Input.GetChildInputs())
    {
        if (Child->GetInputType() != Source.GetType())
        {
            continue;
        }
        ++Matches;
        bLinked = Child->GetValueMode() == UNiagaraStackFunctionInput::EValueMode::Linked
            && Child->GetLinkedValueHandle().GetParameterHandleString() == Source.GetName();
    }
    return Matches == 1 && bLinked;
}

FString NativeConversionParameter(const UEdGraphPin& Override, const FNiagaraTypeDefinition& TargetType)
{
    if (Override.LinkedTo.Num() != 1 || !Override.LinkedTo[0])
    {
        return FString();
    }
    auto* Conversion = Cast<UNiagaraNodeFunctionCall>(Override.LinkedTo[0]->GetOwningNode());
    if (!Conversion || !Conversion->FunctionScript || !Conversion->FunctionScript->GetPathName().StartsWith(TEXT("/Niagara/")))
    {
        return FString();
    }
    auto* Graph = Conversion->GetCalledGraph();
    if (!Graph)
    {
        return FString();
    }
    FString Parameter;
    int32 Matches = 0;
    for (const auto& Pair : Graph->GetAllMetaData())
    {
        if (!IsAuthoredModuleInput(Pair.Key, Pair.Value) || (Pair.Value && Pair.Value->GetIsStaticSwitch()))
        {
            continue;
        }
        const auto Candidates = UNiagaraStackFunctionInput::GetPossibleConversionScripts(Pair.Key.GetType(), TargetType);
        if (Candidates.Num() != 1 || Candidates[0] != Conversion->FunctionScript)
        {
            continue;
        }
        ++Matches;
        const auto Alias = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
            FNiagaraParameterHandle(Pair.Key.GetName()), Conversion);
        auto* Source = FindModuleOverridePin(*Conversion, Alias.GetParameterHandleString());
        if (Source && Source->LinkedTo.Num() == 1 && Source->LinkedTo[0]
            && Source->LinkedTo[0]->GetOwningNode()->GetClass()->GetName() == TEXT("NiagaraNodeParameterMapGet"))
        {
            Parameter = Source->LinkedTo[0]->PinName.ToString();
        }
        else if (!Source && Pair.Value && Pair.Value->DefaultMode == ENiagaraDefaultMode::Binding)
        {
            Parameter = Pair.Value->DefaultBinding.GetName().ToString();
        }
    }
    return Matches == 1 ? Parameter : FString();
}
}
