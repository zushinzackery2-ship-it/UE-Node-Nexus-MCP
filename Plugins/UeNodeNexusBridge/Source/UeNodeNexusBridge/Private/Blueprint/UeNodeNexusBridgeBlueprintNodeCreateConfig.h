#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UClass;
class UEdGraphNode;

namespace UeNodeNexusBridge
{
bool ValidateBlueprintNodeCreateConfig(UClass* NodeClass, const TSharedPtr<FJsonObject>& Payload, FString& OutError);
bool ConfigureCreatedBlueprintNode(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Payload, FString& OutError);
}
