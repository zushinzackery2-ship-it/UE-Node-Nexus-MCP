#pragma once

#include "Dom/JsonObject.h"

namespace NexusLifecycle
{
inline bool ReadString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& Value)
{
    return Object.IsValid() && Object->HasTypedField<EJson::String>(Field) && Object->TryGetStringField(Field, Value);
}

inline bool ReadNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double& Value)
{
    return Object.IsValid() && Object->HasTypedField<EJson::Number>(Field) && Object->TryGetNumberField(Field, Value);
}

inline bool ReadBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool& Value)
{
    return Object.IsValid() && Object->HasTypedField<EJson::Boolean>(Field) && Object->TryGetBoolField(Field, Value);
}
}
