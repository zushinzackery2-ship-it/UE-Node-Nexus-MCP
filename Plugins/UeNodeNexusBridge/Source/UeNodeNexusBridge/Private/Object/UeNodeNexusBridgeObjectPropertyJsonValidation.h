#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UStruct;
class FProperty;

namespace UeNodeNexusBridge
{
const FProperty* ResolveStructJsonField(const UStruct* Struct, const FString& Name);
TSharedPtr<FJsonObject> NormalizeStructJson(const UStruct* Struct, const TSharedPtr<FJsonObject>& Json);
bool ValidateStructJson(
    const UStruct* Struct,
    const TSharedPtr<FJsonObject>& Json,
    const FString& Prefix,
    FString& OutError);
}
