// IFileManager stub for compiler-only checks; see CoreTypes.h.
#pragma once

#include "CoreTypes.h"

class IFileManager
{
public:
    static IFileManager& Get();
    virtual bool FileExists(const TCHAR* Filename) = 0;
};
