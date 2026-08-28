// FDateTime stub for compiler-only checks; see CoreTypes.h.
#pragma once

#include "Containers/UnrealString.h"

struct FDateTime
{
    static FDateTime Now();

    FString ToString() const;
    FString ToString(const TCHAR* Format) const;
};
