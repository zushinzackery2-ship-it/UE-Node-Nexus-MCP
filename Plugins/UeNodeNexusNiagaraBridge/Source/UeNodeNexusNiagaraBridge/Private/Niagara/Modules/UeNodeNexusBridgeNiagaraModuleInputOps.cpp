#include "UeNodeNexusNiagaraOps.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"
#include "UeNodeNexusBridgeNiagaraModuleStack.h"

#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNode.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSystem.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

namespace UeNodeNexusBridge
{
namespace
{
struct FModuleInputRecord
{
    FNiagaraVariable Variable;
    FGuid VariableGuid;
    FString Name;
    FString ShortName;
    FString TypeName;
    FString DefaultValueText;
    FString OverrideValueText;
    bool bHasOverride = false;
    bool bOverrideLinked = false;
};

FString StripModuleNamespace(const FName Name)
{
    FString Text = Name.ToString();
    Text.RemoveFromStart(TEXT("Module."));
    return Text;
}

bool InputNameMatches(const FModuleInputRecord& Input, const FString& Name)
{
    return Input.Name.Equals(Name, ESearchCase::IgnoreCase)
        || Input.ShortName.Equals(Name, ESearchCase::IgnoreCase);
}

UEdGraphPin* FindOverridePinByName(UNiagaraNodeFunctionCall* Module, const FName PinName)
{
    UEdGraph* Graph = Module ? Module->GetGraph() : nullptr;
    if (!Graph)
    {
        return nullptr;
    }

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node)
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

void GatherModuleInputs(UNiagaraNodeFunctionCall* Module, TArray<FModuleInputRecord>& OutInputs)
{
    UNiagaraGraph* CalledGraph = Module ? Module->GetCalledGraph() : nullptr;
    if (!CalledGraph)
    {
        return;
    }

    const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
    for (const TPair<FNiagaraVariable, TObjectPtr<UNiagaraScriptVariable>>& Pair : CalledGraph->GetAllMetaData())
    {
        const FNiagaraVariable& Variable = Pair.Key;
        const FString Name = Variable.GetName().ToString();
        if (!Name.StartsWith(TEXT("Module.")))
        {
            continue;
        }

        FModuleInputRecord Record;
        Record.Variable = Variable;
        Record.Name = Name;
        Record.ShortName = StripModuleNamespace(Variable.GetName());
        Record.TypeName = Variable.GetType().GetName();
        if (Pair.Value)
        {
            Record.VariableGuid = Pair.Value->Metadata.GetVariableGuid();
            if (const uint8* DefaultData = Pair.Value->GetDefaultValueData())
            {
                FNiagaraVariable DefaultVariable(Variable.GetType(), Variable.GetName());
                DefaultVariable.SetData(DefaultData);
                Schema->TryGetPinDefaultValueFromNiagaraVariable(DefaultVariable, Record.DefaultValueText);
            }
        }

        const FNiagaraParameterHandle AliasedHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(Variable.GetName()), Module);
        if (UEdGraphPin* OverridePin = FindOverridePinByName(Module, AliasedHandle.GetParameterHandleString()))
        {
            Record.bHasOverride = true;
            Record.bOverrideLinked = OverridePin->LinkedTo.Num() > 0;
            Record.OverrideValueText = OverridePin->DefaultValue;
        }
        OutInputs.Add(MoveTemp(Record));
    }
}

TSharedPtr<FJsonObject> InputToJson(const FModuleInputRecord& Input)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("name"), Input.Name);
    Json->SetStringField(TEXT("short_name"), Input.ShortName);
    Json->SetStringField(TEXT("type"), Input.TypeName);
    Json->SetStringField(TEXT("variable_guid"), Input.VariableGuid.ToString(EGuidFormats::DigitsWithHyphens));
    Json->SetStringField(TEXT("default_value_text"), Input.DefaultValueText);
    Json->SetBoolField(TEXT("has_override"), Input.bHasOverride);
    Json->SetBoolField(TEXT("override_linked"), Input.bOverrideLinked);
    Json->SetStringField(TEXT("override_value_text"), Input.OverrideValueText);
    return Json;
}

TSharedPtr<FJsonObject> MakeInvalidInputResponse(const FString& Operation, const FString& RequestId, const FString& Message)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), Message));
    return Response;
}

bool ResolveModuleForInputOp(
    UNiagaraSystem*& OutSystem,
    FNiagaraEmitterHandle*& OutEmitter,
    UNiagaraNodeFunctionCall*& OutModule,
    const FString& Operation,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FJsonObject>& OutResponse)
{
    OutSystem = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, OutResponse);
    if (!OutSystem)
    {
        return false;
    }
    OutEmitter = NiagaraModuleStack::ResolveEmitter(OutSystem, Payload, OutResponse, Operation, RequestId);
    if (!OutEmitter)
    {
        return false;
    }
    OutModule = NiagaraModuleStack::ResolveModule(OutEmitter, Payload, OutResponse, Operation, RequestId);
    return OutModule != nullptr;
}
}

TSharedPtr<FJsonObject> HandleNiagaraModuleInputsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = nullptr;
    FNiagaraEmitterHandle* Emitter = nullptr;
    UNiagaraNodeFunctionCall* Module = nullptr;
    if (!ResolveModuleForInputOp(System, Emitter, Module, Operation, RequestId, Payload, EarlyResponse))
    {
        return EarlyResponse;
    }

    TArray<FModuleInputRecord> Inputs;
    GatherModuleInputs(Module, Inputs);

    TArray<TSharedPtr<FJsonValue>> Items;
    for (const FModuleInputRecord& Input : Inputs)
    {
        Items.Add(MakeShared<FJsonValueObject>(InputToJson(Input)));
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetSummaryData(System);
    Data->SetStringField(TEXT("format"), TEXT("niagara_module_inputs_full"));
    Data->SetStringField(TEXT("emitter_name"), Emitter ? Emitter->GetName().ToString() : FString());
    Data->SetStringField(TEXT("function_name"), Module ? Module->GetFunctionName() : FString());
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleNiagaraModuleInputsSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = nullptr;
    FNiagaraEmitterHandle* Emitter = nullptr;
    UNiagaraNodeFunctionCall* Module = nullptr;
    if (!ResolveModuleForInputOp(System, Emitter, Module, Operation, RequestId, Payload, EarlyResponse))
    {
        return EarlyResponse;
    }

    const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
    if (!Payload->TryGetArrayField(TEXT("params"), Params))
    {
        return MakeInvalidInputResponse(Operation, RequestId, TEXT("params is required"));
    }

    bool bDryRun = true;
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);

    TArray<FModuleInputRecord> Inputs;
    GatherModuleInputs(Module, Inputs);
    TArray<TSharedPtr<FJsonValue>> Items;
    bool bChanged = false;

    for (const TSharedPtr<FJsonValue>& Value : *Params)
    {
        const TSharedPtr<FJsonObject>* ParamObject = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(ParamObject) || !ParamObject || !ParamObject->IsValid())
        {
            return MakeInvalidInputResponse(Operation, RequestId, TEXT("each params item must be an object"));
        }

        FString Name;
        FString ValueText;
        if (!(*ParamObject)->TryGetStringField(TEXT("name"), Name) || !(*ParamObject)->TryGetStringField(TEXT("value_text"), ValueText))
        {
            return MakeInvalidInputResponse(Operation, RequestId, TEXT("each params item requires name and value_text"));
        }

        FModuleInputRecord* Match = Inputs.FindByPredicate([&Name](const FModuleInputRecord& Input)
        {
            return InputNameMatches(Input, Name);
        });
        if (!Match)
        {
            return MakeInvalidInputResponse(Operation, RequestId, FString::Printf(TEXT("module input not found: %s"), *Name));
        }

        const FNiagaraParameterHandle AliasedHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(Match->Variable.GetName()), Module);
        UEdGraphPin* ExistingPin = FindOverridePinByName(Module, AliasedHandle.GetParameterHandleString());
        const FString Before = ExistingPin ? ExistingPin->DefaultValue : Match->DefaultValueText;
        if (!bDryRun && Before != ValueText)
        {
            UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Module, AliasedHandle, Match->Variable.GetType(), Match->VariableGuid, FGuid());
            OverridePin.Modify();
            OverridePin.DefaultValue = ValueText;
            if (UNiagaraNode* OwningNode = Cast<UNiagaraNode>(OverridePin.GetOwningNode()))
            {
                OwningNode->MarkNodeRequiresSynchronization(TEXT("MCP module input value changed"), true);
            }
            NiagaraModuleStack::MarkSystemEdited(System);
            bChanged = true;
        }

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Match->Name);
        Item->SetStringField(TEXT("short_name"), Match->ShortName);
        Item->SetStringField(TEXT("before_value_text"), Before);
        Item->SetStringField(TEXT("after_value_text"), bDryRun ? Before : ValueText);
        Item->SetBoolField(TEXT("changed"), Before != ValueText);
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }

    const bool bSaved = bSave && bChanged && SaveAssetPackage(System);
    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetSummaryData(System);
    Data->SetStringField(TEXT("format"), TEXT("niagara_module_inputs_set_summary"));
    Data->SetStringField(TEXT("emitter_name"), Emitter ? Emitter->GetName().ToString() : FString());
    Data->SetStringField(TEXT("function_name"), Module ? Module->GetFunctionName() : FString());
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetBoolField(TEXT("changed"), bChanged);
    Data->SetBoolField(TEXT("saved"), bSaved);
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
