#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UEdGraphNode;

namespace UeNodeNexusBridge
{
bool ConfigureCreatedBlueprintNode(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Payload, FString& OutError);
}
