#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UEdGraph;
class UK2Node_TransitionRuleGetter;

namespace UeNodeNexusBridge
{
bool ConfigureTransitionGetter(UK2Node_TransitionRuleGetter* Node, const UEdGraph* Graph,
    const TSharedPtr<FJsonObject>& Payload, bool bDryRun, FString& OutError);
}
