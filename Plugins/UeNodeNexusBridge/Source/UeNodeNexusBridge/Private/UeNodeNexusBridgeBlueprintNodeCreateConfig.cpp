#include "UeNodeNexusBridgeBlueprintNodeCreateConfig.h"

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraphNode.h"
#include "InputCoreTypes.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_InputAxisEvent.h"
#include "K2Node_InputKey.h"

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

bool ConfigureCreatedBlueprintNode(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Payload, FString& OutError)
{
    if (UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
    {
        return ConfigureCallFunctionNode(CallFunctionNode, Payload, OutError);
    }
    if (UK2Node_InputKey* InputKeyNode = Cast<UK2Node_InputKey>(Node))
    {
        return ConfigureInputKeyNode(InputKeyNode, Payload, OutError);
    }
    if (UK2Node_InputAxisEvent* InputAxisEventNode = Cast<UK2Node_InputAxisEvent>(Node))
    {
        return ConfigureInputAxisEventNode(InputAxisEventNode, Payload, OutError);
    }
    if (UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(Node))
    {
        return ConfigureCustomEventNode(CustomEventNode, Payload);
    }
    return true;
}
}
