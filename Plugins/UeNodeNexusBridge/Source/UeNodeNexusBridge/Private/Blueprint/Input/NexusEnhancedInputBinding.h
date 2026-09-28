#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UK2Node_EnhancedInputAction;

namespace UeNodeNexusBridge
{
bool ConfigureEnhancedInputAction(UK2Node_EnhancedInputAction* Node,
    const TSharedPtr<FJsonObject>& Payload, bool bDryRun, FString& OutError);
}
