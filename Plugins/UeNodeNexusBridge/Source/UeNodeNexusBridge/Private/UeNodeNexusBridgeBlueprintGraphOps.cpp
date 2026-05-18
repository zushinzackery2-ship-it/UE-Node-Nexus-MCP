#include "UeNodeNexusBridgeOperations.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static FString PinDirectionToString(EEdGraphPinDirection Direction)
{
    return Direction == EGPD_Input ? TEXT("input") : TEXT("output");
}

static TSharedPtr<FJsonObject> PinTypeToJson(const FEdGraphPinType& PinType)
{
    TSharedPtr<FJsonObject> Type = MakeShared<FJsonObject>();
    Type->SetStringField(TEXT("category"), PinType.PinCategory.ToString());
    Type->SetStringField(TEXT("subcategory"), PinType.PinSubCategory.ToString());
    Type->SetStringField(TEXT("subcategory_object"), PinType.PinSubCategoryObject.IsValid() ? PinType.PinSubCategoryObject->GetPathName() : FString());
    Type->SetNumberField(TEXT("container_type"), static_cast<int32>(PinType.ContainerType));
    Type->SetBoolField(TEXT("is_reference"), PinType.bIsReference);
    Type->SetBoolField(TEXT("is_const"), PinType.bIsConst);
    return Type;
}

static TSharedPtr<FJsonObject> BlueprintPinToJson(const UEdGraphPin* Pin)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("pin_id"), Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
    Json->SetStringField(TEXT("name"), Pin->PinName.ToString());
    Json->SetStringField(TEXT("direction"), PinDirectionToString(Pin->Direction));
    Json->SetObjectField(TEXT("type"), PinTypeToJson(Pin->PinType));
    Json->SetStringField(TEXT("default_value"), Pin->DefaultValue);

    TArray<TSharedPtr<FJsonValue>> LinkedTo;
    for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
    {
        if (LinkedPin != nullptr && LinkedPin->GetOwningNode() != nullptr)
        {
            LinkedTo.Add(MakeShared<FJsonValueString>(LinkedPin->PinId.ToString(EGuidFormats::DigitsWithHyphens)));
        }
    }
    Json->SetArrayField(TEXT("linked_to"), LinkedTo);
    return Json;
}

static TSharedPtr<FJsonObject> BlueprintNodeToJson(const UEdGraphNode* Node)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    Json->SetStringField(TEXT("class_name"), Node->GetClass()->GetPathName());
    Json->SetStringField(TEXT("display_name"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());

    TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
    Position->SetNumberField(TEXT("x"), Node->NodePosX);
    Position->SetNumberField(TEXT("y"), Node->NodePosY);
    Json->SetObjectField(TEXT("position"), Position);

    TArray<TSharedPtr<FJsonValue>> Pins;
    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin != nullptr)
        {
            Pins.Add(MakeShared<FJsonValueObject>(BlueprintPinToJson(Pin)));
        }
    }
    Json->SetArrayField(TEXT("pins"), Pins);
    Json->SetObjectField(TEXT("params"), MakeShared<FJsonObject>());
    return Json;
}

static void AddBlueprintLinks(const UEdGraphNode* Node, TArray<TSharedPtr<FJsonValue>>& Links)
{
    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin == nullptr || Pin->Direction != EGPD_Output)
        {
            continue;
        }
        for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
        {
            if (LinkedPin == nullptr || LinkedPin->GetOwningNode() == nullptr)
            {
                continue;
            }

            TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
            Link->SetStringField(TEXT("from_node_id"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
            Link->SetStringField(TEXT("from_pin_id"), Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
            Link->SetStringField(TEXT("to_node_id"), LinkedPin->GetOwningNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
            Link->SetStringField(TEXT("to_pin_id"), LinkedPin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
            Links.Add(MakeShared<FJsonValueObject>(Link));
        }
    }
}

TSharedPtr<FJsonObject> BuildBlueprintGraphSnapshot(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const FString& GraphName)
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

    if (TargetGraph == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("graph_not_found"), TEXT("Blueprint graph was not found")));
        return Response;
    }

    TArray<TSharedPtr<FJsonValue>> Nodes;
    TArray<TSharedPtr<FJsonValue>> Links;
    for (const UEdGraphNode* Node : TargetGraph->Nodes)
    {
        if (Node != nullptr)
        {
            Nodes.Add(MakeShared<FJsonValueObject>(BlueprintNodeToJson(Node)));
            AddBlueprintLinks(Node, Links);
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Data->SetStringField(TEXT("asset_class"), Blueprint->GetClass()->GetPathName());
    Data->SetStringField(TEXT("graph_name"), TargetGraph->GetName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("blueprint"));
    Data->SetArrayField(TEXT("nodes"), Nodes);
    Data->SetArrayField(TEXT("links"), Links);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
