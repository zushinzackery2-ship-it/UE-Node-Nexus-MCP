#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UMaterial;

namespace UeNodeNexusBridge
{
struct FMaterialPatchContext;

bool ApplyMaterialPatchCreateNode(UMaterial* Material, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, TArray<TSharedPtr<FJsonValue>>& Diagnostics, FMaterialPatchContext& Context);
bool ApplyMaterialPatchNodeOperation(UMaterial* Material, const FString& OpName, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, TArray<TSharedPtr<FJsonValue>>& Diagnostics, FMaterialPatchContext& Context);
}
