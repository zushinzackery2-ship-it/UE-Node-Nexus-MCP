#pragma once

#include "CoreMinimal.h"

class UNiagaraSystem;
class FJsonValue;

namespace UeNodeNexusBridge::NiagaraAssetLint
{
void CollectSystemIssues(UNiagaraSystem* System, TArray<TSharedPtr<FJsonValue>>& Issues);
}
