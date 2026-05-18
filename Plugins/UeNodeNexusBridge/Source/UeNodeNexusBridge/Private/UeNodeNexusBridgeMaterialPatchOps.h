#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UMaterial;

namespace UeNodeNexusBridge
{
bool IsMaterialPatchAsset(UObject* Asset);
TSharedPtr<FJsonObject> HandleMaterialGraphPatch(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleMaterialNodeParamsGet(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleMaterialNodeParamsSet(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload);
}
