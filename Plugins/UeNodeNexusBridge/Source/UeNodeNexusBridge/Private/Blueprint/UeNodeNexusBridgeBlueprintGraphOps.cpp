#include "UeNodeNexusBridgeOperations.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "UeNodeNexusBridgeBlueprintGraphFilter.h"
#include "UeNodeNexusBridgeBlueprintGraphJson.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeBlueprintPinDefaults.h"
#include "UeNodeNexusBridgeCompactGraph.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeWireGraph.h"

namespace UeNodeNexusBridge
{
static UEdGraph* ResolveBlueprintGraph(UBlueprint* Blueprint, const FString& GraphName)
{
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);

    UEdGraph* TargetGraph = Graphs.Num() > 0 ? Graphs[0] : nullptr;
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph != nullptr && (GraphName.IsEmpty() || Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase)))
        {
            TargetGraph = Graph;
            break;
        }
    }

    return TargetGraph;
}

static void AppendBlueprintCompactNode(FCompactGraphBuilder& Builder, UEdGraphNode* Node, bool bIncludeNodeParams, bool bIncludeLinks, const FBlueprintGraphFilterResult& FilterResult)
{
    const FString NodeId = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    Builder.AddNode(NodeId, Node->GetClass()->GetName(), Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), Node->NodePosX, Node->NodePosY);

    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin == nullptr)
        {
            continue;
        }

        const FString PinId = Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens);
        Builder.AddPin(PinId, NodeId, PinDirectionToString(Pin->Direction), Pin->PinName.ToString(), CompactBlueprintPinType(Pin), BlueprintPinDefaultText(Pin));
        if (bIncludeLinks && Pin->Direction == EGPD_Output)
        {
            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
                if (LinkedNode != nullptr && FilterResult.ShouldInclude(LinkedNode))
                {
                    Builder.AddLink(PinId, LinkedPin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
                }
            }
        }
    }

    if (bIncludeNodeParams)
    {
        for (const TSharedPtr<FJsonValue>& ParamValue : BuildBlueprintNodeParams(Node))
        {
            Builder.AddParam(NodeId, ParamValue->AsObject());
        }
    }
}

static TSharedPtr<FJsonObject> BuildBlueprintCompactGraphSnapshot(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, UEdGraph* TargetGraph, bool bIncludeNodeParams, bool bIncludeLinks, const FBlueprintGraphFilterResult& FilterResult)
{
    FCompactGraphBuilder Builder;
    Builder.Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Builder.Data->SetStringField(TEXT("asset_class"), Blueprint->GetClass()->GetPathName());
    Builder.Data->SetStringField(TEXT("graph_name"), TargetGraph->GetName());
    Builder.Data->SetStringField(TEXT("graph_kind"), TEXT("blueprint"));

    for (UEdGraphNode* Node : TargetGraph->Nodes)
    {
        if (Node != nullptr && FilterResult.ShouldInclude(Node))
        {
            AppendBlueprintCompactNode(Builder, Node, bIncludeNodeParams, bIncludeLinks, FilterResult);
        }
    }
    Builder.FinalizeIds();

    if (!FilterResult.IncludesAll())
    {
        Builder.Data->SetObjectField(TEXT("filter_stats"), FilterResult.ToJson());
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Builder.Data);
    return Response;
}

static TSharedPtr<FJsonObject> BuildBlueprintWireGraphSnapshot(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, UEdGraph* TargetGraph, bool bIncludeLinks, bool bMin, bool bTiny, const FBlueprintGraphFilterResult& FilterResult, bool bExecOnly)
{
    FWireGraphBuilder Builder(TEXT("Wire graph"));
    for (UEdGraphNode* Node : TargetGraph->Nodes)
    {
        if (Node == nullptr || !FilterResult.ShouldInclude(Node))
        {
            continue;
        }

        Builder.AddNodeType(Node->GetClass()->GetName());
        if (!bIncludeLinks)
        {
            continue;
        }

        const FString FromNodeId = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
        const FString FromNode = MakeWireGraphNodeLabel(Node->GetClass()->GetName(), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin == nullptr || Pin->Direction != EGPD_Output)
            {
                continue;
            }
            if (bExecOnly && !IsExecPin(Pin))
            {
                continue;
            }
            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
                if (LinkedNode == nullptr || !FilterResult.ShouldInclude(LinkedNode))
                {
                    continue;
                }

                const FString ToNodeId = LinkedNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
                const FString ToNode = MakeWireGraphNodeLabel(LinkedNode->GetClass()->GetName(), LinkedNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
                Builder.AddWire(FromNode, Pin->PinName.ToString(), FromNodeId, ToNode, LinkedPin->PinName.ToString(), ToNodeId);
            }
        }
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    const FString Text = bTiny ? Builder.BuildTinyText() : (bMin ? Builder.BuildMinText() : Builder.BuildText());
    const FString OutputFormat = bTiny ? TEXT("wires_tiny") : (bMin ? TEXT("wires_min") : TEXT("wires_text"));
    TSharedPtr<FJsonObject> Data = MakeWireGraphData(Blueprint->GetPathName(), Blueprint->GetClass()->GetPathName(), TargetGraph->GetName(), TEXT("blueprint"), Text, OutputFormat);

    if (!FilterResult.IncludesAll())
    {
        Data->SetObjectField(TEXT("filter_stats"), FilterResult.ToJson());
    }

    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> BuildBlueprintGraphSnapshot(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const FString& GraphName, bool bIncludeNodeParams, bool bIncludeLinks, bool bCompact, bool bWire, bool bWireMin, bool bWireTiny, const FBlueprintGraphFilter& Filter)
{
    UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, GraphName);
    if (TargetGraph == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("graph_not_found"), TEXT("Blueprint graph was not found")));
        return Response;
    }

    FBlueprintGraphFilterResult FilterResult = ApplyBlueprintGraphFilter(TargetGraph, Filter);

    if (bWire)
    {
        return BuildBlueprintWireGraphSnapshot(Operation, RequestId, Blueprint, TargetGraph, bIncludeLinks, bWireMin, bWireTiny, FilterResult, Filter.bExecOnly);
    }

    if (bCompact)
    {
        return BuildBlueprintCompactGraphSnapshot(Operation, RequestId, Blueprint, TargetGraph, bIncludeNodeParams, bIncludeLinks, FilterResult);
    }

    TArray<TSharedPtr<FJsonValue>> Nodes;
    TArray<TSharedPtr<FJsonValue>> Links;
    for (UEdGraphNode* Node : TargetGraph->Nodes)
    {
        if (Node == nullptr || !FilterResult.ShouldInclude(Node))
        {
            continue;
        }
        Nodes.Add(MakeShared<FJsonValueObject>(BlueprintNodeToJson(Node, bIncludeNodeParams)));
        if (bIncludeLinks)
        {
            AddBlueprintLinks(Node, Links, FilterResult);
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Data->SetStringField(TEXT("asset_class"), Blueprint->GetClass()->GetPathName());
    Data->SetStringField(TEXT("graph_name"), TargetGraph->GetName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("blueprint"));
    Data->SetArrayField(TEXT("nodes"), Nodes);
    Data->SetArrayField(TEXT("links"), Links);

    if (!FilterResult.IncludesAll())
    {
        Data->SetObjectField(TEXT("filter_stats"), FilterResult.ToJson());
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
