// LoadObject / LoadClass stubs for compiler-only checks; see CoreTypes.h.
#pragma once

#include "UObject/Object.h"

template <typename ObjectType>
ObjectType* LoadObject(UObject* Outer, const TCHAR* Name, const TCHAR* Filename = nullptr)
{
    (void)Outer;
    (void)Name;
    (void)Filename;
    return nullptr;
}

template <typename ObjectType>
UClass* LoadClass(UObject* Outer, const TCHAR* Name, const TCHAR* Filename = nullptr)
{
    (void)Outer;
    (void)Name;
    (void)Filename;
    return nullptr;
}
