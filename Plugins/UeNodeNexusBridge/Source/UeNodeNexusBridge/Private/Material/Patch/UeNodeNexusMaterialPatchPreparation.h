#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UeNodeNexusBridge
{
UObject* MakeMaterialPatchPreview(UObject* Asset);
bool ValidateMaterialPatchAliases(UObject* Asset, const TArray<TSharedPtr<FJsonValue>>& Operations,
    TArray<TSharedPtr<FJsonValue>>& Diagnostics);
bool MaterialPatchHasChanges(const TSharedPtr<FJsonObject>& Diff);
}
