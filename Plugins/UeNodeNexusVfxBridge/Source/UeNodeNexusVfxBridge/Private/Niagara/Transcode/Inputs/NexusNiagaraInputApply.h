#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"

class FJsonObject;
class UNiagaraNodeFunctionCall;
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
    UNiagaraNodeFunctionCall& Module,
    const TSharedPtr<FJsonObject>& Op, int32 Index, Transcode::FApplyContext& Context,
    FInputBindingContext& Bindings);
}
