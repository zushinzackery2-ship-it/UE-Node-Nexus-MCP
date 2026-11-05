#include "NexusNiagaraInputApply.h"

#include "Binding/NexusNiagaraInputBinding.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraSystem.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

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
    UNiagaraNodeFunctionCall& Module,
    const TSharedPtr<FJsonObject>& Op, int32 Index, Transcode::FApplyContext& Context,
    FInputBindingContext& Bindings)
{
    FString Verb;
    FString Name;
    Op->TryGetStringField(TEXT("op"), Verb);
    Op->TryGetStringField(TEXT("input"), Name);
    const bool bLink = Verb == TEXT("ns_module_input_link") || Verb == TEXT("ns_module_input_convert");
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
    bool bChanged = false;
    if (!ApplyNativeInput(System, Emitter, Module, Name, Value, Verb,
        Context.bDryRun, Bindings, bChanged, Error))
    {
        RejectInput(System, Emitter, Module, Index, Name, Value, Error, Context);
        return;
    }
    Context.bChanged |= bChanged;
}
}
