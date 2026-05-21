#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UMaterial;
class UMaterialExpression;
class UMaterialFunction;

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleMaterialNodeInfoGet(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleMaterialNodePositionGet(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleMaterialNodePositionSet(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload, bool bOffset);
TSharedPtr<FJsonObject> HandleMaterialNodeCreate(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleMaterialFunctionNodeInfoGet(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleMaterialFunctionNodePositionGet(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleMaterialFunctionNodePositionSet(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload, bool bOffset);
TSharedPtr<FJsonObject> HandleMaterialFunctionNodeCreate(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> BuildMaterialNodeInterfaceData(UMaterial* Material, UMaterialExpression* Expression, const TSharedPtr<FJsonObject>& Payload, const FString& Prefix);
TSharedPtr<FJsonObject> BuildMaterialFunctionNodeInterfaceData(UMaterialFunction* Function, UMaterialExpression* Expression, const TSharedPtr<FJsonObject>& Payload, const FString& Prefix);
TSharedPtr<FJsonObject> BuildMaterialGraphIndexedData(UMaterial* Material, const TSharedPtr<FJsonObject>& Payload, bool bWithPosition);
TSharedPtr<FJsonObject> BuildMaterialFunctionGraphIndexedData(UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload, bool bWithPosition);
TSharedPtr<FJsonObject> HandleBlueprintNodeInfoGet(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleBlueprintNodePositionGet(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleBlueprintNodePositionSet(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload, bool bOffset);
TSharedPtr<FJsonObject> HandleBlueprintNodeCreate(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload);
UEdGraph* ResolveBlueprintNodeInterfaceGraph(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload);
UEdGraphNode* ResolveBlueprintNodeInterfaceNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Payload);
FString ShortBlueprintNodeClass(UEdGraphNode* Node);
FString BlueprintNodeAlias(UEdGraph* Graph, UEdGraphNode* Target);
int32 BlueprintPinLocalIndex(UEdGraphPin* Pin);
TSharedPtr<FJsonObject> BuildBlueprintNodeInterfaceData(UBlueprint* Blueprint, UEdGraph* Graph, UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Payload, const FString& Format);
TSharedPtr<FJsonObject> BuildBlueprintGraphIndexedData(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Payload, bool bWithPosition);
}
