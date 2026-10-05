#pragma once

#include "CoreMinimal.h"

struct FNiagaraParameterStore;
struct FNiagaraVariable;

namespace UeNodeNexusBridge::VfxTranscode::Storage
{
bool ValidateBytes(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable,
    int32 DestinationSize, FString& Error);
bool ReadBytes(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable,
    void* Destination, int32 DestinationSize, FString& Error);
bool ReadObject(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable,
    UObject*& Object, FString& Error);
bool ReadStructText(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable,
    FString& Text, FString& Error);
}
