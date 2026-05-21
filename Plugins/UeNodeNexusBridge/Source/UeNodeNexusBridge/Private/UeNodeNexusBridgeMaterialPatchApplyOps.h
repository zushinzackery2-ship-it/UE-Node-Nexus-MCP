#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UMaterial;

namespace UeNodeNexusBridge
{
struct FMaterialPatchContext;

bool ApplyMaterialPatchOperation(
    UMaterial* Material,
    const TSharedPtr<FJsonObject>& Op,
    bool bDryRun,
    TSharedPtr<FJsonObject> Diff,
    TArray<TSharedPtr<FJsonValue>>& Diagnostics,
    FMaterialPatchContext& Context);
}
