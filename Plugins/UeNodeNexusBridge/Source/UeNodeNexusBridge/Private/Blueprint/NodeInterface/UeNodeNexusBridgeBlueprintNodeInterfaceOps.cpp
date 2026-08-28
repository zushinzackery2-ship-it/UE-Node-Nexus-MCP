#include "UeNodeNexusBridgeBlueprintNodeInterfaceOps.h"

#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "NodeInterface/UeNodeNexusBridgeBlueprintNodeCompactJson.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
FString ShortBlueprintNodeClass(UEdGraphNode* Node)
{
    FString Name = Node ? Node->GetClass()->GetName() : FString();
    Name.RemoveFromStart(TEXT("K2Node_"));
    Name.RemoveFromStart(TEXT("EdGraphNode_"));
    return Name;
}

FString BlueprintNodeAlias(UEdGraph* Graph, UEdGraphNode* Target)
{
    TMap<FString, int32> Counts;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr)
        {
            continue;
        }
        const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
        const FString Base = Title.IsEmpty() ? ShortBlueprintNodeClass(Node) : Title;
        const int32 Index = Counts.FindOrAdd(Base)++;
        const FString Alias = Index == 0 ? Base : FString::Printf(TEXT("%s_%02d"), *Base, Index);
        if (Node == Target)
        {
            return Alias;
        }
    }
    return FString();
}

static UEdGraphNode* ResolveBlueprintInterfaceNode(UEdGraph* Graph, const FString& NodeId)
{
    if (UEdGraphNode* Found = FindBlueprintNode(Graph, NodeId))
    {
        return Found;
    }
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node != nullptr && (BlueprintNodeAlias(Graph, Node).Equals(NodeId, ESearchCase::IgnoreCase) || Node->GetName().Equals(NodeId, ESearchCase::IgnoreCase)))
        {
            return Node;
        }
    }
    return nullptr;
}

int32 BlueprintPinLocalIndex(UEdGraphPin* Pin)
{
    UEdGraphNode* Node = Pin ? Pin->GetOwningNode() : nullptr;
    if (Node == nullptr)
    {
        return 0;
    }
    int32 Index = 0;
    for (UEdGraphPin* Candidate : Node->Pins)
    {
        if (Candidate == nullptr || Candidate->Direction != Pin->Direction)
        {
            continue;
        }
        if (Candidate == Pin)
        {
            return Index;
        }
        ++Index;
    }
    return 0;
}

static TArray<FString> BlueprintPinLines(UEdGraph* Graph, UEdGraphNode* Node, EEdGraphPinDirection Direction)
{
    TArray<FString> Lines;
    int32 LocalIndex = 0;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin == nullptr || Pin->Direction != Direction)
        {
            continue;
        }
        TArray<FString> Links;
        for (UEdGraphPin* Linked : Pin->LinkedTo)
        {
            UEdGraphNode* Other = Linked ? Linked->GetOwningNode() : nullptr;
            if (Other != nullptr)
            {
                Links.Add(FString::Printf(TEXT("%s.%spin_%02d.%s"), *BlueprintNodeAlias(Graph, Other), Direction == EGPD_Input ? TEXT("out") : TEXT("in"), BlueprintPinLocalIndex(Linked), *Linked->PinName.ToString()));
            }
        }
        const FString Prefix = Direction == EGPD_Input ? TEXT("inpin") : TEXT("outpin");
        const FString Op = Direction == EGPD_Input ? TEXT("<") : TEXT(">");
        Lines.Add(FString::Printf(TEXT("%s_%02d.%s %s %s"), *Prefix, LocalIndex++, *Pin->PinName.ToString(), *Op, Links.Num() == 0 ? TEXT("None") : *FString::Join(Links, TEXT(";"))));
    }
    if (Lines.Num() == 0)
    {
        Lines.Add(Direction == EGPD_Input ? TEXT("none_inpin") : TEXT("none_outpin"));
    }
    return Lines;
}

static TArray<FString> BlueprintParamLines(UEdGraphNode* Node)
{
    TArray<FString> Lines;
    int32 Index = 0;
    for (const TSharedPtr<FJsonValue>& Value : BuildBlueprintNodeParams(Node))
    {
        const TSharedPtr<FJsonObject> Param = Value->AsObject();
        Lines.Add(FString::Printf(TEXT("-nodeparam_%02d.%s = %s"), Index++, *Param->GetStringField(TEXT("name")), *Param->GetStringField(TEXT("default_value"))));
    }
    if (Lines.Num() == 0)
    {
        Lines.Add(TEXT("none_nodeparam"));
    }
    return Lines;
}

static TSharedPtr<FJsonObject> MakeBlueprintNodeData(UBlueprint* Blueprint, UEdGraph* Graph, UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Payload, const FString& Format)
{
    const FString Alias = BlueprintNodeAlias(Graph, Node);
    TArray<FString> Header = {
        FString::Printf(TEXT("Node.Name = %s"), *Alias),
        FString::Printf(TEXT("Node.Class = %s"), *ShortBlueprintNodeClass(Node)),
        FString::Printf(TEXT("Node.Id = %s"), *Alias),
        FString::Printf(TEXT("Node.RealId = %s"), *Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens)),
        FString::Printf(TEXT("Node.Pos = %d,%d"), Node->NodePosX, Node->NodePosY)
    };
    TArray<FString> Inputs = BlueprintPinLines(Graph, Node, EGPD_Input);
    TArray<FString> Params = BlueprintParamLines(Node);
    TArray<FString> Outputs = BlueprintPinLines(Graph, Node, EGPD_Output);

    FString Section = TEXT("all");
    Payload->TryGetStringField(TEXT("section"), Section);
    FString Text;
    bool bValid = true;
    if (Section == TEXT("brief"))
    {
        bValid = AppendSelectedLines(Text, Header, Payload);
    }
    else if (Section == TEXT("input"))
    {
        bValid = AppendSelectedLines(Text, Inputs, Payload);
    }
    else if (Section == TEXT("param"))
    {
        bValid = AppendSelectedLines(Text, Params, Payload);
    }
    else if (Section == TEXT("output"))
    {
        bValid = AppendSelectedLines(Text, Outputs, Payload);
    }
    else if (Section == TEXT("links"))
    {
        bValid = AppendSelectedLines(Text, Inputs, Payload) && AppendSelectedLines(Text, Outputs, Payload);
    }
    else
    {
        AppendSelectedLines(Text, Header, MakeShared<FJsonObject>());
        Text += TEXT("\n");
        AppendSelectedLines(Text, Inputs, MakeShared<FJsonObject>());
        Text += TEXT("\n");
        AppendSelectedLines(Text, Params, MakeShared<FJsonObject>());
        Text += TEXT("\n");
        AppendSelectedLines(Text, Outputs, MakeShared<FJsonObject>());
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), Format);
    Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("blueprint"));
    Data->SetStringField(TEXT("graph_name"), Graph->GetName());
    Data->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    Data->SetStringField(TEXT("node_alias"), Alias);
    if (Format.Equals(TEXT("compact_json"), ESearchCase::IgnoreCase))
    {
        Data->SetStringField(TEXT("format"), TEXT("node_info_compact_json"));
        Data->SetArrayField(TEXT("input"), BuildBlueprintCompactInputRows(Graph, Node));
        Data->SetArrayField(TEXT("param"), BuildBlueprintCompactParamRows(Node));
        Data->SetArrayField(TEXT("output"), BuildBlueprintCompactOutputRows(Graph, Node));
    }
    SetTextPayload(Data, bValid ? Text : TEXT("index_out_of_range"));
    Data->SetBoolField(TEXT("selection_ok"), bValid);
    return Data;
}

UEdGraph* ResolveBlueprintNodeInterfaceGraph(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload)
{
    FString GraphName;
    Payload->TryGetStringField(TEXT("graph_name"), GraphName);
    return FindBlueprintGraph(Blueprint, GraphName);
}

UEdGraphNode* ResolveBlueprintNodeInterfaceNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Payload)
{
    FString NodeId;
    if (!Payload->TryGetStringField(TEXT("node_id"), NodeId))
    {
        return nullptr;
    }
    return ResolveBlueprintInterfaceNode(Graph, NodeId);
}

TSharedPtr<FJsonObject> BuildBlueprintNodeInterfaceData(UBlueprint* Blueprint, UEdGraph* Graph, UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Payload, const FString& Format)
{
    return MakeBlueprintNodeData(Blueprint, Graph, Node, Payload, Format);
}
}
