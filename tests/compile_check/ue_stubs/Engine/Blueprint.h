// UBlueprint stub for compiler-only checks; see CoreTypes.h.
#pragma once

#include "UObject/Object.h"

class UBlueprint : public UObject
{
public:
    UClass* GeneratedClass = nullptr;

    static UClass* StaticClass();
};
