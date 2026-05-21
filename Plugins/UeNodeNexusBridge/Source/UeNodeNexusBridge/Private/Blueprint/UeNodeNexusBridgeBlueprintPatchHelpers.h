#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

namespace UeNodeNexusBridge
{
UEdGraph* FindBlueprintGraph(UBlueprint* Blueprint, const FString& GraphName);
UEdGraphNode* FindBlueprintNode(UEdGraph* Graph, const FString& NodeId);
UEdGraphPin* FindBlueprintPin(UEdGraphNode* Node, const FString& PinId);
TSharedPtr<FJsonObject> MakeBlueprintLinkJson(UEdGraphPin* FromPin, UEdGraphPin* ToPin);
TArray<TSharedPtr<FJsonValue>> BuildBlueprintNodeParams(UEdGraphNode* Node);
bool ResolveBlueprintLinkPins(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, UEdGraphPin*& OutFrom, UEdGraphPin*& OutTo);
TSharedPtr<FJsonObject> BuildBlueprintPinIntegrity(UEdGraph* Graph);
TArray<TSharedPtr<FJsonValue>> CompileBlueprintWithDiagnostics(UBlueprint* Blueprint, const FString& AssetPath, TSharedPtr<FJsonObject>& OutCompile);
}
