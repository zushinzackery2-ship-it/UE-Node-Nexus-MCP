#pragma once

#include "CoreMinimal.h"

class FJsonValue;
class UEdGraph;
class UEdGraphNode;

namespace UeNodeNexusBridge
{
TArray<TSharedPtr<FJsonValue>> BuildBlueprintCompactInputRows(UEdGraph* Graph, UEdGraphNode* Node);
TArray<TSharedPtr<FJsonValue>> BuildBlueprintCompactParamRows(UEdGraphNode* Node);
TArray<TSharedPtr<FJsonValue>> BuildBlueprintCompactOutputRows(UEdGraph* Graph, UEdGraphNode* Node);
}
