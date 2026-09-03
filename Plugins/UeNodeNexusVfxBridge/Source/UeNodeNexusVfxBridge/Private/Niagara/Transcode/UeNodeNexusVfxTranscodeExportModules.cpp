#include "UeNodeNexusVfxTranscode.h"

#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraParameterStore.h"
#include "NiagaraScript.h"
#include "NiagaraScriptVariable.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"

namespace UeNodeNexusBridge::VfxTranscode
{
static UEdGraphPin* FindOverridePin(UNiagaraNodeFunctionCall* Module, const FName PinName)
{
    UEdGraph* Graph = Module->GetGraph();
    if (Graph == nullptr)
    {
        return nullptr;
    }
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr)
        {
            continue;
        }
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Input && Pin->PinName.IsEqual(PinName, ENameCase::IgnoreCase))
            {
                return Pin;
            }
        }
    }
    return nullptr;
}

bool ResolveRapidIterationInput(FNiagaraEmitterHandle& Handle, ENiagaraScriptUsage Usage, const FGuid& UsageId, UNiagaraNodeFunctionCall* Module, const FString& InputName, FNiagaraVariable& OutVariable, UNiagaraScript*& OutScript)
{
    OutScript = nullptr;
    UNiagaraGraph* CalledGraph = Module ? Module->GetCalledGraph() : nullptr;
    FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
    if (CalledGraph == nullptr || Data == nullptr)
    {
        return false;
    }
    for (const TPair<FNiagaraVariable, TObjectPtr<UNiagaraScriptVariable>>& Pair : CalledGraph->GetAllMetaData())
    {
        FString Name = Pair.Key.GetName().ToString();
        if (!Name.RemoveFromStart(TEXT("Module.")) || !Name.Equals(InputName, ESearchCase::IgnoreCase))
        {
            continue;
        }
        const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(Pair.Key.GetName()), Module);
        // Mirrors FNiagaraStackGraphUtilities::CreateRapidIterationParameter (not exported):
        // emitter/particle scripts store "Constants.<UniqueEmitterName>.<Function>.<Input>".
        const FString RapidName = FString::Printf(TEXT("Constants.%s.%s"), *Handle.GetUniqueInstanceName(), *Aliased.GetParameterHandleString().ToString());
        OutVariable = FNiagaraVariable(Pair.Key.GetType(), FName(*RapidName));
        OutScript = Data->GetScript(Usage, UsageId);
        return OutScript != nullptr;
    }
    return false;
}

// Set Variables nodes keep their assigned values in AssignmentTargets/AssignmentDefaultValues
// (read via reflection; the node class header is editor-private in some engine versions).
static TMap<FString, FString> AssignmentDefaults(UNiagaraNodeFunctionCall* Module)
{
    TMap<FString, FString> Defaults;
    const FArrayProperty* Targets = FindFProperty<FArrayProperty>(Module->GetClass(), TEXT("AssignmentTargets"));
    const FArrayProperty* Values = FindFProperty<FArrayProperty>(Module->GetClass(), TEXT("AssignmentDefaultValues"));
    if (Targets == nullptr || Values == nullptr || !Targets->Inner->IsA<FStructProperty>() || !Values->Inner->IsA<FStrProperty>())
    {
        return Defaults;
    }
    FScriptArrayHelper TargetHelper(Targets, Targets->ContainerPtrToValuePtr<void>(Module));
    FScriptArrayHelper ValueHelper(Values, Values->ContainerPtrToValuePtr<void>(Module));
    for (int32 Index = 0; Index < TargetHelper.Num() && Index < ValueHelper.Num(); ++Index)
    {
        const FNiagaraVariableBase* Target = reinterpret_cast<const FNiagaraVariableBase*>(TargetHelper.GetRawPtr(Index));
        Defaults.Add(Target->GetName().ToString(), *reinterpret_cast<const FString*>(ValueHelper.GetRawPtr(Index)));
    }
    return Defaults;
}

static TArray<TSharedPtr<FJsonValue>> ModuleInputs(FNiagaraEmitterHandle* Handle, UNiagaraNodeOutput* Output, UNiagaraNodeFunctionCall* Module)
{
    TArray<TSharedPtr<FJsonValue>> Inputs;
    UNiagaraGraph* CalledGraph = Module->GetCalledGraph();
    if (CalledGraph == nullptr)
    {
        return Inputs;
    }
    const bool bAssignment = Module->GetClass()->GetName() == TEXT("NiagaraNodeAssignment");
    const TMap<FString, FString> Assigned = bAssignment ? AssignmentDefaults(Module) : TMap<FString, FString>();
    const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
    for (const TPair<FNiagaraVariable, TObjectPtr<UNiagaraScriptVariable>>& Pair : CalledGraph->GetAllMetaData())
    {
        const FNiagaraVariable& Variable = Pair.Key;
        FString Name = Variable.GetName().ToString();
        if (!Name.RemoveFromStart(TEXT("Module.")))
        {
            continue;
        }
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("name"), Name);
        Json->SetStringField(TEXT("type"), FriendlyTypeName(Variable.GetType()));
        FString DefaultText;
        if (Pair.Value)
        {
            if (const uint8* DefaultData = Pair.Value->GetDefaultValueData())
            {
                FNiagaraVariable DefaultVariable(Variable.GetType(), Variable.GetName());
                DefaultVariable.SetData(DefaultData);
                Schema->TryGetPinDefaultValueFromNiagaraVariable(DefaultVariable, DefaultText);
            }
        }
        Json->SetStringField(TEXT("default"), DefaultText);
        const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(Variable.GetName()), Module);
        UEdGraphPin* Override = FindOverridePin(Module, Aliased.GetParameterHandleString());
        bool bHasOverride = Override != nullptr;
        FString ValueText = Override ? Override->DefaultValue : DefaultText;
        FNiagaraVariable RapidVariable;
        UNiagaraScript* Script = nullptr;
        if (Override == nullptr && Handle != nullptr && Output != nullptr
            && ResolveRapidIterationInput(*Handle, Output->GetUsage(), Output->GetUsageId(), Module, Name, RapidVariable, Script)
            && Script->RapidIterationParameters.IndexOf(RapidVariable) != INDEX_NONE)
        {
            const FString RapidText = ParameterValueText(Script->RapidIterationParameters, RapidVariable);
            if (!RapidText.IsEmpty())
            {
                bHasOverride = true;
                ValueText = RapidText;
            }
        }
        if (bAssignment)
        {
            // every assigned variable is user content: always print it
            if (const FString* Assignment = Assigned.Find(Name); Assignment && !bHasOverride)
            {
                ValueText = *Assignment;
            }
            bHasOverride = true;
        }
        Json->SetBoolField(TEXT("has_override"), bHasOverride);
        Json->SetStringField(TEXT("value"), ValueText);
        bool bDynamic = false;
        FString Linked;
        if (Override != nullptr && Override->LinkedTo.Num() > 0 && Override->LinkedTo[0] != nullptr)
        {
            UEdGraphNode* Source = Override->LinkedTo[0]->GetOwningNode();
            // UNiagaraNodeParameterMapGet lives in NiagaraEditor/Private; match by class name.
            if (Source != nullptr && Source->GetClass()->GetName() == TEXT("NiagaraNodeParameterMapGet"))
            {
                Linked = Override->LinkedTo[0]->PinName.ToString();
            }
            else
            {
                bDynamic = true;
            }
        }
        if (Linked.IsEmpty())
        {
            Json->SetField(TEXT("linked"), MakeShared<FJsonValueNull>());
        }
        else
        {
            Json->SetStringField(TEXT("linked"), Linked);
        }
        Json->SetBoolField(TEXT("dynamic"), bDynamic);
        Inputs.Add(MakeShared<FJsonValueObject>(Json));
    }
    return Inputs;
}

TSharedPtr<FJsonObject> ModuleJson(FNiagaraEmitterHandle* Handle, UNiagaraNodeOutput* Output, UNiagaraNodeFunctionCall* Module)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("guid"), Module->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    // "Set Variables" modules own a generated transient script (SetVariables_<hash>); the
    // stable, human name is the node kind. They cannot be created from text.
    const bool bAssignment = Module->GetClass()->GetName() == TEXT("NiagaraNodeAssignment");
    Json->SetStringField(TEXT("script"), Module->FunctionScript && !bAssignment ? Module->FunctionScript->GetPathName() : FString());
    Json->SetStringField(TEXT("script_short"), bAssignment ? TEXT("SetVariables") : (Module->FunctionScript ? Module->FunctionScript->GetName() : Module->GetFunctionName()));
    Json->SetStringField(TEXT("function_name"), Module->GetFunctionName());
    Json->SetBoolField(TEXT("enabled"), Module->IsNodeEnabled());
    Json->SetBoolField(TEXT("assignment"), bAssignment);
    Json->SetArrayField(TEXT("inputs"), ModuleInputs(Handle, Output, Module));
    return Json;
}
}
