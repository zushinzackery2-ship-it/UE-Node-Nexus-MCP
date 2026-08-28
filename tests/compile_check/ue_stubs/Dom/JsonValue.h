// FJsonValue stubs for compiler-only checks; see CoreTypes.h.
#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

class FJsonObject;

class FJsonValue
{
public:
    virtual ~FJsonValue() = default;
};

class FJsonValueString : public FJsonValue
{
public:
    FJsonValueString(const FString& InValue);
};

class FJsonValueNumber : public FJsonValue
{
public:
    FJsonValueNumber(double InValue);
};

class FJsonValueBoolean : public FJsonValue
{
public:
    FJsonValueBoolean(bool InValue);
};

class FJsonValueArray : public FJsonValue
{
public:
    FJsonValueArray(const TArray<TSharedPtr<FJsonValue>>& InValue);
};

class FJsonValueObject : public FJsonValue
{
public:
    FJsonValueObject(TSharedPtr<FJsonObject> InValue);
};
