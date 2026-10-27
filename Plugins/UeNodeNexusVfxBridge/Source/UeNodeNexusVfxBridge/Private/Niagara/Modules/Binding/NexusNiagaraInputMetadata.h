#pragma once

#include "CoreMinimal.h"
#include "NiagaraTypes.h"

class UNiagaraScriptVariable;

namespace UeNodeNexusBridge::VfxTranscode
{
bool IsAuthoredModuleInput(const FNiagaraVariable& Variable, const UNiagaraScriptVariable* Metadata);
FString ModuleInputDefaultText(const FNiagaraVariable& Variable, const UNiagaraScriptVariable* Metadata);
}
