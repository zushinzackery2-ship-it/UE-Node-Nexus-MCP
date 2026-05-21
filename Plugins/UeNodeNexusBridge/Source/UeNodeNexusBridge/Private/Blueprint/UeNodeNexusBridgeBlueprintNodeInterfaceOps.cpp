#include "UeNodeNexusBridgeBlueprintNodeInterfaceOps.h"

#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
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

static TSharedPtr<FJsonObject> MakeBlueprintPinJson(UEdGraph* Graph, UEdGraphPin* Pin)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("pin_id"), Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
    Json->SetStringField(TEXT("name"), Pin->PinName.ToString());
    Json->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
    Json->SetNumberField(TEXT("local_index"), BlueprintPinLocalIndex(Pin));
    Json->SetStringField(TEXT("default_value"), Pin->DefaultValue);
    Json->SetStringField(TEXT("default_object"), Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : FString());
    Json->SetStringField(TEXT("type_category"), Pin->PinType.PinCategory.ToString());
    Json->SetStringField(TEXT("type_subcategory"), Pin->PinType.PinSubCategory.ToString());
    Json->SetStringField(TEXT("type_subcategory_object"), Pin->PinType.PinSubCategoryObject.Get() ? Pin->PinType.PinSubCategoryObject->GetPathName() : FString());

    TArray<TSharedPtr<FJsonValue>> Links;
    for (UEdGraphPin* Linked : Pin->LinkedTo)
    {
        UEdGraphNode* LinkedNode = Linked ? Linked->GetOwningNode() : nullptr;
        if (LinkedNode == nullptr)
        {
            continue;
        }

        TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
        Link->SetStringField(TEXT("node_id"), LinkedNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        Link->SetStringField(TEXT("node_alias"), BlueprintNodeAlias(Graph, LinkedNode));
        Link->SetStringField(TEXT("pin_id"), Linked->PinId.ToString(EGuidFormats::DigitsWithHyphens));
        Link->SetStringField(TEXT("pin_name"), Linked->PinName.ToString());
        Links.Add(MakeShared<FJsonValueObject>(Link));
    }
    Json->SetArrayField(TEXT("links"), Links);
    return Json;
}

static TArray<TSharedPtr<FJsonValue>> BuildBlueprintPins(UEdGraph* Graph, UEdGraphNode* Node)
{
    TArray<TSharedPtr<FJsonValue>> Pins;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin != nullptr)
        {
            Pins.Add(MakeShared<FJsonValueObject>(MakeBlueprintPinJson(Graph, Pin)));
        }
    }
    return Pins;
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

static bool ReadIndex(const TSharedPtr<FJsonObject>& Payload, int32& OutIndex)
{
    double Number = 0.0;
    if (!Payload->TryGetNumberField(TEXT("index"), Number))
    {
        return false;
    }
    OutIndex = static_cast<int32>(Number);
    return true;
}

static bool AppendSelected(FString& Text, const TArray<FString>& Lines, const TSharedPtr<FJsonObject>& Payload)
{
    int32 Index = 0;
    if (ReadIndex(Payload, Index))
    {
        if (!Lines.IsValidIndex(Index))
        {
            return false;
        }
        Text += Lines[Index] + TEXT("\n");
        return true;
    }
    for (const FString& Line : Lines)
    {
        Text += Line + TEXT("\n");
    }
    return true;
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
        bValid = AppendSelected(Text, Header, Payload);
    }
    else if (Section == TEXT("input"))
    {
        bValid = AppendSelected(Text, Inputs, Payload);
    }
    else if (Section == TEXT("param"))
    {
        bValid = AppendSelected(Text, Params, Payload);
    }
    else if (Section == TEXT("output"))
    {
        bValid = AppendSelected(Text, Outputs, Payload);
    }
    else if (Section == TEXT("links"))
    {
        bValid = AppendSelected(Text, Inputs, Payload) && AppendSelected(Text, Outputs, Payload);
    }
    else
    {
        AppendSelected(Text, Header, MakeShared<FJsonObject>());
        Text += TEXT("\n");
        AppendSelected(Text, Inputs, MakeShared<FJsonObject>());
        Text += TEXT("\n");
        AppendSelected(Text, Params, MakeShared<FJsonObject>());
        Text += TEXT("\n");
        AppendSelected(Text, Outputs, MakeShared<FJsonObject>());
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), Format);
    Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("blueprint"));
    Data->SetStringField(TEXT("graph_name"), Graph->GetName());
    Data->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    Data->SetStringField(TEXT("node_alias"), Alias);
    Data->SetArrayField(TEXT("pins"), BuildBlueprintPins(Graph, Node));
    Data->SetArrayField(TEXT("params"), BuildBlueprintNodeParams(Node));
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
