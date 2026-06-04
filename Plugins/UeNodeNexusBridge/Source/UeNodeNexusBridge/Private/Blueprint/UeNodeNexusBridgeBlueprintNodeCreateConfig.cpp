#include "UeNodeNexusBridgeBlueprintNodeCreateConfig.h"

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraphNode.h"
#include "InputCoreTypes.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_InputAction.h"
#include "K2Node_InputAxisEvent.h"
#include "K2Node_InputKey.h"
#include "K2Node_Variable.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
static bool ReadStringFieldOrParam(const TSharedPtr<FJsonObject>& Payload, const FString& FieldName, FString& OutValue)
{
    if (Payload->TryGetStringField(FieldName, OutValue))
    {
        return true;
    }

    const TSharedPtr<FJsonObject>* Params = nullptr;
    return Payload->TryGetObjectField(TEXT("params"), Params) && Params != nullptr && (*Params)->TryGetStringField(FieldName, OutValue);
}

static bool ReadBoolFieldOrParam(const TSharedPtr<FJsonObject>& Payload, const FString& FieldName, bool& OutValue)
{
    if (Payload->TryGetBoolField(FieldName, OutValue))
    {
        return true;
    }

    const TSharedPtr<FJsonObject>* Params = nullptr;
    return Payload->TryGetObjectField(TEXT("params"), Params) && Params != nullptr && (*Params)->TryGetBoolField(FieldName, OutValue);
}

static bool ReadVariableNameFieldOrParam(const TSharedPtr<FJsonObject>& Payload, FString& OutValue)
{
    if (ReadStringFieldOrParam(Payload, TEXT("variable_name"), OutValue)
        || ReadStringFieldOrParam(Payload, TEXT("VariableName"), OutValue)
        || ReadStringFieldOrParam(Payload, TEXT("member_name"), OutValue))
    {
        return !OutValue.IsEmpty();
    }

    const TSharedPtr<FJsonObject>* VariableReference = nullptr;
    if (Payload->TryGetObjectField(TEXT("variable_reference"), VariableReference) && VariableReference != nullptr)
    {
        return (*VariableReference)->TryGetStringField(TEXT("member_name"), OutValue) && !OutValue.IsEmpty();
    }

    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (Payload->TryGetObjectField(TEXT("params"), Params) && Params != nullptr)
    {
        if ((*Params)->TryGetObjectField(TEXT("variable_reference"), VariableReference) && VariableReference != nullptr)
        {
            return (*VariableReference)->TryGetStringField(TEXT("member_name"), OutValue) && !OutValue.IsEmpty();
        }
    }
    return false;
}

static USCS_Node* FindComponentNodeByVariableName(UBlueprint* Blueprint, const FName VariableName)
{
    USimpleConstructionScript* Script = Blueprint ? Blueprint->SimpleConstructionScript : nullptr;
    if (Script == nullptr || VariableName.IsNone())
    {
        return nullptr;
    }
    if (USCS_Node* ExactNode = Script->FindSCSNode(VariableName))
    {
        return ExactNode;
    }
    for (USCS_Node* Node : Script->GetAllNodes())
    {
        if (Node != nullptr && Node->GetVariableName().IsEqual(VariableName, ENameCase::IgnoreCase))
        {
            return Node;
        }
    }
    return nullptr;
}

static FProperty* FindBlueprintProperty(UBlueprint* Blueprint, const FName VariableName)
{
    if (Blueprint == nullptr || VariableName.IsNone())
    {
        return nullptr;
    }
    if (UClass* SkeletonClass = Blueprint->SkeletonGeneratedClass)
    {
        if (FProperty* Property = FindFProperty<FProperty>(SkeletonClass, VariableName))
        {
            return Property;
        }
    }
    if (UClass* GeneratedClass = Blueprint->GeneratedClass)
    {
        if (FProperty* Property = FindFProperty<FProperty>(GeneratedClass, VariableName))
        {
            return Property;
        }
    }
    return nullptr;
}

static bool BlueprintVariableExists(UBlueprint* Blueprint, const FString& VariableName)
{
    const FName VariableFName(*VariableName);
    return FindBlueprintProperty(Blueprint, VariableFName) != nullptr || FindComponentNodeByVariableName(Blueprint, VariableFName) != nullptr;
}

static bool ConfigureVariableNode(UK2Node_Variable* Node, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload, FString& OutError)
{
    FString VariableName;
    if (!ReadVariableNameFieldOrParam(Payload, VariableName))
    {
        OutError = TEXT("variable_name is required for K2Node_VariableGet/K2Node_VariableSet");
        return false;
    }

    const FName VariableFName(*VariableName);
    if (FProperty* Property = FindBlueprintProperty(Blueprint, VariableFName))
    {
        Node->SetFromProperty(Property, true, Property->GetOwnerClass());
        return true;
    }

    if (USCS_Node* ComponentNode = FindComponentNodeByVariableName(Blueprint, VariableFName))
    {
        Node->VariableReference.SetSelfMember(ComponentNode->GetVariableName(), ComponentNode->VariableGuid);
        return true;
    }

    OutError = FString::Printf(TEXT("variable_name not found on Blueprint variables or components: %s"), *VariableName);
    return false;
}

static bool ConfigureCallFunctionNode(UK2Node_CallFunction* Node, const TSharedPtr<FJsonObject>& Payload, FString& OutError)
{
    FString FunctionName;
    FString FunctionOwner;
    if (!ReadStringFieldOrParam(Payload, TEXT("function_name"), FunctionName) || !ReadStringFieldOrParam(Payload, TEXT("function_owner"), FunctionOwner))
    {
        OutError = TEXT("function_name and function_owner are required for K2Node_CallFunction");
        return false;
    }

    UClass* OwnerClass = LoadClass<UObject>(nullptr, *FunctionOwner);
    if (OwnerClass == nullptr)
    {
        OutError = FString::Printf(TEXT("Function owner class not found: %s"), *FunctionOwner);
        return false;
    }

    UFunction* Function = OwnerClass->FindFunctionByName(FName(*FunctionName));
    if (Function == nullptr)
    {
        OutError = FString::Printf(TEXT("Function not found: %s.%s"), *OwnerClass->GetName(), *FunctionName);
        return false;
    }

    Node->SetFromFunction(Function);
    return true;
}

static bool ConfigureGenericEventNode(UK2Node_Event* Node, const TSharedPtr<FJsonObject>& Payload, FString& OutError)
{
    if (Node->GetClass() != UK2Node_Event::StaticClass())
    {
        return true;
    }

    FString FunctionName;
    FString FunctionOwner;
    if (!ReadStringFieldOrParam(Payload, TEXT("function_name"), FunctionName) || !ReadStringFieldOrParam(Payload, TEXT("function_owner"), FunctionOwner))
    {
        OutError = TEXT("function_name and function_owner are required for K2Node_Event; use K2Node_CustomEvent with event_name for custom events");
        return false;
    }

    UClass* OwnerClass = LoadClass<UObject>(nullptr, *FunctionOwner);
    if (OwnerClass == nullptr)
    {
        OutError = FString::Printf(TEXT("Event owner class not found: %s"), *FunctionOwner);
        return false;
    }

    UFunction* Function = OwnerClass->FindFunctionByName(FName(*FunctionName));
    if (Function == nullptr)
    {
        OutError = FString::Printf(TEXT("Event function not found: %s.%s"), *OwnerClass->GetName(), *FunctionName);
        return false;
    }

    Node->EventReference.SetFromField<UFunction>(Function, false);
    Node->bOverrideFunction = true;
    return true;
}

static bool ConfigureInputKeyNode(UK2Node_InputKey* Node, const TSharedPtr<FJsonObject>& Payload, FString& OutError)
{
    FString KeyName;
    if (!ReadStringFieldOrParam(Payload, TEXT("input_key"), KeyName))
    {
        OutError = TEXT("input_key is required for K2Node_InputKey");
        return false;
    }

    Node->InputKey = FKey(FName(*KeyName));
    bool bConsumeInput = Node->bConsumeInput;
    bool bExecuteWhenPaused = Node->bExecuteWhenPaused;
    bool bOverrideParentBinding = Node->bOverrideParentBinding;
    bool bControl = Node->bControl;
    bool bAlt = Node->bAlt;
    bool bShift = Node->bShift;
    bool bCommand = Node->bCommand;
    ReadBoolFieldOrParam(Payload, TEXT("consume_input"), bConsumeInput);
    ReadBoolFieldOrParam(Payload, TEXT("execute_when_paused"), bExecuteWhenPaused);
    ReadBoolFieldOrParam(Payload, TEXT("override_parent_binding"), bOverrideParentBinding);
    ReadBoolFieldOrParam(Payload, TEXT("control"), bControl);
    ReadBoolFieldOrParam(Payload, TEXT("alt"), bAlt);
    ReadBoolFieldOrParam(Payload, TEXT("shift"), bShift);
    ReadBoolFieldOrParam(Payload, TEXT("command"), bCommand);
    Node->bConsumeInput = bConsumeInput;
    Node->bExecuteWhenPaused = bExecuteWhenPaused;
    Node->bOverrideParentBinding = bOverrideParentBinding;
    Node->bControl = bControl;
    Node->bAlt = bAlt;
    Node->bShift = bShift;
    Node->bCommand = bCommand;
    return true;
}

static bool ConfigureInputAxisEventNode(UK2Node_InputAxisEvent* Node, const TSharedPtr<FJsonObject>& Payload, FString& OutError)
{
    FString AxisName;
    if (!ReadStringFieldOrParam(Payload, TEXT("axis_name"), AxisName))
    {
        OutError = TEXT("axis_name is required for K2Node_InputAxisEvent");
        return false;
    }

    Node->Initialize(FName(*AxisName));
    bool bConsumeInput = Node->bConsumeInput;
    bool bExecuteWhenPaused = Node->bExecuteWhenPaused;
    bool bOverrideParentBinding = Node->bOverrideParentBinding;
    ReadBoolFieldOrParam(Payload, TEXT("consume_input"), bConsumeInput);
    ReadBoolFieldOrParam(Payload, TEXT("execute_when_paused"), bExecuteWhenPaused);
    ReadBoolFieldOrParam(Payload, TEXT("override_parent_binding"), bOverrideParentBinding);
    Node->bConsumeInput = bConsumeInput;
    Node->bExecuteWhenPaused = bExecuteWhenPaused;
    Node->bOverrideParentBinding = bOverrideParentBinding;
    return true;
}

static bool ConfigureInputActionNode(UK2Node_InputAction* Node, const TSharedPtr<FJsonObject>& Payload, FString& OutError)
{
    FString ActionName;
    if (!ReadStringFieldOrParam(Payload, TEXT("input_action_name"), ActionName))
    {
        OutError = TEXT("input_action_name is required for K2Node_InputAction");
        return false;
    }

    Node->InputActionName = FName(*ActionName);
    bool bConsumeInput = Node->bConsumeInput;
    bool bExecuteWhenPaused = Node->bExecuteWhenPaused;
    bool bOverrideParentBinding = Node->bOverrideParentBinding;
    ReadBoolFieldOrParam(Payload, TEXT("consume_input"), bConsumeInput);
    ReadBoolFieldOrParam(Payload, TEXT("execute_when_paused"), bExecuteWhenPaused);
    ReadBoolFieldOrParam(Payload, TEXT("override_parent_binding"), bOverrideParentBinding);
    Node->bConsumeInput = bConsumeInput;
    Node->bExecuteWhenPaused = bExecuteWhenPaused;
    Node->bOverrideParentBinding = bOverrideParentBinding;
    return true;
}

static bool ConfigureCustomEventNode(UK2Node_CustomEvent* Node, const TSharedPtr<FJsonObject>& Payload)
{
    FString EventName;
    if (!ReadStringFieldOrParam(Payload, TEXT("event_name"), EventName))
    {
        ReadStringFieldOrParam(Payload, TEXT("name"), EventName);
    }
    if (!EventName.IsEmpty())
    {
        Node->CustomFunctionName = FName(*EventName);
    }
    return true;
}

bool ValidateBlueprintNodeCreateConfig(UClass* NodeClass, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload, FString& OutError)
{
    if (NodeClass->IsChildOf(UK2Node_Variable::StaticClass()))
    {
        FString VariableName;
        if (!ReadVariableNameFieldOrParam(Payload, VariableName))
        {
            OutError = TEXT("variable_name is required for K2Node_VariableGet/K2Node_VariableSet");
            return false;
        }
        if (!BlueprintVariableExists(Blueprint, VariableName))
        {
            OutError = FString::Printf(TEXT("variable_name not found on Blueprint variables or components: %s"), *VariableName);
            return false;
        }
    }
    if (NodeClass->IsChildOf(UK2Node_CallFunction::StaticClass()))
    {
        FString FunctionName;
        FString FunctionOwner;
        if (!ReadStringFieldOrParam(Payload, TEXT("function_name"), FunctionName) || !ReadStringFieldOrParam(Payload, TEXT("function_owner"), FunctionOwner))
        {
            OutError = TEXT("function_name and function_owner are required for K2Node_CallFunction");
            return false;
        }
    }
    if (NodeClass->IsChildOf(UK2Node_InputKey::StaticClass()))
    {
        FString KeyName;
        if (!ReadStringFieldOrParam(Payload, TEXT("input_key"), KeyName))
        {
            OutError = TEXT("input_key is required for K2Node_InputKey");
            return false;
        }
    }
    if (NodeClass->IsChildOf(UK2Node_InputAction::StaticClass()))
    {
        FString ActionName;
        if (!ReadStringFieldOrParam(Payload, TEXT("input_action_name"), ActionName))
        {
            OutError = TEXT("input_action_name is required for K2Node_InputAction");
            return false;
        }
    }
    if (NodeClass->IsChildOf(UK2Node_InputAxisEvent::StaticClass()))
    {
        FString AxisName;
        if (!ReadStringFieldOrParam(Payload, TEXT("axis_name"), AxisName))
        {
            OutError = TEXT("axis_name is required for K2Node_InputAxisEvent");
            return false;
        }
    }
    if (NodeClass == UK2Node_Event::StaticClass())
    {
        FString FunctionName;
        FString FunctionOwner;
        if (!ReadStringFieldOrParam(Payload, TEXT("function_name"), FunctionName) || !ReadStringFieldOrParam(Payload, TEXT("function_owner"), FunctionOwner))
        {
            OutError = TEXT("function_name and function_owner are required for K2Node_Event; use K2Node_CustomEvent with event_name for custom events");
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
            OutError = FString::Printf(TEXT("Event function not found: %s.%s"), *OwnerClass->GetName(), *FunctionName);
            return false;
        }
    }
    return true;
}

bool ConfigureCreatedBlueprintNode(UEdGraphNode* Node, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload, FString& OutError)
{
    if (UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node))
    {
        return ConfigureVariableNode(VariableNode, Blueprint, Payload, OutError);
    }
    if (UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
    {
        return ConfigureCallFunctionNode(CallFunctionNode, Payload, OutError);
    }
    if (UK2Node_InputKey* InputKeyNode = Cast<UK2Node_InputKey>(Node))
    {
        return ConfigureInputKeyNode(InputKeyNode, Payload, OutError);
    }
    if (UK2Node_InputAction* InputActionNode = Cast<UK2Node_InputAction>(Node))
    {
        return ConfigureInputActionNode(InputActionNode, Payload, OutError);
    }
    if (UK2Node_InputAxisEvent* InputAxisEventNode = Cast<UK2Node_InputAxisEvent>(Node))
    {
        return ConfigureInputAxisEventNode(InputAxisEventNode, Payload, OutError);
    }
    if (UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(Node))
    {
        return ConfigureCustomEventNode(CustomEventNode, Payload);
    }
    if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
    {
        return ConfigureGenericEventNode(EventNode, Payload, OutError);
    }
    return true;
}
}
