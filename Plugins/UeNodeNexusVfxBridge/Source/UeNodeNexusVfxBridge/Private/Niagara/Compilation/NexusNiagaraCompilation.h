#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UNiagaraSystem;

namespace UeNodeNexusBridge::NiagaraCompilation
{
bool Request(UNiagaraSystem* System);
bool Wait(UNiagaraSystem* System);
bool Runnable(UNiagaraSystem* System);
void AppendStatus(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Data);
TArray<TSharedPtr<FJsonValue>> Diagnostics(UNiagaraSystem* System);
}
