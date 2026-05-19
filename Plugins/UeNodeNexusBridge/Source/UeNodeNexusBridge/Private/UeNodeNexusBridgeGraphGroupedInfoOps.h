#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UBlueprint;
class UEdGraph;
class UMaterial;

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> BuildMaterialGraphGroupedData(UMaterial* Material, const TSharedPtr<FJsonObject>& Payload, bool bWithPosition);
TSharedPtr<FJsonObject> BuildBlueprintGraphGroupedData(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Payload, bool bWithPosition);
}
