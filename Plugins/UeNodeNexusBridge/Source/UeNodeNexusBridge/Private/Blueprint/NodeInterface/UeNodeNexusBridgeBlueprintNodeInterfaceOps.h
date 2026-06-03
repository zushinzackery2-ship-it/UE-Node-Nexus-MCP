#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleBlueprintNodeInfoGet(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleBlueprintNodePositionGet(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleBlueprintNodePositionSet(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleBlueprintNodeCreate(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload);
UEdGraph* ResolveBlueprintNodeInterfaceGraph(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload);
UEdGraphNode* ResolveBlueprintNodeInterfaceNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Payload);
FString ShortBlueprintNodeClass(UEdGraphNode* Node);
FString BlueprintNodeAlias(UEdGraph* Graph, UEdGraphNode* Target);
int32 BlueprintPinLocalIndex(UEdGraphPin* Pin);
TSharedPtr<FJsonObject> BuildBlueprintNodeInterfaceData(UBlueprint* Blueprint, UEdGraph* Graph, UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Payload, const FString& Format);
struct FBlueprintGraphFilterResult;
TSharedPtr<FJsonObject> BuildBlueprintGraphIndexedData(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Payload, bool bWithPosition, const FBlueprintGraphFilterResult& FilterResult);
}
