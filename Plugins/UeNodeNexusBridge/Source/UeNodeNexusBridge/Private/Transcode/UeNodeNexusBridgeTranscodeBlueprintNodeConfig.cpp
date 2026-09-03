#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_InputAction.h"
#include "K2Node_InputAxisEvent.h"
#include "K2Node_InputKey.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_StructOperation.h"
#include "K2Node_Variable.h"

namespace UeNodeNexusBridge::Transcode
{
static int32 CountDataInputs(UEdGraphNode* Node)
{
    int32 Count = 0;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && !Pin->bHidden && Pin->PinName != UEdGraphSchema_K2::PN_Self)
        {
            ++Count;
        }
    }
    return Count;
}

static int32 CountExecOutputs(UEdGraphNode* Node)
{
    int32 Count = 0;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
        {
            ++Count;
        }
    }
    return Count;
}

TSharedPtr<FJsonObject> NodeConfigJson(UBlueprint* Blueprint, UEdGraphNode* Node)
{
    TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();
    UClass* SelfClass = Blueprint ? Blueprint->SkeletonGeneratedClass : nullptr;
    if (UK2Node_CustomEvent* Custom = Cast<UK2Node_CustomEvent>(Node))
    {
        Config->SetStringField(TEXT("event_name"), Custom->CustomFunctionName.ToString());
    }
    else if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
    {
        UClass* Owner = Event->EventReference.GetMemberParentClass(SelfClass);
        Config->SetStringField(TEXT("function_owner"), Owner ? Owner->GetPathName() : FString());
        Config->SetStringField(TEXT("function_name"), Event->EventReference.GetMemberName().ToString());
    }
    else if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
    {
        UClass* Owner = Call->FunctionReference.GetMemberParentClass(SelfClass);
        Config->SetStringField(TEXT("function_owner"), Owner ? Owner->GetPathName() : FString());
        Config->SetStringField(TEXT("function_name"), Call->FunctionReference.GetMemberName().ToString());
        Config->SetStringField(TEXT("self_context"), Call->FunctionReference.IsSelfContext() ? TEXT("true") : TEXT("false"));
        if (Cast<UK2Node_CommutativeAssociativeBinaryOperator>(Node))
        {
            Config->SetStringField(TEXT("dynamic_pins"), FString::FromInt(CountDataInputs(Node)));
        }
    }
    else if (UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node))
    {
        UClass* Owner = Variable->VariableReference.GetMemberParentClass(SelfClass);
        Config->SetStringField(TEXT("variable_name"), Variable->VariableReference.GetMemberName().ToString());
        Config->SetStringField(TEXT("self_context"), Variable->VariableReference.IsSelfContext() ? TEXT("true") : TEXT("false"));
        Config->SetStringField(TEXT("variable_owner"), Owner ? Owner->GetPathName() : FString());
    }
    else if (UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node))
    {
        UEdGraph* MacroGraph = Macro->GetMacroGraph();
        UBlueprint* MacroBlueprint = MacroGraph ? Cast<UBlueprint>(MacroGraph->GetOuter()) : nullptr;
        Config->SetStringField(TEXT("macro_name"), MacroGraph ? MacroGraph->GetName() : FString());
        Config->SetStringField(TEXT("macro_owner"), MacroBlueprint ? MacroBlueprint->GetPathName() : FString());
    }
    else if (UK2Node_DynamicCast* DynamicCast = Cast<UK2Node_DynamicCast>(Node))
    {
        Config->SetStringField(TEXT("target_type"), DynamicCast->TargetType ? DynamicCast->TargetType->GetPathName() : FString());
    }
    else if (UK2Node_StructOperation* StructOperation = Cast<UK2Node_StructOperation>(Node))
    {
        Config->SetStringField(TEXT("struct_type"), StructOperation->StructType ? StructOperation->StructType->GetPathName() : FString());
    }
    else if (UK2Node_InputKey* InputKey = Cast<UK2Node_InputKey>(Node))
    {
        Config->SetStringField(TEXT("input_key"), InputKey->InputKey.ToString());
    }
    else if (UK2Node_InputAction* InputAction = Cast<UK2Node_InputAction>(Node))
    {
        Config->SetStringField(TEXT("input_action_name"), InputAction->InputActionName.ToString());
    }
    else if (UK2Node_InputAxisEvent* InputAxis = Cast<UK2Node_InputAxisEvent>(Node))
    {
        Config->SetStringField(TEXT("input_axis_name"), InputAxis->InputAxisName.ToString());
    }
    else if (Cast<UK2Node_ExecutionSequence>(Node))
    {
        Config->SetStringField(TEXT("dynamic_pins"), FString::FromInt(CountExecOutputs(Node)));
    }
    else if (Cast<UK2Node_MakeArray>(Node))
    {
        Config->SetStringField(TEXT("dynamic_pins"), FString::FromInt(CountDataInputs(Node)));
    }
    else if (Node->GetClass()->GetName() == TEXT("K2Node_EnhancedInputAction"))
    {
        FProperty* Property = Node->GetClass()->FindPropertyByName(TEXT("InputAction"));
        if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        {
            UObject* Action = ObjectProperty->GetObjectPropertyValue_InContainer(Node);
            Config->SetStringField(TEXT("input_action"), Action ? Action->GetPathName() : FString());
        }
    }
    return Config;
}
}
