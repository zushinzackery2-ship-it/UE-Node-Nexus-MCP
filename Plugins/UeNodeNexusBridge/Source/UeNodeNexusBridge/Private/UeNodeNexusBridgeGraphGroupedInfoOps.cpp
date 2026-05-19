#include "UeNodeNexusBridgeGraphGroupedInfoOps.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeGraphIndexedInfoOps.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialNodeInterfaceShared.h"
#include "UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "UeNodeNexusBridgeNodeInterfaceOps.h"

namespace UeNodeNexusBridge
{
static FString JoinOrNone(const TArray<FString>& Items)
{
    return Items.Num() == 0 ? TEXT("none") : FString::Join(Items, TEXT("|"));
}

static void AddGroupedRow(TMap<FString, TArray<FString>>& Groups, TArray<FString>& Order, const FString& Type, const FString& Row)
{
    if (!Groups.Contains(Type))
    {
        Order.Add(Type);
    }
    Groups.FindOrAdd(Type).Add(Row);
}

static FString GroupedParamText(const TArray<TSharedPtr<FJsonValue>>& Params, const FString& ValueField)
{
    TArray<FString> Pairs;
    for (const TSharedPtr<FJsonValue>& Value : Params)
    {
        const TSharedPtr<FJsonObject> Param = Value->AsObject();
        const FString ParamValue = Param->GetStringField(ValueField);
        if (!ParamValue.IsEmpty())
        {
            Pairs.Add(FString::Printf(TEXT("-%s=%s"), *EscapeIndexedToken(Param->GetStringField(TEXT("name"))), *EscapeIndexedToken(ParamValue)));
        }
    }
    return FString::Printf(TEXT("p[%s]"), *JoinOrNone(Pairs));
}

static FString MaterialGroupedInputs(UMaterial* Material, UMaterialExpression* Expression)
{
    TArray<FString> Inputs;
    for (FExpressionInputIterator It{ Expression }; It; ++It)
    {
        const FString InputName = EscapeIndexedToken(Expression->GetInputName(It.Index).ToString());
        if (It.Input != nullptr && It.Input->Expression != nullptr)
        {
            Inputs.Add(FString::Printf(TEXT("%s<%s.%s"), *InputName, *EscapeIndexedToken(MaterialNodeAlias(Material, It.Input->Expression)), *EscapeIndexedToken(MaterialOutputName(It.Input->Expression, It.Input->OutputIndex))));
        }
        else
        {
            Inputs.Add(FString::Printf(TEXT("%s<None"), *InputName));
        }
    }
    return FString::Printf(TEXT("i[%s]"), *JoinOrNone(Inputs));
}

static FString BlueprintGroupedInputs(UEdGraph* Graph, UEdGraphNode* Node)
{
    TArray<FString> Inputs;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin == nullptr || Pin->Direction != EGPD_Input)
        {
            continue;
        }

        TArray<FString> Links;
        for (UEdGraphPin* Linked : Pin->LinkedTo)
        {
            UEdGraphNode* Other = Linked ? Linked->GetOwningNode() : nullptr;
            if (Other != nullptr)
            {
                Links.Add(FString::Printf(TEXT("%s.%s"), *EscapeIndexedToken(BlueprintNodeAlias(Graph, Other)), *EscapeIndexedToken(Linked->PinName.ToString())));
            }
        }
        Inputs.Add(FString::Printf(TEXT("%s<%s"), *EscapeIndexedToken(Pin->PinName.ToString()), Links.Num() == 0 ? TEXT("None") : *FString::Join(Links, TEXT("+"))));
    }
    return FString::Printf(TEXT("i[%s]"), *JoinOrNone(Inputs));
}

static FString BuildGroupedText(const FString& Header, const TArray<FString>& Order, const TMap<FString, TArray<FString>>& Groups)
{
    FString Text = Header;
    for (const FString& Type : Order)
    {
        const TArray<FString>* Rows = Groups.Find(Type);
        if (Rows != nullptr)
        {
            Text += FString::Printf(TEXT("%s:%s\n"), *EscapeIndexedToken(Type), *FString::Join(*Rows, TEXT(";")));
        }
    }
    return Text;
}

TSharedPtr<FJsonObject> BuildMaterialGraphGroupedData(UMaterial* Material, const TSharedPtr<FJsonObject>& Payload, bool bWithPosition)
{
    const int32 MaxNodes = ReadIndexedMaxNodes(Payload);
    const bool bRealIds = WantsRealIds(Payload);
    const TArrayView<const TObjectPtr<UMaterialExpression>> AllExpressions = Material->GetExpressions();
    TMap<FString, TArray<FString>> Groups;
    TArray<FString> Order;
    int32 ReturnedNodes = 0;

    for (TObjectPtr<UMaterialExpression> ExpressionPtr : AllExpressions)
    {
        UMaterialExpression* Expression = ExpressionPtr.Get();
        if (Expression == nullptr || (MaxNodes > 0 && ReturnedNodes >= MaxNodes))
        {
            continue;
        }

        TArray<FString> Parts = {
            GroupedParamText(BuildMaterialExpressionParams(Expression), TEXT("value")),
            MaterialGroupedInputs(Material, Expression)
        };
        if (bWithPosition)
        {
            Parts.Add(FString::Printf(TEXT("x[%d,%d]"), Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY));
        }
        if (bRealIds)
        {
            Parts.Add(FString::Printf(TEXT("r[%s]"), *MaterialExpressionNodeId(Expression)));
        }

        AddGroupedRow(Groups, Order, ShortMaterialExpressionClass(Expression), FString::Printf(TEXT("%s{%s}"), *EscapeIndexedToken(MaterialNodeAlias(Material, Expression)), *FString::Join(Parts, TEXT(";"))));
        ++ReturnedNodes;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), bWithPosition ? TEXT("graph_node_grouped_w_pos") : TEXT("graph_node_grouped"));
    Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("material"));
    Data->SetStringField(TEXT("graph_name"), TEXT("MaterialGraph"));
    Data->SetNumberField(TEXT("total_nodes"), AllExpressions.Num());
    Data->SetNumberField(TEXT("returned_nodes"), ReturnedNodes);
    Data->SetBoolField(TEXT("truncated"), ReturnedNodes < AllExpressions.Num());
    SetTextPayload(Data, BuildGroupedText(FString::Printf(TEXT("G:%s|material|MaterialGraph|%d\n"), *EscapeIndexedToken(Material->GetPathName()), AllExpressions.Num()), Order, Groups));
    return Data;
}

TSharedPtr<FJsonObject> BuildBlueprintGraphGroupedData(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Payload, bool bWithPosition)
{
    const int32 MaxNodes = ReadIndexedMaxNodes(Payload);
    const bool bRealIds = WantsRealIds(Payload);
    TMap<FString, TArray<FString>> Groups;
    TArray<FString> Order;
    int32 ReturnedNodes = 0;

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr || (MaxNodes > 0 && ReturnedNodes >= MaxNodes))
        {
            continue;
        }

        TArray<FString> Parts = {
            GroupedParamText(BuildBlueprintNodeParams(Node), TEXT("default_value")),
            BlueprintGroupedInputs(Graph, Node)
        };
        if (bWithPosition)
        {
            Parts.Add(FString::Printf(TEXT("x[%d,%d]"), Node->NodePosX, Node->NodePosY));
        }
        if (bRealIds)
        {
            Parts.Add(FString::Printf(TEXT("r[%s]"), *Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens)));
        }

        AddGroupedRow(Groups, Order, ShortBlueprintNodeClass(Node), FString::Printf(TEXT("%s{%s}"), *EscapeIndexedToken(BlueprintNodeAlias(Graph, Node)), *FString::Join(Parts, TEXT(";"))));
        ++ReturnedNodes;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), bWithPosition ? TEXT("graph_node_grouped_w_pos") : TEXT("graph_node_grouped"));
    Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("blueprint"));
    Data->SetStringField(TEXT("graph_name"), Graph->GetName());
    Data->SetNumberField(TEXT("total_nodes"), Graph->Nodes.Num());
    Data->SetNumberField(TEXT("returned_nodes"), ReturnedNodes);
    Data->SetBoolField(TEXT("truncated"), ReturnedNodes < Graph->Nodes.Num());
    SetTextPayload(Data, BuildGroupedText(FString::Printf(TEXT("G:%s|blueprint|%s|%d\n"), *EscapeIndexedToken(Blueprint->GetPathName()), *EscapeIndexedToken(Graph->GetName()), Graph->Nodes.Num()), Order, Groups));
    return Data;
}
}
