#include "UeNodeNexusBridgeBlueprintGraphFilter.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

namespace UeNodeNexusBridge
{
bool IsExecPin(const UEdGraphPin* Pin)
{
    return Pin != nullptr && Pin->PinType.PinCategory == TEXT("exec");
}

static bool NodeMatchesKeyword(UEdGraphNode* Node, const FString& Keyword)
{
    const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
    return Title.Contains(Keyword, ESearchCase::IgnoreCase);
}

static bool NodeMatchesClassFilter(UEdGraphNode* Node, const TArray<FString>& ClassFilter)
{
    const FString ClassName = Node->GetClass()->GetName();
    for (const FString& Filter : ClassFilter)
    {
        if (ClassName.Contains(Filter, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

static void CollectNeighbors(UEdGraphNode* Node, TSet<UEdGraphNode*>& OutNeighbors, bool bExecOnly)
{
    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin == nullptr)
        {
            continue;
        }
        if (bExecOnly && !IsExecPin(Pin))
        {
            continue;
        }
        for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
        {
            UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
            if (LinkedNode != nullptr)
            {
                OutNeighbors.Add(LinkedNode);
            }
        }
    }
}

static UEdGraphNode* FindTraceStartNode(UEdGraph* Graph, const FString& TraceFrom)
{
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr)
        {
            continue;
        }
        if (Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens).Equals(TraceFrom, ESearchCase::IgnoreCase))
        {
            return Node;
        }
    }

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node != nullptr && NodeMatchesKeyword(Node, TraceFrom))
        {
            return Node;
        }
    }

    return nullptr;
}

static void BfsFromNode(UEdGraphNode* StartNode, int32 MaxDepth, bool bExecOnly, TSet<UEdGraphNode*>& OutReachable)
{
    TArray<UEdGraphNode*> CurrentFrontier;
    CurrentFrontier.Add(StartNode);
    OutReachable.Add(StartNode);

    for (int32 Depth = 0; Depth < MaxDepth && CurrentFrontier.Num() > 0; ++Depth)
    {
        TArray<UEdGraphNode*> NextFrontier;
        for (UEdGraphNode* Node : CurrentFrontier)
        {
            TSet<UEdGraphNode*> Neighbors;
            CollectNeighbors(Node, Neighbors, bExecOnly);
            for (UEdGraphNode* Neighbor : Neighbors)
            {
                if (!OutReachable.Contains(Neighbor))
                {
                    OutReachable.Add(Neighbor);
                    NextFrontier.Add(Neighbor);
                }
            }
        }
        CurrentFrontier = MoveTemp(NextFrontier);
    }
}

bool FBlueprintGraphFilter::HasFilter() const
{
    return !Keyword.IsEmpty() || ClassFilter.Num() > 0 || !TraceFrom.IsEmpty() || bExecOnly;
}

FBlueprintGraphFilter FBlueprintGraphFilter::FromPayload(const TSharedPtr<FJsonObject>& Payload)
{
    FBlueprintGraphFilter Filter;
    Payload->TryGetStringField(TEXT("keyword"), Filter.Keyword);
    Payload->TryGetStringField(TEXT("trace_from"), Filter.TraceFrom);
    Payload->TryGetNumberField(TEXT("trace_depth"), Filter.TraceDepth);
    Payload->TryGetBoolField(TEXT("exec_only"), Filter.bExecOnly);

    const TArray<TSharedPtr<FJsonValue>>* ClassFilterArray = nullptr;
    if (Payload->TryGetArrayField(TEXT("node_class_filter"), ClassFilterArray) && ClassFilterArray != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *ClassFilterArray)
        {
            FString Entry;
            if (Value.IsValid() && Value->TryGetString(Entry) && !Entry.IsEmpty())
            {
                Filter.ClassFilter.Add(Entry);
            }
        }
    }

    if (Filter.TraceDepth < 1)
    {
        Filter.TraceDepth = 1;
    }
    if (Filter.TraceDepth > 20)
    {
        Filter.TraceDepth = 20;
    }

    return Filter;
}

TSharedPtr<FJsonObject> FBlueprintGraphFilterResult::ToJson() const
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("total_nodes"), TotalNodes);
    Json->SetNumberField(TEXT("matched_nodes"), MatchedNodes);
    Json->SetNumberField(TEXT("included_nodes"), IncludedNodes.Num());
    return Json;
}

FBlueprintGraphFilterResult ApplyBlueprintGraphFilter(UEdGraph* Graph, const FBlueprintGraphFilter& Filter)
{
    FBlueprintGraphFilterResult Result;
    Result.TotalNodes = Graph->Nodes.Num();

    if (!Filter.HasFilter())
    {
        return Result;
    }

    TSet<UEdGraphNode*> MatchedNodes;

    if (!Filter.TraceFrom.IsEmpty())
    {
        UEdGraphNode* StartNode = FindTraceStartNode(Graph, Filter.TraceFrom);
        if (StartNode != nullptr)
        {
            BfsFromNode(StartNode, Filter.TraceDepth, Filter.bExecOnly, MatchedNodes);
        }
    }

    bool bHasSelectionFilter = !Filter.Keyword.IsEmpty() || Filter.ClassFilter.Num() > 0;

    if (bHasSelectionFilter)
    {
        TSet<UEdGraphNode*> SelectionMatches;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr)
            {
                continue;
            }
            bool bMatches = true;
            if (!Filter.Keyword.IsEmpty() && !NodeMatchesKeyword(Node, Filter.Keyword))
            {
                bMatches = false;
            }
            if (bMatches && Filter.ClassFilter.Num() > 0 && !NodeMatchesClassFilter(Node, Filter.ClassFilter))
            {
                bMatches = false;
            }
            if (bMatches)
            {
                SelectionMatches.Add(Node);
            }
        }

        if (!Filter.TraceFrom.IsEmpty())
        {
            MatchedNodes = MatchedNodes.Intersect(SelectionMatches);
        }
        else
        {
            MatchedNodes = SelectionMatches;
        }
    }

    Result.MatchedNodes = MatchedNodes.Num();

    if (bHasSelectionFilter && Filter.TraceFrom.IsEmpty())
    {
        for (UEdGraphNode* Node : MatchedNodes)
        {
            Result.IncludedNodes.Add(Node);
            CollectNeighbors(Node, Result.IncludedNodes, Filter.bExecOnly);
        }
    }
    else
    {
        Result.IncludedNodes = MatchedNodes;
    }

    if (Filter.bExecOnly && !bHasSelectionFilter && Filter.TraceFrom.IsEmpty())
    {
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr)
            {
                continue;
            }
            bool bHasExecConnection = false;
            for (const UEdGraphPin* Pin : Node->Pins)
            {
                if (IsExecPin(Pin) && Pin->LinkedTo.Num() > 0)
                {
                    bHasExecConnection = true;
                    break;
                }
            }
            if (bHasExecConnection)
            {
                Result.IncludedNodes.Add(Node);
            }
        }
        Result.MatchedNodes = Result.IncludedNodes.Num();
    }

    return Result;
}
}
