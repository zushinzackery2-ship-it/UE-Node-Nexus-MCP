#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> WriteNiagaraProperties(const FString& Operation, const FString& RequestId,
    UObject* Object, UObject* Asset, const TSharedPtr<FJsonObject>& Payload, const TSharedPtr<FJsonObject>& Data, bool bFull);
}
