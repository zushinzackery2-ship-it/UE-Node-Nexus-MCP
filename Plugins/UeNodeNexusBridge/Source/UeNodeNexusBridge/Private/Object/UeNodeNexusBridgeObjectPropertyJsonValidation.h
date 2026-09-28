#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UStruct;
class FProperty;

namespace UeNodeNexusBridge
{
bool ValidateJsonPropertyValue(const FProperty* Property, const TSharedPtr<FJsonValue>& Value,
    const FString& Path, FString& OutError);
const FProperty* ResolveStructJsonField(const UStruct* Struct, const FString& Name);
TSharedPtr<FJsonObject> NormalizeStructJson(const UStruct* Struct, const TSharedPtr<FJsonObject>& Json);
bool ValidateStructJson(
    const UStruct* Struct,
    const TSharedPtr<FJsonObject>& Json,
    const FString& Prefix,
    FString& OutError);
}
