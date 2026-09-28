#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;

namespace UeNodeNexusBridge
{
FString BlueprintGraphSelector(const UEdGraph* Graph);
UEdGraph* ResolveBlueprintGraphSelector(UBlueprint* Blueprint, const FString& Selector);
}
