// FName stub for compiler-only checks; see CoreTypes.h for the rationale.
#pragma once

#include "Containers/UnrealString.h"

class FName
{
public:
    FName() = default;
    FName(const TCHAR* Value);

    FString ToString() const;
    bool operator==(const FName& Other) const;
    bool operator!=(const FName& Other) const;
};

struct FNameLexicalLess
{
    bool operator()(const FName& A, const FName& B) const;
};
