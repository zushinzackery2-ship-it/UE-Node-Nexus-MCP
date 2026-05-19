#include "UeNodeNexusBridgeNodeInterfaceOps.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeGraphIndexedInfoOps.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static FString BlueprintPinToken(UEdGraphPin* Pin)
{
    return Pin ? EscapeIndexedToken(Pin->PinName.ToString()) : FString();
}

static void AppendBlueprintIndexedParams(UEdGraphNode* Node, int32 NodeIndex, TMap<FString, int32>& ParamDict, TArray<FString>& Params, TArray<FString>& ValueRows)
{
    TArray<FString> NodeValues;
    for (const TSharedPtr<FJsonValue>& Value : BuildBlueprintNodeParams(Node))
    {
        const TSharedPtr<FJsonObject> Param = Value->AsObject();
        const FString ParamValue = Param->GetStringField(TEXT("default_value"));
        if (ParamValue.IsEmpty())
        {
            continue;
        }

        const int32 ParamIndex = DictIndex(ParamDict, Params, Param->GetStringField(TEXT("name")));
        NodeValues.Add(FString::Printf(TEXT("%d=%s"), ParamIndex, *EscapeIndexedToken(ParamValue)));
    }
    if (NodeValues.Num() > 0)
    {
        ValueRows.Add(FString::Printf(TEXT("%d:%s"), NodeIndex, *FString::Join(NodeValues, TEXT(";"))));
    }
}

static void AppendBlueprintIndexedEdges(UEdGraphNode* Node, int32 NodeIndex, const TMap<UEdGraphNode*, int32>& NodeIndices, TArray<FString>& EdgeRows)
{
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin == nullptr || Pin->Direction != EGPD_Output)
        {
            continue;
        }

        for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
        {
            UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
            const int32* TargetNodeIndex = LinkedNode ? NodeIndices.Find(LinkedNode) : nullptr;
            if (TargetNodeIndex == nullptr)
            {
                continue;
            }

            EdgeRows.Add(FString::Printf(TEXT("%d.%s>%d.%s"), NodeIndex, *BlueprintPinToken(Pin), *TargetNodeIndex, *BlueprintPinToken(LinkedPin)));
        }
    }
}

TSharedPtr<FJsonObject> BuildBlueprintGraphIndexedData(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Payload, bool bWithPosition)
{
    const int32 MaxNodes = ReadIndexedMaxNodes(Payload);
    const bool bRealIds = WantsRealIds(Payload);

    TArray<UEdGraphNode*> Nodes;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node != nullptr)
        {
            if (MaxNodes > 0 && Nodes.Num() >= MaxNodes)
            {
                break;
            }
            Nodes.Add(Node);
        }
    }

    TMap<UEdGraphNode*, int32> NodeIndices;
    for (int32 Index = 0; Index < Nodes.Num(); ++Index)
    {
        NodeIndices.Add(Nodes[Index], Index);
    }

    TMap<FString, int32> TypeDict;
    TArray<FString> Types;
    TMap<FString, int32> ParamDict;
    TArray<FString> Params;
    TArray<FString> NodeRows;
    TArray<FString> ValueRows;
    TArray<FString> EdgeRows;
    TArray<FString> PositionRows;
    TArray<FString> RealIdRows;

    for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
    {
        UEdGraphNode* Node = Nodes[NodeIndex];
        const int32 TypeIndex = DictIndex(TypeDict, Types, ShortBlueprintNodeClass(Node));
        NodeRows.Add(FString::Printf(TEXT("%d:%d:%s"), NodeIndex, TypeIndex, *EscapeIndexedToken(BlueprintNodeAlias(Graph, Node))));

        AppendBlueprintIndexedParams(Node, NodeIndex, ParamDict, Params, ValueRows);
        AppendBlueprintIndexedEdges(Node, NodeIndex, NodeIndices, EdgeRows);

        if (bWithPosition)
        {
            PositionRows.Add(FString::Printf(TEXT("%d=%d,%d"), NodeIndex, Node->NodePosX, Node->NodePosY));
        }
        if (bRealIds)
        {
            RealIdRows.Add(FString::Printf(TEXT("%d=%s"), NodeIndex, *Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens)));
        }
    }

    FString Text = FString::Printf(TEXT("G:%s|blueprint|%s|%d\n"), *EscapeIndexedToken(Blueprint->GetPathName()), *EscapeIndexedToken(Graph->GetName()), Graph->Nodes.Num());
    Text += JoinDictionaryLine(TEXT("T:"), Types);
    Text += JoinDictionaryLine(TEXT("P:"), Params);
    Text += TEXT("N:") + FString::Join(NodeRows, TEXT(";")) + TEXT("\n");
    Text += TEXT("V:") + FString::Join(ValueRows, TEXT("|")) + TEXT("\n");
    Text += TEXT("E:") + FString::Join(EdgeRows, TEXT(";")) + TEXT("\n");
    if (bWithPosition)
    {
        Text += TEXT("X:") + FString::Join(PositionRows, TEXT(";")) + TEXT("\n");
    }
    if (bRealIds)
    {
        Text += TEXT("R:") + FString::Join(RealIdRows, TEXT(";")) + TEXT("\n");
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), bWithPosition ? TEXT("graph_node_indexed_w_pos") : TEXT("graph_node_indexed"));
    Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("blueprint"));
    Data->SetStringField(TEXT("graph_name"), Graph->GetName());
    Data->SetNumberField(TEXT("total_nodes"), Graph->Nodes.Num());
    Data->SetNumberField(TEXT("returned_nodes"), Nodes.Num());
    Data->SetBoolField(TEXT("truncated"), Nodes.Num() < Graph->Nodes.Num());
    SetTextPayload(Data, Text);
    return Data;
}
}
