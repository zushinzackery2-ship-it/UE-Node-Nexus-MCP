#pragma once

#include "CoreMinimal.h"

class UActorComponent;
class UBlueprint;
class USCS_Node;

namespace UeNodeNexusBridge::Transcode
{
USCS_Node* FindInheritedComponentNode(UBlueprint* Blueprint, FName Name);
UActorComponent* ResolveInheritedComponentTemplate(UBlueprint* Blueprint, FName Name, bool bCreateOverride = false);
}
