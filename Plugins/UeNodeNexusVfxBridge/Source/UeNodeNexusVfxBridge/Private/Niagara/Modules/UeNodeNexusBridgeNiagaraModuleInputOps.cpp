#include "UeNodeNexusNiagaraOps.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"
#include "UeNodeNexusBridgeSaveResult.h"
#include "UeNodeNexusBridgeNiagaraModuleStack.h"
#include "Binding/NexusNiagaraInputBinding.h"
#include "Binding/NexusNiagaraInputMetadata.h"
#include "UeNodeNexusVfxTranscode.h"

#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSystem.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"

namespace UeNodeNexusBridge
{
namespace
{
struct FModuleInputRecord
{
    FGuid VariableGuid;
    FString Name;
    FString ShortName;
    FString TypeName;
    FString DefaultValueText;
    FString OverrideValueText;
    bool bHasOverride = false;
    bool bOverrideLinked = false;
    bool bStatic = false;
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

void GatherModuleInputs(FNiagaraEmitterHandle* Emitter, UNiagaraNodeOutput* Output,
    UNiagaraNodeFunctionCall* Module, TArray<FModuleInputRecord>& OutInputs)
{
    UNiagaraGraph* CalledGraph = Module ? Module->GetCalledGraph() : nullptr;
    if (!CalledGraph)
    {
        return;
    }

    for (const TPair<FNiagaraVariable, TObjectPtr<UNiagaraScriptVariable>>& Pair : CalledGraph->GetAllMetaData())
    {
        const FNiagaraVariable& Variable = Pair.Key;
        const FString Name = Variable.GetName().ToString();
        if (!VfxTranscode::IsAuthoredModuleInput(Variable, Pair.Value))
        {
            continue;
        }

        FModuleInputRecord Record;
        Record.Name = Name;
        Record.ShortName = StripModuleNamespace(Variable.GetName());
        Record.TypeName = Variable.GetType().GetName();
        Record.bStatic = Pair.Value && Pair.Value->GetIsStaticSwitch();
        Record.DefaultValueText = VfxTranscode::ModuleInputDefaultText(Variable, Pair.Value);
        if (Pair.Value)
        {
            Record.VariableGuid = Pair.Value->Metadata.GetVariableGuid();
        }

        const FNiagaraParameterHandle AliasedHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(Variable.GetName()), Module);
        if (UEdGraphPin* OverridePin = VfxTranscode::FindModuleOverridePin(*Module, AliasedHandle.GetParameterHandleString()))
        {
            Record.bHasOverride = true;
            Record.bOverrideLinked = OverridePin->LinkedTo.Num() > 0;
            Record.OverrideValueText = OverridePin->DefaultValue;
        }
        else if (!Record.bStatic && Emitter && Output)
        {
            FNiagaraVariable RapidVariable;
            UNiagaraScript* Script = nullptr;
            if (VfxTranscode::ResolveRapidIterationInput(*Emitter, Output->GetUsage(), Output->GetUsageId(),
                Module, Record.ShortName, RapidVariable, Script)
                && Script->RapidIterationParameters.IndexOf(RapidVariable) != INDEX_NONE)
            {
                Record.bHasOverride = true;
                Record.OverrideValueText = VfxTranscode::ParameterValueText(Script->RapidIterationParameters, RapidVariable);
            }
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
    Json->SetBoolField(TEXT("static"), Input.bStatic);
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
    GatherModuleInputs(Emitter, NiagaraModuleStack::ResolveOutputNode(Emitter, Payload->GetStringField(TEXT("usage"))), Module, Inputs);

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
    GatherModuleInputs(Emitter, NiagaraModuleStack::ResolveOutputNode(Emitter, Payload->GetStringField(TEXT("usage"))), Module, Inputs);
    TArray<TSharedPtr<FJsonValue>> Items;
    bool bChanged = false;
    VfxTranscode::FInputBindingContext Bindings;

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

        const FString Before = Match->bHasOverride ? Match->OverrideValueText : Match->DefaultValueText;
        VfxTranscode::FInputBindingError Error;
        bool bInputChanged = false;
        if (!VfxTranscode::ApplyNativeInput(*System, *Emitter, *Module, Name, ValueText,
            TEXT("ns_module_input_set"), bDryRun, Bindings, bInputChanged, Error))
        {
            TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
            Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(Error.Code, Error.Message));
            return Response;
        }
        bChanged |= bInputChanged;

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Match->Name);
        Item->SetStringField(TEXT("short_name"), Match->ShortName);
        Item->SetStringField(TEXT("before_value_text"), Before);
        Item->SetStringField(TEXT("after_value_text"), bDryRun ? Before : ValueText);
        Item->SetBoolField(TEXT("changed"), bInputChanged);
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }

    const auto Save = SaveAssetWrite(System, bSave, bDryRun, bChanged);
    const bool bSaved = Save.bSaved;
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
    Data->SetArrayField(TEXT("native_input_results"), Bindings.Results);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    ApplyAssetWriteSave(Response, Data, Save);
    return Response;
}
}
