#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UK2Node_BaseMCDelegate;
class UBlueprint;

namespace UeNodeNexusBridge
{
bool ConfigureDelegateBinding(UK2Node_BaseMCDelegate* Node, UBlueprint* Blueprint,
    const TSharedPtr<FJsonObject>& Payload, bool bDryRun, FString& OutError);
}
