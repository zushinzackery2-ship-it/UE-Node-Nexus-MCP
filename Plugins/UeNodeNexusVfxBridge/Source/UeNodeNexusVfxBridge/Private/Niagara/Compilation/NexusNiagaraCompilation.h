#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UNiagaraSystem;

namespace UeNodeNexusBridge::NiagaraCompilation
{
bool Wait(UNiagaraSystem* System);
TArray<TSharedPtr<FJsonValue>> Diagnostics(UNiagaraSystem* System);
}
