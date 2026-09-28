#include "NexusGraphSchedule.h"

#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace UeNodeNexusBridge::Transcode
{
void RefreshFunctionCalls(UBlueprint* Blueprint, const TSet<FName>& Functions)
{
    TArray<UK2Node_CallFunction*> Calls;
    FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, Calls);
    for (UK2Node_CallFunction* Call : Calls)
    {
        if (Call->FunctionReference.IsSelfContext() && Functions.Contains(Call->FunctionReference.GetMemberName()))
        {
            Call->Modify();
            Call->ReconstructNode();
        }
    }
}

static bool NeedsTypeAnchor(UBlueprint* Blueprint, UEdGraph* Graph,
    const TSharedPtr<FJsonObject>& Op, const FApplyContext& Context)
{
    auto* From = ResolveGraphNode(Blueprint, Graph, Context, ReadOpString(Op, TEXT("from")));
    auto* To = ResolveGraphNode(Blueprint, Graph, Context, ReadOpString(Op, TEXT("to")));
    if (!From || !To)
    {
        return false;
    }
    auto* Output = ResolvePin(From, ReadOpString(Op, TEXT("from_pin")), EGPD_Output);
    auto* Input = ResolvePin(To, ReadOpString(Op, TEXT("to_pin")), EGPD_Input);
    return Output && Input && Output->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard
        && Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard;
}

void ApplyScheduledGraphOps(UBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>& Plan,
    const TArray<int32>& Indices, FApplyContext& Context, TArray<UEdGraph*>& Touched)
{
    TArray<TPair<int32, UEdGraph*>> Links;
    for (const int32 Index : Indices)
    {
        const auto Op = Plan[Index]->AsObject();
        const FString Name = ReadOpString(Op, TEXT("graph"), TEXT("EventGraph"));
        auto* Graph = FindBlueprintGraph(Blueprint, Name);
        if (!Graph)
        {
            Context.Fail(Index, TEXT("graph_not_found"), TEXT("graph not found: ") + Name);
            continue;
        }
        Touched.AddUnique(Graph);
        if (ReadOpString(Op, TEXT("op")) == TEXT("connect_pins") && !Context.bDryRun)
        {
            Links.Emplace(Index, Graph);
        }
        else
        {
            ApplyBlueprintGraphVerb(Blueprint, Graph, Op, Index, Context);
        }
    }
    while (!Links.IsEmpty())
    {
        TArray<TPair<int32, UEdGraph*>> Pending;
        for (const auto& Link : Links)
        {
            const auto Op = Plan[Link.Key]->AsObject();
            if (NeedsTypeAnchor(Blueprint, Link.Value, Op, Context))
            {
                Pending.Add(Link);
                continue;
            }
            ApplyBlueprintGraphVerb(Blueprint, Link.Value, Op, Link.Key, Context);
        }
        if (Pending.Num() == Links.Num())
        {
            // No type anchor remains: let the native schema accept or diagnose
            // the unresolved chain, retaining the original operation indices.
            for (const auto& Link : Pending)
            {
                ApplyBlueprintGraphVerb(Blueprint, Link.Value, Plan[Link.Key]->AsObject(), Link.Key, Context);
            }
            break;
        }
        Links = MoveTemp(Pending);
    }
}
}
