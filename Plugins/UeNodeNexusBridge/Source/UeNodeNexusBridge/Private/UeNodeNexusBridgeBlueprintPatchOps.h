#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UBlueprint;

namespace UeNodeNexusBridge
{
bool IsBlueprintPatchAsset(UObject* Asset);
TSharedPtr<FJsonObject> HandleBlueprintGraphPatch(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleBlueprintNodeParamsGet(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleBlueprintNodeParamsSet(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload);
}
