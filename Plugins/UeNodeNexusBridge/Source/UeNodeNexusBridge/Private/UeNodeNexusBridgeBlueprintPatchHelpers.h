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
void AppendDiffItem(TSharedPtr<FJsonObject> Diff, const FString& Field, const TSharedPtr<FJsonObject>& Item);
void AddGraphParamChange(TSharedPtr<FJsonObject> Diff, const FString& NodeId, const FString& Name, const FString& Before, const FString& After);
bool ReadGraphPosition(const TSharedPtr<FJsonObject>& Json, int32& OutX, int32& OutY);
bool ReadJsonScalarAsString(const TSharedPtr<FJsonObject>& Json, const FString& Field, FString& OutValue);
bool ResolveBlueprintLinkPins(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, UEdGraphPin*& OutFrom, UEdGraphPin*& OutTo);
TSharedPtr<FJsonObject> BuildBlueprintPinIntegrity(UEdGraph* Graph);
TArray<TSharedPtr<FJsonValue>> CompileBlueprintWithDiagnostics(UBlueprint* Blueprint, const FString& AssetPath, TSharedPtr<FJsonObject>& OutCompile);
}
