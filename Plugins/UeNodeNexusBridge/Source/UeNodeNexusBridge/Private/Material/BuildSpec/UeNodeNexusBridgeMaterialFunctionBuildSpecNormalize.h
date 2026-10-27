#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UMaterialFunction;

namespace UeNodeNexusBridge
{
void AppendMaterialFunctionBuildSpecOperations(
    const TSharedPtr<FJsonObject>& Payload,
    TArray<TSharedPtr<FJsonValue>>& OutOperations,
    TArray<TSharedPtr<FJsonValue>>& Diagnostics,
    UMaterialFunction* Function);
}
