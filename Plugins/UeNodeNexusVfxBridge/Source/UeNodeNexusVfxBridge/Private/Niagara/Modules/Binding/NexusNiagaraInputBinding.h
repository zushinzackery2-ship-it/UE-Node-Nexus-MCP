#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "NiagaraTypes.h"

class FNiagaraSystemViewModel;
class UNiagaraNodeFunctionCall;
class UNiagaraStackFunctionInput;
class UNiagaraSystem;
struct FNiagaraEmitterHandle;
struct FNiagaraUserRedirectionParameterStore;

namespace UeNodeNexusBridge::VfxTranscode
{
struct FInputBindingError
{
    FString Code;
    FString Message;
};

// Scoped to one transcode plan; lazily creates one native data-processing VM.
struct FInputBindingContext
{
    TSharedPtr<FNiagaraSystemViewModel> ViewModel;
    TArray<TSharedPtr<FJsonValue>> Results;

    UNiagaraStackFunctionInput* Resolve(UNiagaraSystem& System, FNiagaraEmitterHandle& Emitter,
        UNiagaraNodeFunctionCall& Module, const FNiagaraVariable& Variable, FInputBindingError& Error);
};

bool ResolveModuleInput(UNiagaraNodeFunctionCall& Module, const FString& Name,
    FNiagaraVariable& Variable, FInputBindingError& Error);
bool ValidateUserParameter(const FNiagaraUserRedirectionParameterStore& Store,
    const FString& Parameter, const FNiagaraTypeDefinition& Type, FInputBindingError& Error);
bool HasLinkedOverride(UNiagaraNodeFunctionCall& Module, const FNiagaraVariable& Variable);

// Native transitions own override/rapid-iteration cleanup and strict readback.
bool ApplyNativeInput(UNiagaraSystem& System, FNiagaraEmitterHandle& Emitter,
    UNiagaraNodeFunctionCall& Module, const FString& Name, const FString& Value,
    const FString& Verb, bool bDryRun, FInputBindingContext& Context,
    bool& bChanged, FInputBindingError& Error);
}
