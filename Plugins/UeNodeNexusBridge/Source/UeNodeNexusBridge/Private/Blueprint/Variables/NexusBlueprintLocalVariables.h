#pragma once

#include "CoreMinimal.h"

class UEdGraph;
struct FBPVariableDescription;

namespace UeNodeNexusBridge
{
const FBPVariableDescription* FindBlueprintLocalVariable(UEdGraph* Graph, FName Name);
}
