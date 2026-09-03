#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
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
    if (Node->GetClass()->GetName() == TEXT("K2Node_EnhancedInputAction"))
    {
        return ImportPropertyValue(Node, TEXT("InputAction"), ReadOpString(Config, TEXT("input_action")), OutError);
    }
    if (ConfigureCreatedBlueprintNode(Node, Blueprint, Config, OutError))
    {
        return true;
    }
    // A variable added earlier in the same plan may not be on the skeleton class yet.
    if (UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node))
    {
        const FName VariableName(*ReadOpString(Config, TEXT("variable_name")));
        for (const FBPVariableDescription& Description : Blueprint->NewVariables)
        {
            if (Description.VarName == VariableName)
            {
                FGuid Guid = Description.VarGuid;
                Variable->VariableReference.SetSelfMember(VariableName, Guid);
                OutError.Reset();
                return true;
            }
        }
    }
    return false;
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

void AddCustomEventParams(UK2Node_CustomEvent* Event, const TArray<FString>& Positional, FApplyContext& Context, int32 Index)
{
    for (int32 ParamIndex = 1; ParamIndex < Positional.Num(); ++ParamIndex)
    {
        FString Name;
        FString TypeText;
        if (!Positional[ParamIndex].Split(TEXT(":"), &Name, &TypeText))
        {
            Context.Fail(Index, TEXT("invalid_param"), FString::Printf(TEXT("custom event parameter needs Name: Type, got %s"), *Positional[ParamIndex]));
            continue;
        }
        TSharedPtr<FJsonObject> TypeJson = MakeShared<FJsonObject>();
        TypeJson->SetStringField(TEXT("category"), TypeText.TrimStartAndEnd());
        FEdGraphPinType PinType;
        FString Error;
        if (!PinTypeFromJson(TypeJson, PinType, Error))
        {
            Context.Fail(Index, TEXT("invalid_type"), Error);
            continue;
        }
        Event->CreateUserDefinedPin(FName(*Name.TrimStartAndEnd()), PinType, EGPD_Output);
    }
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
    const TArray<FString> Positional = ReadOpStrings(Op, TEXT("positional"));
    TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();
    FString Error;
    if (!ConfigureFromPositional(Blueprint, NodeClass, Positional, Config, Error))
    {
        Context.Fail(Index, TEXT("invalid_positional"), Error);
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
    if (UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node))
    {
        AddCustomEventParams(Event, Positional, Context, Index);
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
