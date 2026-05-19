#include "UeNodeNexusBridgeMaterialGraphSnapshot.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "UeNodeNexusBridgeCompactGraph.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "UeNodeNexusBridgeWireGraph.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
static FString MaterialNodeId(const UMaterialExpression* Expression)
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

static TSharedPtr<FJsonObject> MaterialNodeToJson(UMaterialExpression* Expression, bool bIncludeNodeParams)
{
    TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
    const FString NodeId = MaterialNodeId(Expression);
    Node->SetStringField(TEXT("node_id"), NodeId);
    Node->SetStringField(TEXT("class_name"), Expression->GetClass()->GetPathName());
    Node->SetStringField(TEXT("display_name"), Expression->GetName());

    TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
    Position->SetNumberField(TEXT("x"), Expression->MaterialExpressionEditorX);
    Position->SetNumberField(TEXT("y"), Expression->MaterialExpressionEditorY);
    Node->SetObjectField(TEXT("position"), Position);

    TArray<TSharedPtr<FJsonValue>> Pins;
    for (int32 Index = 0; FExpressionInput* Input = Expression->GetInput(Index); ++Index)
    {
        Pins.Add(MakeShared<FJsonValueObject>(MaterialPinToJson(FString::Printf(TEXT("%s:in:%d"), *NodeId, Index), Expression->GetInputName(Index).ToString(), TEXT("input"))));
    }
    TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
    for (int32 Index = 0; Index < Outputs.Num(); ++Index)
    {
        const FString OutputName = Outputs[Index].OutputName.IsNone() ? FString::FromInt(Index) : Outputs[Index].OutputName.ToString();
        Pins.Add(MakeShared<FJsonValueObject>(MaterialPinToJson(FString::Printf(TEXT("%s:out:%d"), *NodeId, Index), OutputName, TEXT("output"))));
    }

    Node->SetArrayField(TEXT("pins"), Pins);
    Node->SetArrayField(TEXT("params"), bIncludeNodeParams ? BuildMaterialExpressionParams(Expression) : TArray<TSharedPtr<FJsonValue>>());
    return Node;
}

static void AddMaterialLinks(UMaterialExpression* TargetExpression, TArray<TSharedPtr<FJsonValue>>& Links)
{
    const FString TargetId = MaterialNodeId(TargetExpression);
    for (int32 Index = 0; FExpressionInput* Input = TargetExpression->GetInput(Index); ++Index)
    {
        if (Input == nullptr || Input->Expression == nullptr)
        {
            continue;
        }

        const FString SourceId = MaterialNodeId(Input->Expression);
        TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
        Link->SetStringField(TEXT("from_node_id"), SourceId);
        Link->SetStringField(TEXT("from_pin_id"), FString::Printf(TEXT("%s:out:%d"), *SourceId, Input->OutputIndex));
        Link->SetStringField(TEXT("to_node_id"), TargetId);
        Link->SetStringField(TEXT("to_pin_id"), FString::Printf(TEXT("%s:in:%d"), *TargetId, Index));
        Links.Add(MakeShared<FJsonValueObject>(Link));
    }
}

static void AppendMaterialCompactNode(FCompactGraphBuilder& Builder, UMaterialExpression* Expression, bool bIncludeNodeParams)
{
    const FString NodeId = MaterialNodeId(Expression);
    Builder.AddNode(NodeId, Expression->GetClass()->GetName(), Expression->GetName(), Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY);
    for (int32 Index = 0; FExpressionInput* Input = Expression->GetInput(Index); ++Index)
    {
        Builder.AddPin(FString::Printf(TEXT("%s:in:%d"), *NodeId, Index), NodeId, TEXT("input"), Expression->GetInputName(Index).ToString(), TEXT("material"), FString());
    }

    TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
    for (int32 Index = 0; Index < Outputs.Num(); ++Index)
    {
        const FString OutputName = Outputs[Index].OutputName.IsNone() ? FString::FromInt(Index) : Outputs[Index].OutputName.ToString();
        Builder.AddPin(FString::Printf(TEXT("%s:out:%d"), *NodeId, Index), NodeId, TEXT("output"), OutputName, TEXT("material"), FString());
    }

    if (bIncludeNodeParams)
    {
        for (const TSharedPtr<FJsonValue>& ParamValue : BuildMaterialExpressionParams(Expression))
        {
            Builder.AddParam(NodeId, ParamValue->AsObject());
        }
    }
}

static void AddMaterialCompactLinks(FCompactGraphBuilder& Builder, UMaterialExpression* TargetExpression)
{
    const FString TargetId = MaterialNodeId(TargetExpression);
    for (int32 Index = 0; FExpressionInput* Input = TargetExpression->GetInput(Index); ++Index)
    {
        if (Input != nullptr && Input->Expression != nullptr)
        {
            const FString SourceId = MaterialNodeId(Input->Expression);
            Builder.AddLink(FString::Printf(TEXT("%s:out:%d"), *SourceId, Input->OutputIndex), FString::Printf(TEXT("%s:in:%d"), *TargetId, Index));
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

    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Material->GetExpressions())
    {
        if (UMaterialExpression* Expression = ExpressionPtr.Get())
        {
            AppendMaterialCompactNode(Builder, Expression, bIncludeNodeParams);
            if (bIncludeLinks)
            {
                AddMaterialCompactLinks(Builder, Expression);
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

        const FString TargetId = MaterialNodeId(Expression);
        const FString TargetNode = MakeWireGraphNodeLabel(Expression->GetClass()->GetName(), MaterialExpressionDisplayName(Expression));
        for (int32 Index = 0; FExpressionInput* Input = Expression->GetInput(Index); ++Index)
        {
            if (Input == nullptr || Input->Expression == nullptr)
            {
                continue;
            }

            UMaterialExpression* SourceExpression = Input->Expression;
            const FString SourceId = MaterialNodeId(SourceExpression);
            const FString SourceNode = MakeWireGraphNodeLabel(SourceExpression->GetClass()->GetName(), MaterialExpressionDisplayName(SourceExpression));
            TArray<FExpressionOutput>& Outputs = SourceExpression->GetOutputs();
            const FString SourcePin = Outputs.IsValidIndex(Input->OutputIndex) && !Outputs[Input->OutputIndex].OutputName.IsNone() ? Outputs[Input->OutputIndex].OutputName.ToString() : FString::FromInt(Input->OutputIndex);
            Builder.AddWire(SourceNode, SourcePin, SourceId, TargetNode, Expression->GetInputName(Index).ToString(), TargetId);
        }
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    const FString Text = bTiny ? Builder.BuildTinyText() : (bMin ? Builder.BuildMinText() : Builder.BuildText());
    const FString OutputFormat = bTiny ? TEXT("wires_tiny_v1") : (bMin ? TEXT("wires_min_v1") : TEXT("wires_text_v1"));
    Response->SetObjectField(TEXT("data"), MakeWireGraphData(Material->GetPathName(), Material->GetClass()->GetPathName(), TEXT("MaterialGraph"), TEXT("material"), Text, OutputFormat));
    return Response;
}

static TSharedPtr<FJsonObject> BuildMaterialFullSnapshot(const FString& Operation, const FString& RequestId, UMaterial* Material, bool bIncludeNodeParams, bool bIncludeLinks)
{
    TArray<TSharedPtr<FJsonValue>> Nodes;
    TArray<TSharedPtr<FJsonValue>> Links;
    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Material->GetExpressions())
    {
        if (UMaterialExpression* Expression = ExpressionPtr.Get())
        {
            Nodes.Add(MakeShared<FJsonValueObject>(MaterialNodeToJson(Expression, bIncludeNodeParams)));
            if (bIncludeLinks)
            {
                AddMaterialLinks(Expression, Links);
            }
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
    Data->SetStringField(TEXT("asset_class"), Material->GetClass()->GetPathName());
    Data->SetStringField(TEXT("graph_name"), TEXT("MaterialGraph"));
    Data->SetStringField(TEXT("graph_kind"), TEXT("material"));
    Data->SetArrayField(TEXT("nodes"), Nodes);
    Data->SetArrayField(TEXT("links"), Links);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> BuildMaterialGraphSnapshot(const FString& Operation, const FString& RequestId, UMaterial* Material, bool bIncludeNodeParams, bool bIncludeLinks, bool bCompact, bool bWire, bool bWireMin, bool bWireTiny)
{
    if (bWire)
    {
        return BuildMaterialWireSnapshot(Operation, RequestId, Material, bIncludeLinks, bWireMin, bWireTiny);
    }
    if (bCompact)
    {
        return BuildMaterialCompactSnapshot(Operation, RequestId, Material, bIncludeNodeParams, bIncludeLinks);
    }
    return BuildMaterialFullSnapshot(Operation, RequestId, Material, bIncludeNodeParams, bIncludeLinks);
}
}
