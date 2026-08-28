// FString stub for compiler-only checks; see CoreTypes.h for the rationale.
#pragma once

#include "CoreTypes.h"

#include <string>

class FString
{
public:
    FString() = default;
    FString(const TCHAR* Value);
    FString(const FString&) = default;
    FString(FString&&) = default;
    FString& operator=(const FString&) = default;
    FString& operator=(FString&&) = default;

    bool IsEmpty() const;
    int32 Len() const;
    bool FindChar(TCHAR Char, int32& OutIndex) const;
    void LeftInline(int32 Count, bool bAllowShrinking = true);
    bool StartsWith(const TCHAR* Prefix) const;
    bool StartsWith(const FString& Prefix) const;
    bool Contains(const TCHAR* Substring) const;
    bool Contains(const FString& Substring) const;
    bool Equals(const FString& Other, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive) const;

    const TCHAR* operator*() const;
    FString operator+(const TCHAR* Suffix) const;
    FString operator+(const FString& Suffix) const;
    FString& operator+=(const TCHAR* Suffix);
    bool operator==(const FString& Other) const;
    bool operator!=(const FString& Other) const;

    // UE's FString supports range-based for over its characters.
    const TCHAR* begin() const;
    const TCHAR* end() const;

    static FString FromInt(int32 Value);

    template <typename... ArgTypes>
    static FString Printf(const TCHAR* Format, ArgTypes&&... Args)
    {
        (void)Format;
        return FString();
    }

private:
    std::wstring Storage;
};
