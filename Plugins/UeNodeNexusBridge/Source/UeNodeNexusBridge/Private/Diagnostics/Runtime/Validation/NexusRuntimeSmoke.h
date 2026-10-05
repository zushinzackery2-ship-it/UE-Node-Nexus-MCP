#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UeNodeNexusBridge::RuntimeSmoke
{
TSharedPtr<FJsonObject> Start(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> Status(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
void Stop();
}
