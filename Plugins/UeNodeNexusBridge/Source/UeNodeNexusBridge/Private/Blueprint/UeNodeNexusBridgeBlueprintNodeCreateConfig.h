#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UBlueprint;
class UClass;
class UEdGraphNode;

namespace UeNodeNexusBridge
{
bool ValidateBlueprintNodeCreateConfig(UClass* NodeClass, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload, FString& OutError);
bool ConfigureCreatedBlueprintNode(UEdGraphNode* Node, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload, FString& OutError);
}
