#include "NexusBlueprintPinTypes.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"

namespace UeNodeNexusBridge::Transcode
{
namespace
{
// Types settle in chains: an array pin takes its type from its source, then
// hands it to the element pins that depend on it. Each pass moves the frontier
// one link, and a handful covers any graph a plan produces; the loop stops as
// soon as a pass changes nothing.
constexpr int32 MaxPasses = 6;

bool IsUnresolved(const UEdGraphPin* Pin)
{
    return Pin != nullptr && !Pin->bOrphanedPin && Pin->LinkedTo.Num() > 0
        && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard;
}

int32 CountUnresolved(const TArray<UEdGraph*>& Graphs)
{
    int32 Count = 0;
    for (const UEdGraph* Graph : Graphs)
    {
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr)
            {
                continue;
            }
            for (const UEdGraphPin* Pin : Node->Pins)
            {
                Count += IsUnresolved(Pin) ? 1 : 0;
            }
        }
    }
    return Count;
}

void ReplayConnections(const TArray<UEdGraph*>& Graphs)
{
    for (UEdGraph* Graph : Graphs)
    {
        TArray<UEdGraphNode*> Nodes = Graph->Nodes;
        for (UEdGraphNode* Node : Nodes)
        {
            if (Node == nullptr || !Graph->Nodes.Contains(Node))
            {
                continue;
            }
            // Handling a notification can collapse split pins and shorten the
            // array, so the bound is re-read every step, as the engine does in
            // UK2Node_CallArrayFunction::PostReconstructNode.
            for (int32 Index = 0; Index < Node->Pins.Num(); ++Index)
            {
                UEdGraphPin* Pin = Node->Pins[Index];
                if (Pin != nullptr && Pin->LinkedTo.Num() > 0)
                {
                    Node->PinConnectionListChanged(Pin);
                }
            }
        }
    }
}

void CollectUnresolved(const TArray<UEdGraph*>& Graphs, TArray<FString>& OutNames)
{
    for (const UEdGraph* Graph : Graphs)
    {
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr)
            {
                continue;
            }
            for (const UEdGraphPin* Pin : Node->Pins)
            {
                if (IsUnresolved(Pin))
                {
                    OutNames.Add(FString::Printf(TEXT("%s.%s"),
                        *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), *Pin->PinName.ToString()));
                }
            }
        }
    }
}
}

FWildcardResolution ResolveWildcardPins(const TArray<UEdGraph*>& Graphs)
{
    // A node like UK2Node_CallArrayFunction creates TargetArray as a wildcard and
    // only takes a type from NotifyPinConnectionListChanged, which reads the type
    // its first link has at that moment. Interactive editing always connects an
    // already typed source, so one notification is enough; a plan applies links in
    // its own order, so a source that is typed later leaves the sink a wildcard
    // forever and every array write in the asset fails to compile. Replaying the
    // notifications once the whole batch is in place is what the editor itself
    // does through ReconstructNode, without reconstructing anything.
    FWildcardResolution Result;
    Result.Before = CountUnresolved(Graphs);
    Result.Remaining = Result.Before;
    for (int32 Pass = 0; Pass < MaxPasses && Result.Remaining > 0; ++Pass)
    {
        ReplayConnections(Graphs);
        const int32 After = CountUnresolved(Graphs);
        if (After >= Result.Remaining)
        {
            Result.Remaining = After;
            break;
        }
        Result.Remaining = After;
    }
    if (Result.Remaining > 0)
    {
        CollectUnresolved(Graphs, Result.Unresolved);
    }
    return Result;
}
}
