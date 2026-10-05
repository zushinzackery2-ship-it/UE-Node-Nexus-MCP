#pragma once

#include "CoreMinimal.h"
#include "NiagaraTypes.h"

namespace UeNodeNexusBridge::VfxTranscode
{
struct FParsedValue
{
    double Components[4] =
    {
        0, 0, 0, 1
    };
    int32 Integer = 0;
    bool Boolean = false;
    UObject* Object = nullptr;
};

bool ParseParameterValue(const FNiagaraTypeDefinition& Type, const FString& Text, FParsedValue& Out, FString& Error);
}
