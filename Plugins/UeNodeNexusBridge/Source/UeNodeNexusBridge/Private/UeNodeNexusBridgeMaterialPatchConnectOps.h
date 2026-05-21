#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UMaterial;

namespace UeNodeNexusBridge
{
struct FMaterialPatchContext;

bool ApplyMaterialPatchConnect(UMaterial* Material, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, TArray<TSharedPtr<FJsonValue>>& Diagnostics, const FMaterialPatchContext& Context);
bool ApplyMaterialPatchDisconnect(UMaterial* Material, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, const FMaterialPatchContext& Context);
}
