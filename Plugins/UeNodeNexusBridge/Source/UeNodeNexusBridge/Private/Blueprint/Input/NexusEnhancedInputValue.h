#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UEdGraphNode;

namespace UeNodeNexusBridge
{
bool IsEnhancedInputValueClass(const UClass* Class);
bool ConfigureEnhancedInputValue(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Payload,
    bool bDryRun, FString& Error);
}
