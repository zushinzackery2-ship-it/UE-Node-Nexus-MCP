#include "UeNodeNexusBridgeMaterialGraphSnapshot.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "UeNodeNexusBridgeCompactGraph.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialGraphSnapshotShared.h"
#include "UeNodeNexusBridgeMaterialPropertySchema.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "UeNodeNexusBridgeWireGraph.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
FString MaterialSnapshotNodeId(const UMaterialExpression* Expression)
{
    return Expression ? const_cast<UMaterialExpression*>(Expression)->GetMaterialExpressionId().ToString(EGuidFormats::DigitsWithHyphens) : FString();
}

static TSharedPtr<FJsonObject> MaterialPinToJson(const FString& PinId, const FString& Name, const FString& Direction)
{
    TSharedPtr<FJsonObject> Pin = MakeShared<FJsonObject>();
    Pin->SetStringField(TEXT("pin_id"), PinId);
    Pin->SetStringField(TEXT("name"), Name);
    Pin->SetStringField(TEXT("direction"), Direction);
    Pin->SetStringField(TEXT("type"), TEXT("material"));
    Pin->SetArrayField(TEXT("linked_to"), TArray<TSharedPtr<FJsonValue>>());
    return Pin;
}

static FString ReadExpressionPropertyText(UMaterialExpression* Expression, const FName& PropertyName)
{
    FProperty* Property = Expression ? Expression->GetClass()->FindPropertyByName(PropertyName) : nullptr;
    if (Property == nullptr)
    {
        return FString();
    }

    FString Value;
    Property->ExportText_InContainer(0, Value, Expression, nullptr, Expression, PPF_None);
    Value.RemoveFromStart(TEXT("("));
    Value.RemoveFromEnd(TEXT(")"));
    return Value;
}

static FString MaterialExpressionDisplayName(UMaterialExpression* Expression)
{
    const FString ParameterName = ReadExpressionPropertyText(Expression, TEXT("ParameterName"));
    return !ParameterName.IsEmpty() && !ParameterName.Equals(TEXT("None"), ESearchCase::IgnoreCase) ? ParameterName : FString();
}

FString MaterialSnapshotFunctionExpressionDisplayName(UMaterialExpression* Expression)
{
    const FString InputName = ReadExpressionPropertyText(Expression, TEXT("InputName"));
    if (!InputName.IsEmpty() && !InputName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
    {
        return InputName;
    }

    const FString OutputName = ReadExpressionPropertyText(Expression, TEXT("OutputName"));
    if (!OutputName.IsEmpty() && !OutputName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
    {
        return OutputName;
    }

    return MaterialExpressionDisplayName(Expression);
}

static TArray<TSharedPtr<FJsonValue>> BuildMaterialSnapshotNodeParams(UMaterialExpression* Expression, bool bIncludeNodeParams, EMaterialSnapshotNodeParamsFormat NodeParamsFormat)
{
    if (!bIncludeNodeParams)
    {
        return TArray<TSharedPtr<FJsonValue>>();
    }
    return NodeParamsFormat == EMaterialSnapshotNodeParamsFormat::Full
        ? BuildMaterialExpressionParams(Expression)
        : BuildMaterialExpressionParamValues(Expression);
}

TSharedPtr<FJsonObject> MaterialSnapshotNodeToJson(UMaterialExpression* Expression, bool bIncludeNodeParams, EMaterialSnapshotNodeParamsFormat NodeParamsFormat)
{
    TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
    const FString NodeId = MaterialSnapshotNodeId(Expression);
    Node->SetStringField(TEXT("node_id"), NodeId);
    Node->SetStringField(TEXT("class_name"), Expression->GetClass()->GetPathName());
    Node->SetStringField(TEXT("display_name"), Expression->GetName());

    TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
    Position->SetNumberField(TEXT("x"), Expression->MaterialExpressionEditorX);
    Position->SetNumberField(TEXT("y"), Expression->MaterialExpressionEditorY);
    Node->SetObjectField(TEXT("position"), Position);

    TArray<TSharedPtr<FJsonValue>> Pins;
    for (FExpressionInputIterator It{ Expression }; It; ++It)
    {
        Pins.Add(MakeShared<FJsonValueObject>(MaterialPinToJson(FString::Printf(TEXT("%s:in:%d"), *NodeId, It.Index), Expression->GetInputName(It.Index).ToString(), TEXT("input"))));
    }
    TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
    for (int32 Index = 0; Index < Outputs.Num(); ++Index)
    {
        const FString OutputName = Outputs[Index].OutputName.IsNone() ? FString::FromInt(Index) : Outputs[Index].OutputName.ToString();
        Pins.Add(MakeShared<FJsonValueObject>(MaterialPinToJson(FString::Printf(TEXT("%s:out:%d"), *NodeId, Index), OutputName, TEXT("output"))));
    }

    Node->SetArrayField(TEXT("pins"), Pins);
    Node->SetArrayField(TEXT("params"), BuildMaterialSnapshotNodeParams(Expression, bIncludeNodeParams, NodeParamsFormat));
    return Node;
}

void AddMaterialSnapshotLinks(UMaterialExpression* TargetExpression, TArray<TSharedPtr<FJsonValue>>& Links)
{
    const FString TargetId = MaterialSnapshotNodeId(TargetExpression);
    for (FExpressionInputIterator It{ TargetExpression }; It; ++It)
    {
        FExpressionInput* Input = It.Input;
        if (Input == nullptr || Input->Expression == nullptr)
        {
            continue;
        }

        const FString SourceId = MaterialSnapshotNodeId(Input->Expression);
        TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
        Link->SetStringField(TEXT("from_node_id"), SourceId);
        Link->SetStringField(TEXT("from_pin_id"), FString::Printf(TEXT("%s:out:%d"), *SourceId, Input->OutputIndex));
        Link->SetStringField(TEXT("to_node_id"), TargetId);
        Link->SetStringField(TEXT("to_pin_id"), FString::Printf(TEXT("%s:in:%d"), *TargetId, It.Index));
        Links.Add(MakeShared<FJsonValueObject>(Link));
    }
}

void AppendMaterialSnapshotCompactNode(FCompactGraphBuilder& Builder, UMaterialExpression* Expression, bool bIncludeNodeParams)
{
    const FString NodeId = MaterialSnapshotNodeId(Expression);
    Builder.AddNode(NodeId, Expression->GetClass()->GetName(), Expression->GetName(), Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY);
    for (FExpressionInputIterator It{ Expression }; It; ++It)
    {
        Builder.AddPin(FString::Printf(TEXT("%s:in:%d"), *NodeId, It.Index), NodeId, TEXT("input"), Expression->GetInputName(It.Index).ToString(), TEXT("material"), FString());
    }

    TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
    for (int32 Index = 0; Index < Outputs.Num(); ++Index)
    {
        const FString OutputName = Outputs[Index].OutputName.IsNone() ? FString::FromInt(Index) : Outputs[Index].OutputName.ToString();
        Builder.AddPin(FString::Printf(TEXT("%s:out:%d"), *NodeId, Index), NodeId, TEXT("output"), OutputName, TEXT("material"), FString());
    }

    if (bIncludeNodeParams)
    {
        for (const TSharedPtr<FJsonValue>& ParamValue : BuildMaterialExpressionParamValues(Expression))
        {
            Builder.AddParam(NodeId, ParamValue->AsObject());
        }
    }
}

void AddMaterialSnapshotCompactLinks(FCompactGraphBuilder& Builder, UMaterialExpression* TargetExpression)
{
    const FString TargetId = MaterialSnapshotNodeId(TargetExpression);
    for (FExpressionInputIterator It{ TargetExpression }; It; ++It)
    {
        FExpressionInput* Input = It.Input;
        if (Input != nullptr && Input->Expression != nullptr)
        {
            const FString SourceId = MaterialSnapshotNodeId(Input->Expression);
            Builder.AddLink(FString::Printf(TEXT("%s:out:%d"), *SourceId, Input->OutputIndex), FString::Printf(TEXT("%s:in:%d"), *TargetId, It.Index));
        }
    }
}

static TSharedPtr<FJsonObject> BuildMaterialCompactSnapshot(const FString& Operation, const FString& RequestId, UMaterial* Material, bool bIncludeNodeParams, bool bIncludeLinks)
{
    FCompactGraphBuilder Builder;
    Builder.Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
    Builder.Data->SetStringField(TEXT("asset_class"), Material->GetClass()->GetPathName());
    Builder.Data->SetStringField(TEXT("graph_name"), TEXT("MaterialGraph"));
    Builder.Data->SetStringField(TEXT("graph_kind"), TEXT("material"));
    Builder.Data->SetStringField(TEXT("node_params_format"), bIncludeNodeParams ? TEXT("compact") : TEXT("none"));

    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Material->GetExpressions())
    {
        if (UMaterialExpression* Expression = ExpressionPtr.Get())
        {
            AppendMaterialSnapshotCompactNode(Builder, Expression, bIncludeNodeParams);
            if (bIncludeLinks)
            {
                AddMaterialSnapshotCompactLinks(Builder, Expression);
            }
        }
    }
    Builder.FinalizeIds();

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Builder.Data);
    return Response;
}

static TSharedPtr<FJsonObject> BuildMaterialWireSnapshot(const FString& Operation, const FString& RequestId, UMaterial* Material, bool bIncludeLinks, bool bMin, bool bTiny)
{
    FWireGraphBuilder Builder(TEXT("Wire graph"));
    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Material->GetExpressions())
    {
        UMaterialExpression* Expression = ExpressionPtr.Get();
        if (Expression == nullptr)
        {
            continue;
        }

        Builder.AddNodeType(Expression->GetClass()->GetName().Replace(TEXT("MaterialExpression"), TEXT("")));
        if (!bIncludeLinks)
        {
            continue;
        }

        const FString TargetId = MaterialSnapshotNodeId(Expression);
        const FString TargetNode = MakeWireGraphNodeLabel(Expression->GetClass()->GetName(), MaterialExpressionDisplayName(Expression));
        for (FExpressionInputIterator It{ Expression }; It; ++It)
        {
            FExpressionInput* Input = It.Input;
            if (Input == nullptr || Input->Expression == nullptr)
            {
                continue;
            }

            UMaterialExpression* SourceExpression = Input->Expression;
            const FString SourceId = MaterialSnapshotNodeId(SourceExpression);
            const FString SourceNode = MakeWireGraphNodeLabel(SourceExpression->GetClass()->GetName(), MaterialExpressionDisplayName(SourceExpression));
            TArray<FExpressionOutput>& Outputs = SourceExpression->GetOutputs();
            const FString SourcePin = Outputs.IsValidIndex(Input->OutputIndex) && !Outputs[Input->OutputIndex].OutputName.IsNone() ? Outputs[Input->OutputIndex].OutputName.ToString() : FString::FromInt(Input->OutputIndex);
            Builder.AddWire(SourceNode, SourcePin, SourceId, TargetNode, Expression->GetInputName(It.Index).ToString(), TargetId);
        }
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    const FString Text = bTiny ? Builder.BuildTinyText() : (bMin ? Builder.BuildMinText() : Builder.BuildText());
    const FString OutputFormat = bTiny ? TEXT("wires_tiny") : (bMin ? TEXT("wires_min") : TEXT("wires_text"));
    Response->SetObjectField(TEXT("data"), MakeWireGraphData(Material->GetPathName(), Material->GetClass()->GetPathName(), TEXT("MaterialGraph"), TEXT("material"), Text, OutputFormat));
    return Response;
}

static TSharedPtr<FJsonObject> BuildMaterialFullSnapshot(const FString& Operation, const FString& RequestId, UMaterial* Material, bool bIncludeNodeParams, EMaterialSnapshotNodeParamsFormat NodeParamsFormat, bool bIncludeLinks)
{
    TArray<TSharedPtr<FJsonValue>> Nodes;
    TArray<TSharedPtr<FJsonValue>> Links;
    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Material->GetExpressions())
    {
        if (UMaterialExpression* Expression = ExpressionPtr.Get())
        {
            Nodes.Add(MakeShared<FJsonValueObject>(MaterialSnapshotNodeToJson(Expression, bIncludeNodeParams, NodeParamsFormat)));
            if (bIncludeLinks)
            {
                AddMaterialSnapshotLinks(Expression, Links);
            }
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
    Data->SetStringField(TEXT("asset_class"), Material->GetClass()->GetPathName());
    Data->SetStringField(TEXT("graph_name"), TEXT("MaterialGraph"));
    Data->SetStringField(TEXT("graph_kind"), TEXT("material"));
    Data->SetStringField(TEXT("node_params_format"), bIncludeNodeParams ? (NodeParamsFormat == EMaterialSnapshotNodeParamsFormat::Full ? TEXT("full") : TEXT("compact")) : TEXT("none"));
    Data->SetArrayField(TEXT("nodes"), Nodes);
    Data->SetArrayField(TEXT("links"), Links);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> BuildMaterialGraphSnapshot(const FString& Operation, const FString& RequestId, UMaterial* Material, bool bIncludeNodeParams, EMaterialSnapshotNodeParamsFormat NodeParamsFormat, bool bIncludeLinks, bool bCompact, bool bWire, bool bWireMin, bool bWireTiny)
{
    if (bWire)
    {
        return BuildMaterialWireSnapshot(Operation, RequestId, Material, bIncludeLinks, bWireMin, bWireTiny);
    }
    if (bCompact)
    {
        return BuildMaterialCompactSnapshot(Operation, RequestId, Material, bIncludeNodeParams, bIncludeLinks);
    }
    return BuildMaterialFullSnapshot(Operation, RequestId, Material, bIncludeNodeParams, NodeParamsFormat, bIncludeLinks);
}

}
