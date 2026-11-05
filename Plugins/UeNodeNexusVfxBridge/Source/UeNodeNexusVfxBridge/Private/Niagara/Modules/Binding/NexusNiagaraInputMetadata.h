#pragma once

#include "CoreMinimal.h"
#include "NiagaraTypes.h"

class UNiagaraScriptVariable;
class UNiagaraGraph;
class UEdGraphPin;

namespace UeNodeNexusBridge::VfxTranscode
{
bool IsAuthoredModuleInput(const FNiagaraVariable& Variable, const UNiagaraScriptVariable* Metadata);
TArray<const UEdGraphPin*> FindModuleInputDefaultPins(const UNiagaraGraph& Graph, FName VariableName);
FString ModuleInputDefaultText(const FNiagaraVariable& Variable, const UNiagaraScriptVariable* Metadata,
    const UNiagaraGraph* Graph = nullptr);
}
