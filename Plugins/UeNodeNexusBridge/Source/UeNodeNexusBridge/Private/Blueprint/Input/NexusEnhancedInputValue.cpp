#include "NexusEnhancedInputValue.h"

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraphNode.h"
#include "EnhancedInputLibrary.h"
#include "InputAction.h"
#include "K2Node_CallFunction.h"
#include "UeNodeNexusBridgeBlueprintNodeCreateFields.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
bool IsEnhancedInputValueClass(const UClass* Class)
{
    return Class && (Class->GetPathName() == TEXT("/Script/InputBlueprintNodes.K2Node_GetInputActionValue")
        || Class->GetPathName() == TEXT("/Script/InputBlueprintNodes.K2Node_InputActionValueAccessor"));
}

bool ConfigureEnhancedInputValue(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Payload,
    bool bDryRun, FString& Error)
{
    FString Path;
    if (!ReadBlueprintNodeStringField(Payload, TEXT("input_action"), Path) || Path.IsEmpty())
    {
        Error = TEXT("input_action requires an InputAction asset path");
        return false;
    }
    auto* Action = Cast<UInputAction>(LoadObject<UObject>(nullptr, *Path));
    auto* Property = CastField<FObjectPropertyBase>(Node->GetClass()->FindPropertyByName(TEXT("InputAction")));
    if (!Action || !IsEnhancedInputValueClass(Node->GetClass()) || !Property)
    {
        Error = TEXT("invalid Enhanced Input current-value binding: ") + Path;
        return false;
    }
    if (!bDryRun)
    {
        // Both native Initialize methods set this structural property before
        // AllocateDefaultPins. The accessor also declares its native function.
        Property->SetObjectPropertyValue_InContainer(Node, Action);
        if (auto* Call = Cast<UK2Node_CallFunction>(Node))
        {
            Call->SetFromFunction(UEnhancedInputLibrary::StaticClass()->FindFunctionByName(
                GET_FUNCTION_NAME_CHECKED(UEnhancedInputLibrary, GetBoundActionValue)));
        }
    }
    return true;
}
}
