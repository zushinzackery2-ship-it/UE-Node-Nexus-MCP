#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

class FJsonObject;
class FJsonValue;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;

namespace UeNodeNexusBridge
{
struct FBlueprintPatchContext
{
    TMap<FString, UEdGraphNode*> ClientNodes;
    TArray<UEdGraphNode*> DryRunNodes;
};

FString ReadBlueprintPatchNodeRef(
    const TSharedPtr<FJsonObject>& Operation,
    const TCHAR* CanonicalField,
    const TCHAR* AliasField);
UEdGraphNode* ResolveBlueprintPatchNode(
    UEdGraph* Graph,
    const FString& NodeId,
    const FBlueprintPatchContext& Context);
UEdGraphPin* ResolveBlueprintPatchPinByIdOrName(
    UEdGraphNode* Node,
    const TSharedPtr<FJsonObject>& Operation,
    const TCHAR* IdField,
    const TCHAR* NameField,
    EEdGraphPinDirection Direction);
bool ResolveBlueprintPatchLinkPins(
    UEdGraph* Graph,
    const TSharedPtr<FJsonObject>& Operation,
    const FBlueprintPatchContext& Context,
    UEdGraphPin*& OutFrom,
    UEdGraphPin*& OutTo,
    TArray<TSharedPtr<FJsonValue>>& Diagnostics,
    UBlueprint* Blueprint);
UEdGraphNode* ResolveBlueprintPatchOpNode(
    UEdGraph* Graph,
    const TSharedPtr<FJsonObject>& Operation,
    const FBlueprintPatchContext& Context,
    FString& OutNodeId);
}
