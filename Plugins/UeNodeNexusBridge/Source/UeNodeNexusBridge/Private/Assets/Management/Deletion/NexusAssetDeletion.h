#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> DeleteAssetSet(const FString& Operation, const FString& RequestId,
    const TArray<FString>& AssetPaths, const TSharedPtr<FJsonObject>& Payload);
}
