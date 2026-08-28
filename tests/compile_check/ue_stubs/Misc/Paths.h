// FPaths stub for compiler-only checks; see CoreTypes.h.
#pragma once

#include "Containers/UnrealString.h"

class FPaths
{
public:
    static FString ScreenShotDir();
    static FString ProjectSavedDir();
    static FString Combine(const FString& PathA, const FString& PathB);
};
