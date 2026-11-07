#pragma once

#include "Niagara/Modules/Binding/NexusNiagaraInputBinding.h"

class FAutomationTestBase;
class UNiagaraNodeOutput;
struct FNiagaraEmitterHandle;

namespace UeNodeNexusBridge::Tests
{
struct FNiagaraVectorFieldFixture
{
    UNiagaraSystem* System = nullptr;
    FNiagaraEmitterHandle* Emitter = nullptr;
    UNiagaraNodeFunctionCall* Sample = nullptr;
    UNiagaraNodeFunctionCall* Force = nullptr;
    UNiagaraNodeOutput* Output = nullptr;
    VfxTranscode::FInputBindingContext Bindings;

    bool Initialize(FAutomationTestBase& Test, bool bWriterFirst);
    FString OutputHandle() const;
};
}

