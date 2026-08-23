#include "UeNodeNexusBridgeBlueprintGraphJson.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "UeNodeNexusBridgeBlueprintGraphFilter.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"

namespace UeNodeNexusBridge
{
FString PinDirectionToString(EEdGraphPinDirection Direction)
{
    return Direction == EGPD_Input ? TEXT("input") : TEXT("output");
}

static TSharedPtr<FJsonObject> PinTypeToJson(const FEdGraphPinType& PinType)
{
    TSharedPtr<FJsonObject> Type = MakeShared<FJsonObject>();
    Type->SetStringField(TEXT("category"), PinType.PinCategory.ToString());
    Type->SetStringField(TEXT("subcategory"), PinType.PinSubCategory.ToString());
    Type->SetStringField(
        TEXT("subcategory_object"),
        PinType.PinSubCategoryObject.IsValid() ? PinType.PinSubCategoryObject->GetPathName() : FString());
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
            LinkedTo.Add(
                MakeShared<FJsonValueString>(LinkedPin->PinId.ToString(EGuidFormats::DigitsWithHyphens)));
        }
    }
    Json->SetArrayField(TEXT("linked_to"), LinkedTo);
    return Json;
}

TSharedPtr<FJsonObject> BlueprintNodeToJson(UEdGraphNode* Node, bool bIncludeNodeParams)
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
    Json->SetArrayField(
        TEXT("params"),
        bIncludeNodeParams ? BuildBlueprintNodeParams(Node) : TArray<TSharedPtr<FJsonValue>>());
    return Json;
}

void AddBlueprintLinks(
    const UEdGraphNode* Node,
    TArray<TSharedPtr<FJsonValue>>& Links,
    const FBlueprintGraphFilterResult& FilterResult)
{
    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin == nullptr || Pin->Direction != EGPD_Output)
        {
            continue;
        }
        for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
        {
            UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
            if (LinkedNode == nullptr || !FilterResult.ShouldInclude(LinkedNode))
            {
                continue;
            }

            TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
            Link->SetStringField(
                TEXT("from_node_id"),
                Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
            Link->SetStringField(
                TEXT("from_pin_id"),
                Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
            Link->SetStringField(
                TEXT("to_node_id"),
                LinkedNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
            Link->SetStringField(
                TEXT("to_pin_id"),
                LinkedPin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
            Links.Add(MakeShared<FJsonValueObject>(Link));
        }
    }
}
}
