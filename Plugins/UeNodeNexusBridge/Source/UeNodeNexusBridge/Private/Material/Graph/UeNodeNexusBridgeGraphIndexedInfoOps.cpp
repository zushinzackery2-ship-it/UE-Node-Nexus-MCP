#include "UeNodeNexusBridgeGraphIndexedInfoOps.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "UeNodeNexusBridgeJson.h"
#include "NodeInterface/UeNodeNexusBridgeMaterialNodeInterfaceShared.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "NodeInterface/UeNodeNexusBridgeMaterialNodeInterfaceOps.h"
#include "UeNodeNexusBridgeMaterialNodeFilter.h"

namespace UeNodeNexusBridge
{
static FString MaterialPinToken(UMaterialExpression* Expression, bool bInput, int32 Index)
{
    if (bInput)
    {
        return EscapeIndexedToken(Expression->GetInputName(Index).ToString());
    }
    return EscapeIndexedToken(MaterialOutputName(Expression, Index));
}

TSharedPtr<FJsonObject> BuildMaterialGraphIndexedData(UMaterial* Material, const TSharedPtr<FJsonObject>& Payload, bool bWithPosition)
{
    const int32 MaxNodes = ReadIndexedMaxNodes(Payload);
    const bool bRealIds = WantsRealIds(Payload);
    const TConstArrayView<TObjectPtr<UMaterialExpression>> AllExpressions = Material->GetExpressions();
    int32 Matched = 0;
    const TArray<UMaterialExpression*> Expressions = FilterMaterialNodes(Material, AllExpressions, MaxNodes, Payload, Matched);

    TMap<UMaterialExpression*, int32> NodeIndices;
    for (int32 Index = 0; Index < Expressions.Num(); ++Index)
    {
        NodeIndices.Add(Expressions[Index], Index);
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

    for (int32 NodeIndex = 0; NodeIndex < Expressions.Num(); ++NodeIndex)
    {
        UMaterialExpression* Expression = Expressions[NodeIndex];
        const int32 TypeIndex = DictIndex(TypeDict, Types, ShortMaterialExpressionClass(Expression));
        NodeRows.Add(FString::Printf(TEXT("%d:%d:%s"), NodeIndex, TypeIndex, *EscapeIndexedToken(MaterialNodeAlias(Material, Expression))));

        TArray<FString> NodeValues;
        for (const TSharedPtr<FJsonValue>& Value : BuildMaterialExpressionParams(Expression))
        {
            const TSharedPtr<FJsonObject> Param = Value->AsObject();
            const FString ParamValue = Param->GetStringField(TEXT("value"));
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

        for (FExpressionInputIterator It{ Expression }; It; ++It)
        {
            if (It.Input == nullptr || It.Input->Expression == nullptr)
            {
                continue;
            }
            const int32* SourceNodeIndex = NodeIndices.Find(It.Input->Expression);
            if (SourceNodeIndex == nullptr)
            {
                continue;
            }
            EdgeRows.Add(FString::Printf(TEXT("%d.%s>%d.%s"), *SourceNodeIndex, *MaterialPinToken(It.Input->Expression, false, It.Input->OutputIndex), NodeIndex, *MaterialPinToken(Expression, true, It.Index)));
        }

        if (bWithPosition)
        {
            PositionRows.Add(FString::Printf(TEXT("%d=%d,%d"), NodeIndex, Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY));
        }
        if (bRealIds)
        {
            RealIdRows.Add(FString::Printf(TEXT("%d=%s"), NodeIndex, *MaterialExpressionNodeId(Expression)));
        }
    }

    const bool bIncludeOutput = MatchesMaterialOutputKeyword(Payload) && (MaxNodes == 0 || Expressions.Num() < MaxNodes);
    for (EMaterialProperty Property : MaterialOutputProperties())
    {
        if (!bIncludeOutput)
        {
            break;
        }
        FExpressionInput* Input = Material->GetExpressionInputForProperty(Property);
        if (Input == nullptr || Input->Expression == nullptr)
        {
            continue;
        }
        const int32* SourceNodeIndex = NodeIndices.Find(Input->Expression);
        if (SourceNodeIndex == nullptr)
        {
            continue;
        }
        EdgeRows.Add(FString::Printf(TEXT("%d.%s>MaterialOutput.%s"), *SourceNodeIndex, *MaterialPinToken(Input->Expression, false, Input->OutputIndex), *EscapeIndexedToken(MaterialOutputPropertyName(Property))));
    }

    FString Text = FString::Printf(TEXT("G:%s|material|MaterialGraph|%d\n"), *EscapeIndexedToken(Material->GetPathName()), AllExpressions.Num() + 1);
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
    Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("material"));
    Data->SetStringField(TEXT("graph_name"), TEXT("MaterialGraph"));
    Data->SetNumberField(TEXT("total_nodes"), AllExpressions.Num() + 1);
    Data->SetNumberField(TEXT("returned_nodes"), Expressions.Num() + (bIncludeOutput ? 1 : 0));
    Data->SetBoolField(TEXT("truncated"), Expressions.Num() + (bIncludeOutput ? 1 : 0) < Matched + (MatchesMaterialOutputKeyword(Payload) ? 1 : 0));
    SetTextPayload(Data, Text);
    return Data;
}

TSharedPtr<FJsonObject> BuildMaterialFunctionGraphIndexedData(UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload, bool bWithPosition)
{
    const int32 MaxNodes = ReadIndexedMaxNodes(Payload);
    const bool bRealIds = WantsRealIds(Payload);
    const TConstArrayView<TObjectPtr<UMaterialExpression>> AllExpressions = Function->GetExpressions();
    int32 Matched = 0;
    const TArray<UMaterialExpression*> Expressions = FilterMaterialNodes(Function, AllExpressions, MaxNodes, Payload, Matched);

    TMap<UMaterialExpression*, int32> NodeIndices;
    for (int32 Index = 0; Index < Expressions.Num(); ++Index)
    {
        NodeIndices.Add(Expressions[Index], Index);
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

    for (int32 NodeIndex = 0; NodeIndex < Expressions.Num(); ++NodeIndex)
    {
        UMaterialExpression* Expression = Expressions[NodeIndex];
        const int32 TypeIndex = DictIndex(TypeDict, Types, ShortMaterialExpressionClass(Expression));
        NodeRows.Add(FString::Printf(TEXT("%d:%d:%s"), NodeIndex, TypeIndex, *EscapeIndexedToken(MaterialNodeAlias(Function, Expression))));

        TArray<FString> NodeValues;
        for (const TSharedPtr<FJsonValue>& Value : BuildMaterialExpressionParams(Expression))
        {
            const TSharedPtr<FJsonObject> Param = Value->AsObject();
            const FString ParamValue = Param->GetStringField(TEXT("value"));
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

        for (FExpressionInputIterator It{ Expression }; It; ++It)
        {
            if (It.Input == nullptr || It.Input->Expression == nullptr)
            {
                continue;
            }
            const int32* SourceNodeIndex = NodeIndices.Find(It.Input->Expression);
            if (SourceNodeIndex == nullptr)
            {
                continue;
            }
            EdgeRows.Add(FString::Printf(TEXT("%d.%s>%d.%s"), *SourceNodeIndex, *MaterialPinToken(It.Input->Expression, false, It.Input->OutputIndex), NodeIndex, *MaterialPinToken(Expression, true, It.Index)));
        }

        if (bWithPosition)
        {
            PositionRows.Add(FString::Printf(TEXT("%d=%d,%d"), NodeIndex, Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY));
        }
        if (bRealIds)
        {
            RealIdRows.Add(FString::Printf(TEXT("%d=%s"), NodeIndex, *MaterialExpressionNodeId(Expression)));
        }
    }

    FString Text = FString::Printf(TEXT("G:%s|material_function|MaterialFunctionGraph|%d\n"), *EscapeIndexedToken(Function->GetPathName()), AllExpressions.Num());
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
    Data->SetStringField(TEXT("asset_path"), Function->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("material_function"));
    Data->SetStringField(TEXT("graph_name"), TEXT("MaterialFunctionGraph"));
    Data->SetNumberField(TEXT("total_nodes"), AllExpressions.Num());
    Data->SetNumberField(TEXT("returned_nodes"), Expressions.Num());
    Data->SetBoolField(TEXT("truncated"), Expressions.Num() < Matched);
    SetTextPayload(Data, Text);
    return Data;
}
}
