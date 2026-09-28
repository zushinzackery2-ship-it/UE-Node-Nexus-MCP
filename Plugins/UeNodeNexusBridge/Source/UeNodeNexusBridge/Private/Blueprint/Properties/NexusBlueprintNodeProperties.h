#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UEdGraphNode;

namespace UeNodeNexusBridge
{
bool ApplyBlueprintNodeProperty(UEdGraphNode* Node, const FString& Name,
    const TSharedPtr<FJsonValue>& Value, bool bDryRun, bool bNotify,
    FString& Before, FString& After, FString& Error);

bool ConfigureBlueprintNodeProperties(UEdGraphNode* Node,
    const TSharedPtr<FJsonObject>& Payload, bool bDryRun, FString& Error);
}
