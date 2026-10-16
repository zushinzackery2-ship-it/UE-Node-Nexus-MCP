#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"

#include "Blueprint/CallHost/NexusCallHostClass.h"
#include "Blueprint/Creation/NexusBlueprintNodeDefaults.h"
#include "Blueprint/Input/NexusEnhancedInputValue.h"
#include "Blueprint/Delegates/NexusDelegateBinding.h"
#include "Blueprint/Delegates/NexusDelegatePlanBinding.h"
#include "Blueprint/Members/NexusCustomEventPins.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_StructOperation.h"
#include "K2Node_Variable.h"
#include "UeNodeNexusBridgeBlueprintNodeCreateConfig.h"

namespace UeNodeNexusBridge::Transcode
{
namespace
{
UEdGraph* FindMacroGraph(const FString& Reference)
{
    FString Owner;
    FString Name;
    if (!Reference.Split(TEXT("."), &Owner, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
    {
        Owner = TEXT("StandardMacros");
        Name = Reference;
    }
    const FString OwnerPath = Owner.StartsWith(TEXT("/")) ? Owner : FString::Printf(TEXT("/Engine/EditorBlueprintResources/%s.%s"), *Owner, *Owner);
    UBlueprint* MacroBlueprint = LoadObject<UBlueprint>(nullptr, *OwnerPath);
    if (MacroBlueprint == nullptr)
    {
        return nullptr;
    }
    for (UEdGraph* Graph : MacroBlueprint->MacroGraphs)
    {
        if (Graph && Graph->GetName().Equals(Name, ESearchCase::IgnoreCase))
        {
            return Graph;
        }
    }
    return nullptr;
}

bool ConfigureSpecialNode(UBlueprint* Blueprint, UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Config, FString& OutError)
{
    if (UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node))
    {
        UEdGraph* Graph = FindMacroGraph(ReadOpString(Config, TEXT("macro")));
        if (Graph == nullptr)
        {
            OutError = FString::Printf(TEXT("macro graph not found: %s"), *ReadOpString(Config, TEXT("macro")));
            return false;
        }
        Macro->SetMacroGraph(Graph);
        return true;
    }
    if (UK2Node_DynamicCast* DynamicCast = Cast<UK2Node_DynamicCast>(Node))
    {
        UClass* Target = ResolveClassByNameOrPath(ReadOpString(Config, TEXT("target_type")));
        if (Target == nullptr)
        {
            OutError = FString::Printf(TEXT("cast target class not found: %s"), *ReadOpString(Config, TEXT("target_type")));
            return false;
        }
        DynamicCast->TargetType = Target;
        return true;
    }
    if (UK2Node_StructOperation* StructOperation = Cast<UK2Node_StructOperation>(Node))
    {
        UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *ReadOpString(Config, TEXT("struct_type")));
        if (Struct == nullptr)
        {
            OutError = FString::Printf(TEXT("struct not found: %s"), *ReadOpString(Config, TEXT("struct_type")));
            return false;
        }
        StructOperation->StructType = Struct;
        return true;
    }
    if (UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node); Variable && Config->HasField(TEXT("variable_owner")))
    {
        UClass* Owner = ResolveClassByNameOrPath(ReadOpString(Config, TEXT("variable_owner")));
        if (Owner == nullptr)
        {
            OutError = FString::Printf(TEXT("variable owner class not found: %s"), *ReadOpString(Config, TEXT("variable_owner")));
            return false;
        }
        Variable->VariableReference.SetExternalMember(FName(*ReadOpString(Config, TEXT("variable_name"))), Owner);
        return true;
    }
    return ConfigureCreatedBlueprintNode(Node, Blueprint, Config, OutError);
}

// Actor templates ship disabled ghost events (BeginPlay/Tick/...). Declaring such an
// event in text adopts the ghost, like the editor does, instead of duplicating it.
UK2Node_Event* FindGhostEvent(UEdGraph* Graph, UClass* NodeClass, const TSharedPtr<FJsonObject>& Config)
{
    if (NodeClass != UK2Node_Event::StaticClass())
    {
        return nullptr;
    }
    const FName FunctionName(*ReadOpString(Config, TEXT("function_name")));
    for (UEdGraphNode* Existing : Graph->Nodes)
    {
        UK2Node_Event* Event = Cast<UK2Node_Event>(Existing);
        if (Event != nullptr && Event->GetClass() == UK2Node_Event::StaticClass() && Event->bOverrideFunction
            && Event->EventReference.GetMemberName() == FunctionName && !Event->IsNodeEnabled())
        {
            return Event;
        }
    }
    return nullptr;
}

}

void ApplyBlueprintCreateNode(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, int32 Index, FApplyContext& Context)
{
    const FString Id = ReadOpString(Op, TEXT("id"));
    UClass* NodeClass = ResolveBlueprintNodeClassForCreate(ReadOpString(Op, TEXT("class")));
    if (NodeClass == nullptr || !NodeClass->IsChildOf(UEdGraphNode::StaticClass()))
    {
        Context.Fail(Index, TEXT("unknown_class"), FString::Printf(TEXT("unknown Blueprint node class: %s"), *ReadOpString(Op, TEXT("class"))));
        return;
    }
    if (NodeClass->IsChildOf(UK2Node_FunctionEntry::StaticClass()) || NodeClass->IsChildOf(UK2Node_FunctionResult::StaticClass()))
    {
        Context.Fail(Index, TEXT("implicit_function_node"), TEXT("use the function signature and implicit entry/result links"));
        return;
    }
    const TArray<FString> Positional = ReadOpStrings(Op, TEXT("positional"));
    TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();
    FString Error;
    if (!ConfigureFromPositional(Blueprint, NodeClass, Positional, Config, Error))
    {
        Context.Fail(Index, TEXT("invalid_positional"), Error);
        return;
    }
    // ``CallFunction`` names a call, not a host class: the function decides it,
    // exactly as the editor's spawner does. Deciding here, before the dry-run
    // return, makes the preview refuse what the real apply would.
    const UFunction* Function = FindCallFunction(ReadOpString(Config, TEXT("function_owner")), ReadOpString(Config, TEXT("function_name")), Blueprint);
    if (!SelectCallHostClass(NodeClass, Function, Error))
    {
        Context.Fail(Index, TEXT("node_class_mismatch"), FString::Printf(TEXT("%s: %s"), *Id, *Error));
        return;
    }
    if (IsEnhancedInputValueClass(NodeClass))
    {
        FString Action = ReadOpString(Config, TEXT("input_action"));
        Op->TryGetStringField(TEXT("input_action"), Action);
        const TSharedPtr<FJsonObject>* Arguments = nullptr;
        if (Action.IsEmpty() && Op->TryGetObjectField(TEXT("params"), Arguments))
        {
            (*Arguments)->TryGetStringField(TEXT("Action"), Action);
        }
        Config->SetStringField(TEXT("input_action"), Action);
        if (!ConfigureEnhancedInputValue(CastChecked<UEdGraphNode>(NodeClass->GetDefaultObject()), Config, true, Error))
        {
            Context.Fail(Index, TEXT("node_config_failed"), Id + TEXT(": ") + Error);
            return;
        }
    }
    if (NodeClass->IsChildOf(UK2Node_BaseMCDelegate::StaticClass())
        && !ValidateDelegatePlanBinding(Blueprint, NodeClass, Config, Context, Error))
    {
        Context.Fail(Index, TEXT("node_config_failed"), Id + TEXT(": ") + Error);
        return;
    }
    if (Context.bDryRun)
    {
        Context.Ids.Add(Id, TEXT("dry-run"));
        return;
    }
    int32 X = 0;
    int32 Y = 0;
    Op->TryGetNumberField(TEXT("x"), X);
    Op->TryGetNumberField(TEXT("y"), Y);
    Graph->Modify();
    UEdGraphNode* Node = FindGhostEvent(Graph, NodeClass, Config);
    if (Node != nullptr)
    {
        Node->Modify();
        Node->SetEnabledState(ENodeEnabledState::Enabled);
        Node->NodeComment.Reset();
        Node->bCommentBubbleVisible = false;
        Node->NodePosX = X;
        Node->NodePosY = Y;
    }
    else
    {
        FGraphNodeCreator<UEdGraphNode> Creator(*Graph);
        Node = Creator.CreateNode(false, NodeClass);
        Node->NodePosX = X;
        Node->NodePosY = Y;
        if (!ConfigureSpecialNode(Blueprint, Node, Config, Error))
        {
            Creator.Finalize();
            Graph->RemoveNode(Node);
            Context.Fail(Index, TEXT("node_config_failed"), FString::Printf(TEXT("%s: %s"), *Id, *Error));
            return;
        }
        Creator.Finalize();
    }
    InitializeBlueprintNodeDefaults(Node);
    if (UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node))
    {
        if (!ConfigureCustomEventPins(Event, Op, Error))
        {
            Context.Fail(Index, TEXT("invalid_event_signature"), Error);
        }
    }
    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (Op->TryGetObjectField(TEXT("params"), Params) && Params != nullptr)
    {
        FString Pins;
        if ((*Params)->TryGetStringField(TEXT("pins"), Pins) && !SetDynamicPins(Node, FCString::Atoi(*Pins), Error))
        {
            Context.Fail(Index, TEXT("pins_failed"), Error);
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Params)->Values)
        {
            if (Pair.Key != TEXT("pins") && !SetNodeParam(Blueprint, Graph, Node, Pair.Key, Pair.Value, Error))
            {
                Context.Fail(Index, TEXT("param_failed"), FString::Printf(TEXT("%s: %s"), *Id, *Error));
            }
        }
    }
    bool bEnabled = true;
    if (Op->TryGetBoolField(TEXT("enabled"), bEnabled) && !bEnabled)
    {
        Node->SetEnabledState(ENodeEnabledState::Disabled);
    }
    FString Comment;
    if (Op->TryGetStringField(TEXT("comment"), Comment) && !Comment.IsEmpty())
    {
        Node->NodeComment = Comment;
        Node->bCommentBubbleVisible = true;
    }
    const FString Guid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    Context.Ids.Add(Id, Guid);
    Context.Created.Add(Id, Guid);
    Context.bChanged = true;
}
}
