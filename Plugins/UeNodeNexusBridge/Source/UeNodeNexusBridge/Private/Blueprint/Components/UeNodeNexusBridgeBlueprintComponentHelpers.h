#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UBlueprint;
class USceneComponent;
class USCS_Node;
class USimpleConstructionScript;

namespace UeNodeNexusBridge
{
USCS_Node* FindBlueprintSCSNode(
    USimpleConstructionScript* Script,
    const FString& Name);
USceneComponent* FindNativeBlueprintSceneComponent(
    UBlueprint* Blueprint,
    const FString& Name);
TSharedPtr<FJsonObject> MakeBlueprintComponentItem(
    USCS_Node* Node,
    const FString& ParentName);
}
