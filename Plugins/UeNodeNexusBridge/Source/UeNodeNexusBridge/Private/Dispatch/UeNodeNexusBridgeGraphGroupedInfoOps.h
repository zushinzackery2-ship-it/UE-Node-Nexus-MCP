#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UBlueprint;
class UEdGraph;
class UMaterial;
class UMaterialFunction;

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> BuildMaterialGraphGroupedData(UMaterial* Material, const TSharedPtr<FJsonObject>& Payload, bool bWithPosition);
TSharedPtr<FJsonObject> BuildMaterialFunctionGraphGroupedData(UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload, bool bWithPosition);
struct FBlueprintGraphFilterResult;
TSharedPtr<FJsonObject> BuildBlueprintGraphGroupedData(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Payload, bool bWithPosition, const FBlueprintGraphFilterResult& FilterResult);
}
