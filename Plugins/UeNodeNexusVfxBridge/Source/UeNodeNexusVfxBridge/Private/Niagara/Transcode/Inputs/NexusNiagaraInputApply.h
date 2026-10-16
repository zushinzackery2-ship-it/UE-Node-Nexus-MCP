#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"

class FJsonObject;
class UNiagaraNodeFunctionCall;
class UNiagaraNodeOutput;
class UNiagaraSystem;
struct FNiagaraEmitterHandle;

namespace UeNodeNexusBridge::Transcode
{
struct FApplyContext;
}

namespace UeNodeNexusBridge::VfxTranscode
{
struct FInputBindingContext;
void ApplyModuleInput(UNiagaraSystem& System, FNiagaraEmitterHandle& Emitter,
    UNiagaraNodeOutput& Output, UNiagaraNodeFunctionCall& Module, ENiagaraScriptUsage Usage,
    const TSharedPtr<FJsonObject>& Op, int32 Index, Transcode::FApplyContext& Context,
    const TSharedPtr<FJsonObject>& Payload, FInputBindingContext& Bindings);
}
