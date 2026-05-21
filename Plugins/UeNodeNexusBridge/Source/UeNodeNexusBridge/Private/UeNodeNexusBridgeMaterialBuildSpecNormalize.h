#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UMaterial;

namespace UeNodeNexusBridge
{
void AppendMaterialBuildSpecOperations(
    const TSharedPtr<FJsonObject>& Payload,
    TArray<TSharedPtr<FJsonValue>>& OutOperations,
    TArray<TSharedPtr<FJsonValue>>& Diagnostics,
    UMaterial* Material);
}
