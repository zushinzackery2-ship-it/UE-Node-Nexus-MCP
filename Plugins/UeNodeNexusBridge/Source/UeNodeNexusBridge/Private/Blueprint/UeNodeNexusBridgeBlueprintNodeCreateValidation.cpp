#include "UeNodeNexusBridgeBlueprintNodeCreateConfig.h"

#include "Blueprint/CallHost/NexusCallHostClass.h"
#include "Dom/JsonObject.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_InputAction.h"
#include "K2Node_InputAxisEvent.h"
#include "K2Node_InputKey.h"
#include "K2Node_Variable.h"
#include "UeNodeNexusBridgeBlueprintNodeCreateFields.h"

namespace UeNodeNexusBridge
{
bool ValidateBlueprintNodeCreateConfig(
    UClass*& NodeClass,
    UBlueprint* Blueprint,
    const TSharedPtr<FJsonObject>& Payload,
    FString& OutError)
{
    if (NodeClass->IsChildOf(UK2Node_Variable::StaticClass()))
    {
        FString VariableName;
        if (!ReadBlueprintVariableName(Payload, VariableName))
        {
            OutError = TEXT("variable_name is required for K2Node_VariableGet/K2Node_VariableSet");
            return false;
        }
        if (!BlueprintVariableExists(Blueprint, VariableName))
        {
            OutError = FString::Printf(
                TEXT("variable_name not found on Blueprint variables or components: %s"),
                *VariableName);
            return false;
        }
    }
    if (NodeClass->IsChildOf(UK2Node_CallFunction::StaticClass()))
    {
        FString FunctionName;
        FString FunctionOwner;
        if (!ReadBlueprintNodeStringField(Payload, TEXT("function_name"), FunctionName)
            || !ReadBlueprintNodeStringField(Payload, TEXT("function_owner"), FunctionOwner))
        {
            OutError = TEXT("function_name and function_owner are required for K2Node_CallFunction");
            return false;
        }
        // Chosen before anything is created, so a dry run refuses what the
        // real call would, and a plain request becomes the class UE spawns.
        if (!SelectCallHostClass(NodeClass, FindCallFunction(FunctionOwner, FunctionName), OutError))
        {
            return false;
        }
    }
    if (NodeClass->IsChildOf(UK2Node_InputKey::StaticClass()))
    {
        FString KeyName;
        if (!ReadBlueprintNodeStringField(Payload, TEXT("input_key"), KeyName))
        {
            OutError = TEXT("input_key is required for K2Node_InputKey");
            return false;
        }
    }
    if (NodeClass->IsChildOf(UK2Node_InputAction::StaticClass()))
    {
        FString ActionName;
        if (!ReadBlueprintNodeStringField(Payload, TEXT("input_action_name"), ActionName))
        {
            OutError = TEXT("input_action_name is required for K2Node_InputAction");
            return false;
        }
    }
    if (NodeClass->IsChildOf(UK2Node_InputAxisEvent::StaticClass()))
    {
        FString AxisName;
        if (!ReadBlueprintNodeStringField(Payload, TEXT("axis_name"), AxisName))
        {
            OutError = TEXT("axis_name is required for K2Node_InputAxisEvent");
            return false;
        }
    }
    if (NodeClass == UK2Node_Event::StaticClass())
    {
        FString FunctionName;
        FString FunctionOwner;
        if (!ReadBlueprintNodeStringField(Payload, TEXT("function_name"), FunctionName)
            || !ReadBlueprintNodeStringField(Payload, TEXT("function_owner"), FunctionOwner))
        {
            OutError = TEXT(
                "function_name and function_owner are required for K2Node_Event; "
                "use K2Node_CustomEvent with event_name for custom events");
            return false;
        }

        UClass* OwnerClass = LoadClass<UObject>(nullptr, *FunctionOwner);
        if (OwnerClass == nullptr)
        {
            OutError = FString::Printf(TEXT("Event owner class not found: %s"), *FunctionOwner);
            return false;
        }

        if (OwnerClass->FindFunctionByName(FName(*FunctionName)) == nullptr)
        {
            OutError = FString::Printf(
                TEXT("Event function not found: %s.%s"),
                *OwnerClass->GetName(),
                *FunctionName);
            return false;
        }
    }
    return true;
}
}
