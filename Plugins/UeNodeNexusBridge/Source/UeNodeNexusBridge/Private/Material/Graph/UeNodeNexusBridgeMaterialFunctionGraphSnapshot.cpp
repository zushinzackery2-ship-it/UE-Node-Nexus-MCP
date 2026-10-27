#include "UeNodeNexusBridgeMaterialGraphSnapshot.h"

#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "UeNodeNexusBridgeCompactGraph.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialGraphSnapshotShared.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "UeNodeNexusBridgeWireGraph.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> BuildMaterialFunctionCompactSnapshot(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, bool bIncludeNodeParams, bool bIncludeLinks)
{
    FCompactGraphBuilder Builder;
    Builder.Data->SetStringField(TEXT("asset_path"), Function->GetPathName());
    Builder.Data->SetStringField(TEXT("asset_class"), Function->GetClass()->GetPathName());
    Builder.Data->SetStringField(TEXT("graph_name"), TEXT("MaterialFunctionGraph"));
    Builder.Data->SetStringField(TEXT("graph_kind"), TEXT("material_function"));
    Builder.Data->SetStringField(TEXT("node_params_format"), bIncludeNodeParams ? TEXT("compact") : TEXT("none"));

    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Function->GetExpressions())
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

static TSharedPtr<FJsonObject> BuildMaterialFunctionWireSnapshot(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, bool bIncludeLinks, bool bMin, bool bTiny)
{
    FWireGraphBuilder Builder(TEXT("Wire graph"));
    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Function->GetExpressions())
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
        const FString TargetNode = MakeWireGraphNodeLabel(Expression->GetClass()->GetName(), MaterialSnapshotFunctionExpressionDisplayName(Expression));
        for (FExpressionInputIterator It{ Expression }; It; ++It)
        {
            FExpressionInput* Input = It.Input;
            if (Input == nullptr || Input->Expression == nullptr)
            {
                continue;
            }

            UMaterialExpression* SourceExpression = Input->Expression;
            const FString SourceId = MaterialSnapshotNodeId(SourceExpression);
            const FString SourceNode = MakeWireGraphNodeLabel(SourceExpression->GetClass()->GetName(), MaterialSnapshotFunctionExpressionDisplayName(SourceExpression));
            TArray<FExpressionOutput>& Outputs = SourceExpression->GetOutputs();
            const FString SourcePin = Outputs.IsValidIndex(Input->OutputIndex) && !Outputs[Input->OutputIndex].OutputName.IsNone() ? Outputs[Input->OutputIndex].OutputName.ToString() : FString::FromInt(Input->OutputIndex);
            Builder.AddWire(SourceNode, SourcePin, SourceId, TargetNode, Expression->GetInputName(It.Index).ToString(), TargetId);
        }
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    const FString Text = bTiny ? Builder.BuildTinyText() : (bMin ? Builder.BuildMinText() : Builder.BuildText());
    const FString OutputFormat = bTiny ? TEXT("wires_tiny") : (bMin ? TEXT("wires_min") : TEXT("wires_text"));
    Response->SetObjectField(TEXT("data"), MakeWireGraphData(Function->GetPathName(), Function->GetClass()->GetPathName(), TEXT("MaterialFunctionGraph"), TEXT("material_function"), Text, OutputFormat));
    return Response;
}

static TSharedPtr<FJsonObject> BuildMaterialFunctionFullSnapshot(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, bool bIncludeNodeParams, EMaterialSnapshotNodeParamsFormat NodeParamsFormat, bool bIncludeLinks)
{
    TArray<TSharedPtr<FJsonValue>> Nodes;
    TArray<TSharedPtr<FJsonValue>> Links;
    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Function->GetExpressions())
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
    Data->SetStringField(TEXT("asset_path"), Function->GetPathName());
    Data->SetStringField(TEXT("asset_class"), Function->GetClass()->GetPathName());
    Data->SetStringField(TEXT("graph_name"), TEXT("MaterialFunctionGraph"));
    Data->SetStringField(TEXT("graph_kind"), TEXT("material_function"));
    Data->SetStringField(TEXT("node_params_format"), bIncludeNodeParams ? (NodeParamsFormat == EMaterialSnapshotNodeParamsFormat::Full ? TEXT("full") : TEXT("compact")) : TEXT("none"));
    Data->SetArrayField(TEXT("nodes"), Nodes);
    Data->SetArrayField(TEXT("links"), Links);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> BuildMaterialFunctionGraphSnapshot(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, bool bIncludeNodeParams, EMaterialSnapshotNodeParamsFormat NodeParamsFormat, bool bIncludeLinks, bool bCompact, bool bWire, bool bWireMin, bool bWireTiny)
{
    if (bWire)
    {
        return BuildMaterialFunctionWireSnapshot(Operation, RequestId, Function, bIncludeLinks, bWireMin, bWireTiny);
    }
    if (bCompact)
    {
        return BuildMaterialFunctionCompactSnapshot(Operation, RequestId, Function, bIncludeNodeParams, bIncludeLinks);
    }
    return BuildMaterialFunctionFullSnapshot(Operation, RequestId, Function, bIncludeNodeParams, NodeParamsFormat, bIncludeLinks);
}
}
