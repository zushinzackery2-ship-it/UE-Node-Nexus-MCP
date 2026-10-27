#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

class FJsonObject;
class FJsonValue;
class UEdGraphNode;
class UEdGraphPin;

namespace UeNodeNexusBridge
{
struct FBlueprintGraphFilterResult;

FString PinDirectionToString(EEdGraphPinDirection Direction);
TSharedPtr<FJsonObject> BlueprintNodeToJson(UEdGraphNode* Node, bool bIncludeNodeParams);
void AddBlueprintLinks(
    const UEdGraphNode* Node,
    TArray<TSharedPtr<FJsonValue>>& Links,
    const FBlueprintGraphFilterResult& FilterResult);
}
