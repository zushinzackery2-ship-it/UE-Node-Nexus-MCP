// FJsonObject stub for compiler-only checks; see CoreTypes.h.
#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Dom/JsonValue.h"
#include "Templates/SharedPointer.h"

class FJsonObject
{
public:
    bool TryGetStringField(const FString& FieldName, FString& OutValue) const;
    bool TryGetBoolField(const FString& FieldName, bool& OutValue) const;
    bool TryGetNumberField(const FString& FieldName, double& OutValue) const;
    bool TryGetNumberField(const FString& FieldName, int32& OutValue) const;
    bool TryGetObjectField(const FString& FieldName, const TSharedPtr<FJsonObject>*& OutObject) const;
    bool TryGetArrayField(const FString& FieldName, const TArray<TSharedPtr<FJsonValue>>*& OutArray) const;
    bool HasField(const FString& FieldName) const;

    void SetStringField(const FString& FieldName, const FString& Value);
    void SetBoolField(const FString& FieldName, bool Value);
    void SetNumberField(const FString& FieldName, double Value);
    void SetArrayField(const FString& FieldName, const TArray<TSharedPtr<FJsonValue>>& Value);
    void SetObjectField(const FString& FieldName, const TSharedPtr<FJsonObject>& Value);
};
