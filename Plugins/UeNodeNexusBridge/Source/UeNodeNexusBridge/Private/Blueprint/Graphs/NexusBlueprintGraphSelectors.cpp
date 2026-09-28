#include "NexusBlueprintGraphSelectors.h"

#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"

namespace UeNodeNexusBridge
{
FString BlueprintGraphSelector(const UEdGraph* Graph)
{
    if (Graph == nullptr)
    {
        return FString();
    }
    const UBlueprint* Blueprint = Graph->GetTypedOuter<UBlueprint>();
    return Blueprint != nullptr ? Graph->GetPathName(Blueprint) : Graph->GetPathName();
}

UEdGraph* ResolveBlueprintGraphSelector(UBlueprint* Blueprint, const FString& Selector)
{
    if (Blueprint == nullptr)
    {
        return nullptr;
    }
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    if (Selector.IsEmpty())
    {
        return Graphs.Num() > 0 ? Graphs[0] : nullptr;
    }
    FGuid RequestedGuid;
    const bool bHasGuid = FGuid::Parse(Selector, RequestedGuid) && RequestedGuid.IsValid();
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph != nullptr &&
            (Graph->GetPathName().Equals(Selector, ESearchCase::IgnoreCase) ||
             BlueprintGraphSelector(Graph).Equals(Selector, ESearchCase::IgnoreCase) ||
             (bHasGuid && Graph->GraphGuid == RequestedGuid)))
        {
            return Graph;
        }
    }
    UEdGraph* Match = nullptr;
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr || !Graph->GetName().Equals(Selector, ESearchCase::IgnoreCase))
        {
            continue;
        }
        if (Match != nullptr)
        {
            return nullptr;
        }
        Match = Graph;
    }
    return Match;
}
}
