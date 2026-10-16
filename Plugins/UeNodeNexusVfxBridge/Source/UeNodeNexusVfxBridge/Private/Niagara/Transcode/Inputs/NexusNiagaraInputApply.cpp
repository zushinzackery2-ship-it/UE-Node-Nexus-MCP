#include "NexusNiagaraInputApply.h"

#include "Binding/NexusNiagaraInputBinding.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "UeNodeNexusBridgeNiagaraModuleStack.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UeNodeNexusNiagaraOps.h"
#include "UeNodeNexusVfxTranscode.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusNiagaraInputApply, Log, All);

namespace UeNodeNexusBridge::VfxTranscode
{
namespace
{
void RejectInput(UNiagaraSystem& System, FNiagaraEmitterHandle& Emitter,
    UNiagaraNodeFunctionCall& Module, int32 Index, const FString& Name,
    const FString& Value, const FInputBindingError& Error, Transcode::FApplyContext& Context)
{
    const FString Message = FString::Printf(TEXT("asset=%s emitter=%s module_guid=%s input=%s requested=%s: %s"),
        *System.GetPathName(), *Emitter.GetName().ToString(), *Module.NodeGuid.ToString(),
        *Name, *Value, *Error.Message);
    UE_LOG(LogNexusNiagaraInputApply, Error, TEXT("%s: %s"), *Error.Code, *Message);
    Context.Fail(Index, Error.Code, Message);
}
}

void ApplyModuleInput(UNiagaraSystem& System, FNiagaraEmitterHandle& Emitter,
    UNiagaraNodeOutput& Output, UNiagaraNodeFunctionCall& Module, ENiagaraScriptUsage Usage,
    const TSharedPtr<FJsonObject>& Op, int32 Index, Transcode::FApplyContext& Context,
    const TSharedPtr<FJsonObject>& Payload, FInputBindingContext& Bindings)
{
    FString Verb;
    FString Name;
    Op->TryGetStringField(TEXT("op"), Verb);
    Op->TryGetStringField(TEXT("input"), Name);
    const bool bLink = Verb == TEXT("ns_module_input_link");
    FString Value;
    FInputBindingError Error;
    if (Name.IsEmpty() || !Op->TryGetStringField(bLink ? TEXT("parameter")
        : (Verb == TEXT("ns_module_input_set") ? TEXT("value") : TEXT("default")), Value))
    {
        Error.Code = TEXT("invalid_input_verb");
        Error.Message = TEXT("input and typed parameter/value/default fields are required");
        RejectInput(System, Emitter, Module, Index, Name, Value, Error, Context);
        return;
    }
    FNiagaraVariable Variable;
    if (!ResolveModuleInput(Module, Name, Variable, Error))
    {
        RejectInput(System, Emitter, Module, Index, Name, Value, Error, Context);
        return;
    }
    if (bLink || HasLinkedOverride(Module, Variable))
    {
        bool bChanged = false;
        const bool bOk = ApplyNativeInput(System, Emitter, Module, Name, Value, Verb,
            Context.bDryRun, Bindings, bChanged, Error);
        Context.bChanged |= bChanged;
        if (!bOk)
        {
            Context.Fail(Index, Error.Code, Error.Message);
        }
        return;
    }
    FNiagaraVariable RapidVariable;
    UNiagaraScript* Script = nullptr;
    if (ResolveRapidIterationInput(Emitter, Usage, Output.GetUsageId(), &Module, Name, RapidVariable, Script)
        && Script->RapidIterationParameters.IndexOf(RapidVariable) != INDEX_NONE)
    {
        Script->Modify();
        bool bOk = true;
        if (Verb == TEXT("ns_module_input_reset"))
        {
            Script->RapidIterationParameters.RemoveParameter(RapidVariable);
        }
        else
        {
            bOk = SetParameterValueText(Script->RapidIterationParameters, RapidVariable, Value, false, &Error.Message);
        }
        if (!bOk)
        {
            Error.Code = TEXT("input_value_invalid");
            RejectInput(System, Emitter, Module, Index, Name, Value, Error, Context);
            return;
        }
        NiagaraModuleStack::MarkSystemEdited(&System);
        Context.bChanged = true;
        return;
    }
    TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
    Param->SetStringField(TEXT("name"), Name);
    Param->SetStringField(TEXT("value_text"), Value);
    TArray<TSharedPtr<FJsonValue>> Params;
    Params.Add(MakeShared<FJsonValueObject>(Param));
    Payload->SetArrayField(TEXT("params"), Params);
    Payload->SetBoolField(TEXT("dry_run"), false);
    Payload->SetBoolField(TEXT("save"), false);
    const TSharedPtr<FJsonObject> Response = HandleNiagaraModuleInputsSet(TEXT("niagara_module_inputs_set"), TEXT("transcode"), Payload);
    if (!Response.IsValid() || !Response->GetBoolField(TEXT("ok")))
    {
        const TSharedPtr<FJsonObject>* Failure = nullptr;
        const FString Message = Response.IsValid() && Response->TryGetObjectField(TEXT("error"), Failure)
            ? (*Failure)->GetStringField(TEXT("message")) : TEXT("module input handler returned no response");
        Error.Code = TEXT("input_failed");
        Error.Message = Message;
        RejectInput(System, Emitter, Module, Index, Name, Value, Error, Context);
        return;
    }
    Context.bChanged = true;
}
}
