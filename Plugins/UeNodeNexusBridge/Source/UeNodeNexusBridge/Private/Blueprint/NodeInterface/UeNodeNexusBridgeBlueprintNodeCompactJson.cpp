#include "UeNodeNexusBridgeBlueprintNodeCompactJson.h"

#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "UeNodeNexusBridgeBlueprintNodeInterfaceOps.h"
#include "Blueprint/UeNodeNexusBridgeBlueprintPinDefaults.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonValueArray> MakeNullLinkRow(const FString& PinName)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(PinName));
    Row.Add(MakeShared<FJsonValueNull>());
    return MakeShared<FJsonValueArray>(Row);
}

static TSharedPtr<FJsonValueArray> MakeLinkRow(const FString& PinName, const FString& LinkedNode, const FString& LinkedPin)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(PinName));
    Row.Add(MakeShared<FJsonValueString>(LinkedNode));
    Row.Add(MakeShared<FJsonValueString>(LinkedPin));
    return MakeShared<FJsonValueArray>(Row);
}

static TSharedPtr<FJsonValueArray> MakeParamRow(const FString& Name, const FString& Value)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Name));
    Row.Add(MakeShared<FJsonValueString>(Value));
    return MakeShared<FJsonValueArray>(Row);
}

TArray<TSharedPtr<FJsonValue>> BuildBlueprintCompactInputRows(UEdGraph* Graph, UEdGraphNode* Node)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    if (Node == nullptr)
    {
        return Rows;
    }

    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin == nullptr || Pin->Direction != EGPD_Input)
        {
            continue;
        }

        if (Pin->LinkedTo.Num() == 0)
        {
            Rows.Add(MakeNullLinkRow(Pin->PinName.ToString()));
            continue;
        }
        for (UEdGraphPin* Linked : Pin->LinkedTo)
        {
            UEdGraphNode* Other = Linked ? Linked->GetOwningNode() : nullptr;
            if (Other != nullptr)
            {
                Rows.Add(MakeLinkRow(Pin->PinName.ToString(), BlueprintNodeAlias(Graph, Other), Linked->PinName.ToString()));
            }
        }
    }
    return Rows;
}

TArray<TSharedPtr<FJsonValue>> BuildBlueprintCompactParamRows(UEdGraphNode* Node)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    if (Node == nullptr)
    {
        return Rows;
    }

    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin != nullptr && Pin->Direction == EGPD_Input)
        {
            Rows.Add(MakeParamRow(Pin->PinName.ToString(), BlueprintPinDefaultText(Pin)));
        }
    }
    return Rows;
}

TArray<TSharedPtr<FJsonValue>> BuildBlueprintCompactOutputRows(UEdGraph* Graph, UEdGraphNode* Node)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    if (Node == nullptr)
    {
        return Rows;
    }

    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin == nullptr || Pin->Direction != EGPD_Output)
        {
            continue;
        }

        for (UEdGraphPin* Linked : Pin->LinkedTo)
        {
            UEdGraphNode* Other = Linked ? Linked->GetOwningNode() : nullptr;
            if (Other != nullptr)
            {
                Rows.Add(MakeLinkRow(Pin->PinName.ToString(), BlueprintNodeAlias(Graph, Other), Linked->PinName.ToString()));
            }
        }
    }
    return Rows;
}
}
