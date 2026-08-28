// FPackageName stub for compiler-only checks; see CoreTypes.h.
#pragma once

#include "Containers/UnrealString.h"

class FPackageName
{
public:
    static bool TryConvertLongPackageNameToFilename(
        const FString& InLongPackageName,
        FString& OutFilename,
        const FString& InExtension = FString());

    static FString GetMapPackageExtension();
};
