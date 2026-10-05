#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UeNodeNexusBridge::Safety
{
TSharedPtr<FJsonObject> ExportBaseline(const FString& Operation, const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload);
}
