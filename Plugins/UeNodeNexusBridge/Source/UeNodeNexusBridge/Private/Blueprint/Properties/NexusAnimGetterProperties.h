#pragma once

#include "CoreMinimal.h"

class FJsonValue;
class FProperty;
class UEdGraphNode;

namespace UeNodeNexusBridge
{
bool IsAnimGetterBindingProperty(const UEdGraphNode* Node, const FProperty* Property);
bool ValidateAnimGetterBinding(const UEdGraphNode* Node, const FProperty* Property,
    const TSharedPtr<FJsonValue>& Value, FString& Error);
}
