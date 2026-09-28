#include "NexusEnhancedInputBinding.h"

#include "InputAction.h"
#include "K2Node_EnhancedInputAction.h"
#include "UeNodeNexusBridgeBlueprintNodeCreateFields.h"

namespace UeNodeNexusBridge
{
bool ConfigureEnhancedInputAction(UK2Node_EnhancedInputAction* Node,
    const TSharedPtr<FJsonObject>& Payload, bool bDryRun, FString& OutError)
{
    FString Path;
    if (!ReadBlueprintNodeStringField(Payload, TEXT("input_action"), Path) || Path.IsEmpty())
    {
        OutError = TEXT("input_action requires an InputAction asset path");
        return false;
    }
    UInputAction* Action = Cast<UInputAction>(LoadObject<UObject>(nullptr, *Path));
    if (Action == nullptr)
    {
        OutError = FString::Printf(TEXT("input_action is not an InputAction: %s"), *Path);
        return false;
    }
    if (!bDryRun)
    {
        Node->InputAction = Action;
    }
    return true;
}
}
