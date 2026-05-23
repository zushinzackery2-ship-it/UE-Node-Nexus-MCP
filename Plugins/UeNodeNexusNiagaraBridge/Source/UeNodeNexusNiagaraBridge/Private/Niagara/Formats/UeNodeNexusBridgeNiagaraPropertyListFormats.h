#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace UeNodeNexusBridge::NiagaraPropertyListFormats
{
bool IsSupportedFormat(const FString& Format);
TSharedPtr<FJsonObject> MakeInvalidFormatResponse(const FString& Operation, const FString& RequestId, const FString& Format);
void AddObjectIdentity(UObject* Object, TSharedPtr<FJsonObject> Data);
TSharedPtr<FJsonObject> BuildObjectPropertiesData(
    UObject* Object,
    const TSharedPtr<FJsonObject>& Payload,
    const FString& Format,
    const FString& CompactFormatName,
    const FString& IndexedFormatName,
    const FString& TinyFormatName);
}
