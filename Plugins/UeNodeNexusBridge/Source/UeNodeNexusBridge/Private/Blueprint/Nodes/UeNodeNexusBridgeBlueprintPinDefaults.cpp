#include "UeNodeNexusBridgeBlueprintPinDefaults.h"

#include "EdGraph/EdGraphPin.h"

namespace UeNodeNexusBridge
{
FString BlueprintPinDefaultText(const UEdGraphPin* Pin)
{
    if (Pin == nullptr)
    {
        return FString();
    }
    if (Pin->DefaultObject != nullptr)
    {
        return Pin->DefaultObject->GetPathName();
    }
    if (!Pin->DefaultTextValue.IsEmpty())
    {
        return Pin->DefaultTextValue.ToString();
    }
    return Pin->DefaultValue;
}
}
