#include "NexusNiagaraInputBinding.h"

#include "NiagaraEmitterHandle.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "UObject/StructOnScope.h"
#include "UeNodeNexusBridgeNiagaraModuleStack.h"
#include "UeNodeNexusVfxTranscode.h"
#include "Niagara/Transcode/Values/Storage/NexusNiagaraStorage.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusNiagaraInputBinding, Log, All);

namespace UeNodeNexusBridge::VfxTranscode
{
namespace
{
using EMode = UNiagaraStackFunctionInput::EValueMode;

bool Reject(UNiagaraSystem& System, FNiagaraEmitterHandle& Emitter, UNiagaraNodeFunctionCall& Module,
    const FString& Name, const FString& Value, FInputBindingError& Error)
{
    Error.Message = FString::Printf(TEXT("asset=%s emitter=%s module_guid=%s input=%s requested=%s: %s"),
        *System.GetPathName(), *Emitter.GetName().ToString(), *Module.NodeGuid.ToString(),
        *Name, *Value, *Error.Message);
    UE_LOG(LogNexusNiagaraInputBinding, Error, TEXT("%s: %s"), *Error.Code, *Error.Message);
    return false;
}

bool ParseLocal(const FNiagaraVariable& Variable, const FString& Text,
    TSharedPtr<FStructOnScope>& Value, FInputBindingError& Error)
{
    if (!Variable.GetType().GetStruct())
    {
        Error.Code = TEXT("input_local_type_unsupported");
        Error.Message = TEXT("local value requires a reflected Niagara struct type");
        return false;
    }
    FNiagaraParameterStore Store;
    Value = MakeShared<FStructOnScope>(Variable.GetType().GetStruct());
    if (!SetParameterValueText(Store, Variable, Text, true, &Error.Message)
        || !Storage::ReadBytes(Store, Variable, Value->GetStructMemory(), Variable.GetType().GetSize(), Error.Message))
    {
        Error.Code = TEXT("input_value_invalid");
        return false;
    }
    return true;
}

bool LocalEqual(UNiagaraStackFunctionInput& Input, const FStructOnScope& Value)
{
    if (Input.GetValueMode() != EMode::Local)
    {
        return false;
    }
    const TSharedPtr<const FStructOnScope> Actual = Input.GetLocalValueStruct();
    return Actual.IsValid() && FMemory::Memcmp(
        Actual->GetStructMemory(), Value.GetStructMemory(), Input.GetInputType().GetSize()) == 0;
}
}

bool ApplyNativeInput(UNiagaraSystem& System, FNiagaraEmitterHandle& Emitter,
    UNiagaraNodeFunctionCall& Module, const FString& Name, const FString& Value,
    const FString& Verb, bool bDryRun, FInputBindingContext& Context,
    bool& bChanged, FInputBindingError& Error)
{
    bChanged = false;
    const bool bLink = Verb == TEXT("ns_module_input_link");
    const bool bReset = Verb == TEXT("ns_module_input_reset");
    FNiagaraVariable Variable;
    if (!ResolveModuleInput(Module, Name, Variable, Error)
        || (bLink && !ValidateUserParameter(System.GetExposedParameters(), Value, Variable.GetType(), Error)))
    {
        return Reject(System, Emitter, Module, Name, Value, Error);
    }
    TSharedPtr<FStructOnScope> Local;
    if (!bLink && !bReset && !ParseLocal(Variable, Value, Local, Error))
    {
        return Reject(System, Emitter, Module, Name, Value, Error);
    }
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("module_guid"), Module.NodeGuid.ToString());
    Result->SetStringField(TEXT("input"), Variable.GetName().ToString());
    Result->SetStringField(TEXT("type"), Variable.GetType().GetName());
    Result->SetStringField(TEXT("requested"), Value);
    Result->SetBoolField(TEXT("dry_run"), bDryRun);
    if (bDryRun)
    {
        Context.Results.Add(MakeShared<FJsonValueObject>(Result));
        return true;
    }
    UNiagaraStackFunctionInput* Input = Context.Resolve(System, Emitter, Module, Variable, Error);
    if (!Input)
    {
        return Reject(System, Emitter, Module, Name, Value, Error);
    }
    const FNiagaraParameterHandle Handle = FNiagaraParameterHandle(FName(*Value));
    const bool bWasLinked = Input->GetValueMode() == EMode::Linked;
    Result->SetStringField(TEXT("before_linked"), bWasLinked ? Input->GetLinkedValueHandle().GetParameterHandleString().ToString() : FString());
    Result->SetNumberField(TEXT("before_mode"), static_cast<int32>(Input->GetValueMode()));
    if (bLink)
    {
        bChanged = !bWasLinked || Input->GetLinkedValueHandle() != Handle;
        if (bChanged)
        {
            Input->SetLinkedValueHandle(Handle);
        }
    }
    else if (bReset)
    {
        bChanged = Input->CanReset();
        if (bChanged)
        {
            Input->Reset();
        }
    }
    else
    {
        bChanged = !LocalEqual(*Input, *Local);
        if (bChanged)
        {
            Input->SetLocalValue(Local.ToSharedRef());
        }
    }
    Input = Context.Resolve(System, Emitter, Module, Variable, Error);
    if (!Input)
    {
        return Reject(System, Emitter, Module, Name, Value, Error);
    }
    const bool bVerified = bLink ? Input->GetValueMode() == EMode::Linked && Input->GetLinkedValueHandle() == Handle
        : (bReset ? !Input->CanReset() : LocalEqual(*Input, *Local));
    if (!bVerified)
    {
        Error.Code = TEXT("input_binding_readback_mismatch");
        Error.Message = FString::Printf(TEXT("verb=%s actual_mode=%d"), *Verb, static_cast<int32>(Input->GetValueMode()));
        return Reject(System, Emitter, Module, Name, Value, Error);
    }
    if (bChanged)
    {
        NiagaraModuleStack::MarkSystemEdited(&System);
    }
    Result->SetNumberField(TEXT("after_mode"), static_cast<int32>(Input->GetValueMode()));
    Result->SetStringField(TEXT("after_linked"), bLink ? Value : FString());
    Result->SetBoolField(TEXT("changed"), bChanged);
    Context.Results.Add(MakeShared<FJsonValueObject>(Result));
    UE_LOG(LogNexusNiagaraInputBinding, Display, TEXT("asset=%s emitter=%s module_guid=%s input=%s type=%s verb=%s requested=%s changed=%d"),
        *System.GetPathName(), *Emitter.GetName().ToString(), *Module.NodeGuid.ToString(),
        *Name, *Variable.GetType().GetName(), *Verb, *Value, bChanged);
    return true;
}
}
