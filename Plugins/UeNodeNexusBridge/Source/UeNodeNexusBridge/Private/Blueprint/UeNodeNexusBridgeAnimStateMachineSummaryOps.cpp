#include "UeNodeNexusBridgeOperations.h"

#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "Animation/AnimBlueprint.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "UeNodeNexusBridgeAnimReflectionUtils.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
namespace
{
FString StateName(UAnimStateNodeBase* State)
{
    return State ? State->GetStateName() : FString();
}

// Reflection-derived digest of a transition's blend/rule configuration. Exporting
// the properties (vs hand-mapping enums) keeps blend_mode/logic_type rendered as
// their authored enum names for free.
FString TransitionBlendSummary(UAnimStateTransitionNode* Transition)
{
    TArray<FString> Parts;
    UClass* Class = Transition->GetClass();
    AddAnimField(Parts, Transition, Transition, Class, TEXT("CrossfadeDuration"), TEXT("crossfade"));
    AddAnimField(Parts, Transition, Transition, Class, TEXT("BlendMode"), TEXT("blend_mode"));
    AddAnimField(Parts, Transition, Transition, Class, TEXT("LogicType"), TEXT("logic"));
    AddAnimField(Parts, Transition, Transition, Class, TEXT("PriorityOrder"), TEXT("priority"));
    AddAnimField(Parts, Transition, Transition, Class, TEXT("bAutomaticRuleBasedOnSequencePlayerInState"), TEXT("automatic"));
    AddAnimField(Parts, Transition, Transition, Class, TEXT("SharedRulesName"), TEXT("shared_rules"));
    return FString::Join(Parts, TEXT(";"));
}

// Compact summary of a transition's rule: "automatic" when driven by the
// sequence-player-in-state rule, otherwise the title of the node feeding the
// TransitionResult input (the dominant driver of the condition), else the bound
// graph node count as a structural hint.
FString TransitionRuleSummary(UAnimStateTransitionNode* Transition)
{
    if (Transition->bAutomaticRuleBasedOnSequencePlayerInState)
    {
        return TEXT("automatic");
    }

    UEdGraph* BoundGraph = Transition->GetBoundGraph();
    if (BoundGraph == nullptr)
    {
        return FString();
    }

    for (UEdGraphNode* Node : BoundGraph->Nodes)
    {
        UAnimGraphNode_TransitionResult* Result = Cast<UAnimGraphNode_TransitionResult>(Node);
        if (Result == nullptr)
        {
            continue;
        }
        for (UEdGraphPin* Pin : Result->Pins)
        {
            if (Pin == nullptr || Pin->Direction != EGPD_Input)
            {
                continue;
            }
            if (Pin->LinkedTo.Num() > 0 && Pin->LinkedTo[0] != nullptr && Pin->LinkedTo[0]->GetOwningNodeUnchecked() != nullptr)
            {
                return Pin->LinkedTo[0]->GetOwningNodeUnchecked()->GetNodeTitle(ENodeTitleType::ListView).ToString();
            }
            return Pin->GetDefaultAsString();
        }
    }
    return FString::Printf(TEXT("rule_nodes=%d"), BoundGraph->Nodes.Num());
}

TSharedPtr<FJsonValue> MakeStateRow(UAnimStateNodeBase* State)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(StateName(State)));
    Row.Add(MakeShared<FJsonValueString>(State->GetBoundGraph() ? State->GetBoundGraph()->GetName() : FString()));
    Row.Add(MakeShared<FJsonValueNumber>(State->NodePosX));
    Row.Add(MakeShared<FJsonValueNumber>(State->NodePosY));
    return MakeShared<FJsonValueArray>(Row);
}

TSharedPtr<FJsonObject> MakeStateObject(UAnimStateNodeBase* State)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("id"), State->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    Json->SetStringField(TEXT("name"), StateName(State));
    Json->SetStringField(TEXT("class"), State->GetClass()->GetName());
    Json->SetStringField(TEXT("bound_graph"), State->GetBoundGraph() ? State->GetBoundGraph()->GetName() : FString());
    Json->SetNumberField(TEXT("x"), State->NodePosX);
    Json->SetNumberField(TEXT("y"), State->NodePosY);
    return Json;
}

TSharedPtr<FJsonValue> MakeTransitionRow(UAnimStateTransitionNode* Transition)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(StateName(Transition->GetPreviousState())));
    Row.Add(MakeShared<FJsonValueString>(StateName(Transition->GetNextState())));
    Row.Add(MakeShared<FJsonValueString>(TransitionRuleSummary(Transition)));
    Row.Add(MakeShared<FJsonValueString>(TransitionBlendSummary(Transition)));
    return MakeShared<FJsonValueArray>(Row);
}

TSharedPtr<FJsonObject> MakeTransitionObject(UAnimStateTransitionNode* Transition)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("id"), Transition->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    Json->SetStringField(TEXT("from"), StateName(Transition->GetPreviousState()));
    Json->SetStringField(TEXT("to"), StateName(Transition->GetNextState()));
    Json->SetStringField(TEXT("rule"), TransitionRuleSummary(Transition));
    Json->SetStringField(TEXT("rule_graph"), Transition->GetBoundGraph() ? Transition->GetBoundGraph()->GetName() : FString());
    Json->SetStringField(TEXT("blend"), TransitionBlendSummary(Transition));
    return Json;
}

FString EntryStateName(UAnimationStateMachineGraph* Graph)
{
    if (Graph == nullptr || Graph->EntryNode == nullptr)
    {
        return FString();
    }
    return StateName(Cast<UAnimStateNodeBase>(Graph->EntryNode->GetOutputNode()));
}

TSharedPtr<FJsonObject> MakeStateMachineObject(UAnimGraphNode_StateMachineBase* MachineNode, bool bCompact, int32& OutStateCount, int32& OutTransitionCount)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    UAnimationStateMachineGraph* Graph = MachineNode->EditorStateMachineGraph;
    Json->SetStringField(TEXT("name"), MachineNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
    Json->SetStringField(TEXT("graph"), Graph ? Graph->GetName() : FString());
    Json->SetStringField(TEXT("entry_state"), EntryStateName(Graph));

    TArray<TSharedPtr<FJsonValue>> States;
    TArray<TSharedPtr<FJsonValue>> Transitions;
    if (Graph != nullptr)
    {
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node))
            {
                Transitions.Add(bCompact ? MakeTransitionRow(Transition) : MakeShared<FJsonValueObject>(MakeTransitionObject(Transition)));
            }
            else if (UAnimStateNodeBase* State = Cast<UAnimStateNodeBase>(Node))
            {
                States.Add(bCompact ? MakeStateRow(State) : MakeShared<FJsonValueObject>(MakeStateObject(State)));
            }
        }
    }

    OutStateCount += States.Num();
    OutTransitionCount += Transitions.Num();
    Json->SetArrayField(TEXT("states"), States);
    Json->SetArrayField(TEXT("transitions"), Transitions);
    Json->SetNumberField(TEXT("state_count"), States.Num());
    Json->SetNumberField(TEXT("transition_count"), Transitions.Num());
    return Json;
}
}

TSharedPtr<FJsonObject> HandleAnimStateMachineSummaryGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const double StartSeconds = FPlatformTime::Seconds();

    TSharedPtr<FJsonObject> ErrorResponse;
    UAnimBlueprint* AnimBlueprint = LoadAssetOrError<UAnimBlueprint>(Payload, Operation, RequestId, ErrorResponse, TEXT("AnimBlueprint"));
    if (AnimBlueprint == nullptr)
    {
        return ErrorResponse;
    }

    FString Format = TEXT("compact");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bCompact = !Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);

    TArray<UEdGraph*> Graphs;
    AnimBlueprint->GetAllGraphs(Graphs);

    TArray<TSharedPtr<FJsonValue>> Machines;
    int32 StateCount = 0;
    int32 TransitionCount = 0;
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UAnimGraphNode_StateMachineBase* MachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node))
            {
                Machines.Add(MakeShared<FJsonValueObject>(MakeStateMachineObject(MachineNode, bCompact, StateCount, TransitionCount)));
            }
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AnimBlueprint->GetPathName());
    Data->SetStringField(TEXT("asset_class"), AnimBlueprint->GetClass()->GetPathName());
    if (bCompact)
    {
        Data->SetStringField(TEXT("format"), TEXT("anim_state_machine_summary_compact"));
        Data->SetArrayField(TEXT("state_columns"), { MakeShared<FJsonValueString>(TEXT("name")), MakeShared<FJsonValueString>(TEXT("bound_graph")), MakeShared<FJsonValueString>(TEXT("x")), MakeShared<FJsonValueString>(TEXT("y")) });
        Data->SetArrayField(TEXT("transition_columns"), { MakeShared<FJsonValueString>(TEXT("from")), MakeShared<FJsonValueString>(TEXT("to")), MakeShared<FJsonValueString>(TEXT("rule")), MakeShared<FJsonValueString>(TEXT("blend")) });
    }
    Data->SetArrayField(TEXT("state_machines"), Machines);
    Data->SetNumberField(TEXT("state_machine_count"), Machines.Num());
    Data->SetNumberField(TEXT("state_count"), StateCount);
    Data->SetNumberField(TEXT("transition_count"), TransitionCount);
    AddElapsedMs(Data, StartSeconds);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
