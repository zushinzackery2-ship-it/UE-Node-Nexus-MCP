#include "UeNodeNexusBridgeOperations.h"

#include "AnimGraphNode_StateMachineBase.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
namespace
{
FString MachineTitle(UAnimGraphNode_StateMachineBase* MachineNode)
{
    return MachineNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
}

// Resolves the target state machine graph. machine_name may be empty when the
// blueprint contains exactly one machine; otherwise the ambiguity is an error.
UAnimationStateMachineGraph* ResolveStateMachineGraph(UAnimBlueprint* AnimBlueprint, const FString& MachineName, FString& OutTitle, TArray<FString>& OutAvailable)
{
    TArray<UEdGraph*> Graphs;
    AnimBlueprint->GetAllGraphs(Graphs);

    UAnimationStateMachineGraph* Match = nullptr;
    int32 MachineCount = 0;
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UAnimGraphNode_StateMachineBase* MachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
            if (MachineNode == nullptr || MachineNode->EditorStateMachineGraph == nullptr)
            {
                continue;
            }
            ++MachineCount;
            const FString Title = MachineTitle(MachineNode);
            OutAvailable.Add(Title);
            const bool bNameMatches = !MachineName.IsEmpty() && Title.Equals(MachineName, ESearchCase::IgnoreCase);
            if (bNameMatches || MachineName.IsEmpty())
            {
                Match = MachineNode->EditorStateMachineGraph;
                OutTitle = Title;
            }
        }
    }

    if (!MachineName.IsEmpty())
    {
        return OutAvailable.ContainsByPredicate([&MachineName](const FString& Title) { return Title.Equals(MachineName, ESearchCase::IgnoreCase); }) ? Match : nullptr;
    }
    return MachineCount == 1 ? Match : nullptr;
}

TSharedPtr<FJsonObject> MakeMachineResolveError(const FString& Operation, const FString& RequestId, const FString& MachineName, const TArray<FString>& Available)
{
    const FString Code = MachineName.IsEmpty() && Available.Num() > 1 ? TEXT("machine_ambiguous") : TEXT("machine_not_found");
    const FString Message = FString::Printf(
        TEXT("%s. Available state machines: %s"),
        MachineName.IsEmpty() ? TEXT("machine_name is required when the blueprint has multiple state machines") : *FString::Printf(TEXT("No state machine named: %s"), *MachineName),
        Available.Num() == 0 ? TEXT("(none)") : *FString::Join(Available, TEXT(", ")));
    return MakeOperationError(Operation, RequestId, Code, Message);
}

UAnimStateNodeBase* FindStateByName(UAnimationStateMachineGraph* Graph, const FString& StateName)
{
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Cast<UAnimStateTransitionNode>(Node) != nullptr)
        {
            continue;
        }
        UAnimStateNodeBase* State = Cast<UAnimStateNodeBase>(Node);
        if (State != nullptr && State->GetStateName().Equals(StateName, ESearchCase::IgnoreCase))
        {
            return State;
        }
    }
    return nullptr;
}

int32 CountStates(UAnimationStateMachineGraph* Graph)
{
    int32 Count = 0;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Cast<UAnimStateTransitionNode>(Node) == nullptr && Cast<UAnimStateNodeBase>(Node) != nullptr)
        {
            ++Count;
        }
    }
    return Count;
}

TSharedPtr<FJsonObject> MakeStateMachineWriteData(UAnimBlueprint* AnimBlueprint, const FString& MachineTitleText, bool bDryRun, bool bApplied)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AnimBlueprint->GetPathName());
    Data->SetStringField(TEXT("machine"), MachineTitleText);
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), bApplied);
    Data->SetBoolField(TEXT("changed"), bApplied);
    return Data;
}
}

TSharedPtr<FJsonObject> HandleAnimStateMachineStateAdd(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> ErrorResponse;
    UAnimBlueprint* AnimBlueprint = LoadAssetOrError<UAnimBlueprint>(Payload, Operation, RequestId, ErrorResponse, TEXT("AnimBlueprint"));
    if (AnimBlueprint == nullptr)
    {
        return ErrorResponse;
    }

    FString StateName;
    if (!Payload->TryGetStringField(TEXT("state_name"), StateName) || StateName.TrimStartAndEnd().IsEmpty())
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("state_name is required"));
    }
    StateName = StateName.TrimStartAndEnd();

    FString MachineName;
    Payload->TryGetStringField(TEXT("machine_name"), MachineName);
    FString ResolvedTitle;
    TArray<FString> Available;
    UAnimationStateMachineGraph* Graph = ResolveStateMachineGraph(AnimBlueprint, MachineName, ResolvedTitle, Available);
    if (Graph == nullptr)
    {
        return MakeMachineResolveError(Operation, RequestId, MachineName, Available);
    }

    if (FindStateByName(Graph, StateName) != nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("state_already_exists"), FString::Printf(TEXT("State already exists: %s"), *StateName));
    }

    double X = 0.0, Y = 0.0;
    Payload->TryGetNumberField(TEXT("x"), X);
    Payload->TryGetNumberField(TEXT("y"), Y);
    bool bSetAsEntry = false;
    Payload->TryGetBoolField(TEXT("set_as_entry"), bSetAsEntry);
    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);

    if (!bDryRun)
    {
        FGraphNodeCreator<UAnimStateNode> NodeCreator(*Graph);
        UAnimStateNode* StateNode = NodeCreator.CreateNode();
        StateNode->NodePosX = static_cast<int32>(X);
        StateNode->NodePosY = static_cast<int32>(Y);
        NodeCreator.Finalize();
        if (StateNode->GetBoundGraph() != nullptr)
        {
            FBlueprintEditorUtils::RenameGraph(StateNode->GetBoundGraph(), StateName);
        }
        if (bSetAsEntry && Graph->EntryNode != nullptr && Graph->EntryNode->Pins.Num() > 0 && StateNode->GetInputPin() != nullptr)
        {
            Graph->EntryNode->Pins[0]->BreakAllPinLinks();
            Graph->EntryNode->Pins[0]->MakeLinkTo(StateNode->GetInputPin());
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
    }

    TSharedPtr<FJsonObject> Data = MakeStateMachineWriteData(AnimBlueprint, ResolvedTitle, bDryRun, !bDryRun);
    Data->SetStringField(TEXT("state_name"), StateName);
    Data->SetBoolField(TEXT("set_as_entry"), bSetAsEntry);
    Data->SetNumberField(TEXT("state_count"), CountStates(Graph));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleAnimStateMachineTransitionAdd(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> ErrorResponse;
    UAnimBlueprint* AnimBlueprint = LoadAssetOrError<UAnimBlueprint>(Payload, Operation, RequestId, ErrorResponse, TEXT("AnimBlueprint"));
    if (AnimBlueprint == nullptr)
    {
        return ErrorResponse;
    }

    FString FromName, ToName;
    if (!Payload->TryGetStringField(TEXT("from_state"), FromName) || FromName.IsEmpty() || !Payload->TryGetStringField(TEXT("to_state"), ToName) || ToName.IsEmpty())
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("from_state and to_state are required"));
    }

    FString MachineName;
    Payload->TryGetStringField(TEXT("machine_name"), MachineName);
    FString ResolvedTitle;
    TArray<FString> Available;
    UAnimationStateMachineGraph* Graph = ResolveStateMachineGraph(AnimBlueprint, MachineName, ResolvedTitle, Available);
    if (Graph == nullptr)
    {
        return MakeMachineResolveError(Operation, RequestId, MachineName, Available);
    }

    UAnimStateNodeBase* FromState = FindStateByName(Graph, FromName);
    if (FromState == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("state_not_found"), FString::Printf(TEXT("from_state not found: %s"), *FromName));
    }
    UAnimStateNodeBase* ToState = FindStateByName(Graph, ToName);
    if (ToState == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("state_not_found"), FString::Printf(TEXT("to_state not found: %s"), *ToName));
    }

    int32 TransitionCount = 0;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node);
        if (Transition == nullptr)
        {
            continue;
        }
        ++TransitionCount;
        if (Transition->GetPreviousState() == FromState && Transition->GetNextState() == ToState)
        {
            return MakeOperationError(Operation, RequestId, TEXT("transition_already_exists"), FString::Printf(TEXT("A transition %s -> %s already exists"), *FromName, *ToName));
        }
    }

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);

    if (!bDryRun)
    {
        FGraphNodeCreator<UAnimStateTransitionNode> NodeCreator(*Graph);
        UAnimStateTransitionNode* TransitionNode = NodeCreator.CreateNode();
        NodeCreator.Finalize();
        TransitionNode->CreateConnections(FromState, ToState);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
        ++TransitionCount;
    }

    TSharedPtr<FJsonObject> Data = MakeStateMachineWriteData(AnimBlueprint, ResolvedTitle, bDryRun, !bDryRun);
    Data->SetStringField(TEXT("from_state"), FromState->GetStateName());
    Data->SetStringField(TEXT("to_state"), ToState->GetStateName());
    Data->SetNumberField(TEXT("transition_count"), TransitionCount);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
