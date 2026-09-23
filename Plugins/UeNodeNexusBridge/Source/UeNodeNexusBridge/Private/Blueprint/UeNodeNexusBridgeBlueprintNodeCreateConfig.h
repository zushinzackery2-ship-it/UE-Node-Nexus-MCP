#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UBlueprint;
class UClass;
class UEdGraphNode;

namespace UeNodeNexusBridge
{
// May replace ``NodeClass`` with the call class the function requires.
bool ValidateBlueprintNodeCreateConfig(UClass*& NodeClass, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload, FString& OutError);
bool ConfigureCreatedBlueprintNode(UEdGraphNode* Node, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload, FString& OutError);
UClass* ResolveBlueprintNodeClassForCreate(const FString& NodeClass);
}
