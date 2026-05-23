#include "UeNodeNexusBridgeOperations.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "NodeInterface/UeNodeNexusBridgeBlueprintNodeInterfaceOps.h"
#include "UeNodeNexusBridgeGraphGroupedInfoOps.h"
#include "UeNodeNexusBridgeJson.h"
#include "NodeInterface/UeNodeNexusBridgeMaterialNodeInterfaceOps.h"

namespace UeNodeNexusBridge
{
static int32 ReadGraphNodeInfoMaxNodes(const TSharedPtr<FJsonObject>& Payload)
{
    double MaxNodes = 0.0;
    if (!Payload->TryGetNumberField(TEXT("max_nodes"), MaxNodes))
    {
        return 0;
    }
    return FMath::Max(0, static_cast<int32>(MaxNodes));
}

static TSharedPtr<FJsonObject> MakeGraphNodeInfoPayload(const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> NodePayload = MakeShared<FJsonObject>();
    FString Section = TEXT("all");
    Payload->TryGetStringField(TEXT("section"), Section);
    NodePayload->SetStringField(TEXT("section"), Section);
    return NodePayload;
}

static bool WantsGraphNodePositions(const TSharedPtr<FJsonObject>& Payload)
{
    bool bIncludePosition = false;
    Payload->TryGetBoolField(TEXT("include_position"), bIncludePosition);
    return bIncludePosition;
}

static TSharedPtr<FJsonObject> MakeGraphNodeInfoData(const FString& AssetPath, const FString& GraphKind, const FString& GraphName, int32 TotalNodes, int32 ReturnedNodes, const FString& Text)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("graph_node_info_text"));
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("graph_kind"), GraphKind);
    Data->SetStringField(TEXT("graph_name"), GraphName);
    Data->SetNumberField(TEXT("total_nodes"), TotalNodes);
    Data->SetNumberField(TEXT("returned_nodes"), ReturnedNodes);
    Data->SetBoolField(TEXT("truncated"), ReturnedNodes < TotalNodes);
    SetTextPayload(Data, Text);
    return Data;
}

static TSharedPtr<FJsonObject> BuildMaterialGraphNodeInfo(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload)
{
    FString Format = TEXT("indexed");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bWithPosition = WantsGraphNodePositions(Payload);
    if (Format.Equals(TEXT("grouped"), ESearchCase::IgnoreCase))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        Response->SetObjectField(TEXT("data"), BuildMaterialGraphGroupedData(Material, Payload, bWithPosition));
        return Response;
    }
    if (!Format.Equals(TEXT("text"), ESearchCase::IgnoreCase))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        Response->SetObjectField(TEXT("data"), BuildMaterialGraphIndexedData(Material, Payload, bWithPosition));
        return Response;
    }

    const int32 MaxNodes = ReadGraphNodeInfoMaxNodes(Payload);
    const TSharedPtr<FJsonObject> NodePayload = MakeGraphNodeInfoPayload(Payload);
    const TArrayView<const TObjectPtr<UMaterialExpression>> Expressions = Material->GetExpressions();

    FString Text = FString::Printf(TEXT("Graph.Asset = %s\nGraph.Kind = material\nGraph.Name = MaterialGraph\nGraph.Nodes = %d\n\n"), *Material->GetPathName(), Expressions.Num());
    int32 ReturnedNodes = 0;
    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Expressions)
    {
        UMaterialExpression* Expression = ExpressionPtr.Get();
        if (Expression == nullptr)
        {
            continue;
        }
        if (MaxNodes > 0 && ReturnedNodes >= MaxNodes)
        {
            break;
        }

        TSharedPtr<FJsonObject> NodeData = BuildMaterialNodeInterfaceData(Material, Expression, NodePayload, FString());
        Text += FString::Printf(TEXT("--- node_%02d ---\n"), ReturnedNodes);
        Text += NodeData->GetStringField(TEXT("text"));
        Text += TEXT("\n");
        ++ReturnedNodes;
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), MakeGraphNodeInfoData(Material->GetPathName(), TEXT("material"), TEXT("MaterialGraph"), Expressions.Num(), ReturnedNodes, Text));
    return Response;
}

static TSharedPtr<FJsonObject> BuildMaterialFunctionGraphNodeInfo(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload)
{
    FString Format = TEXT("indexed");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bWithPosition = WantsGraphNodePositions(Payload);
    if (Format.Equals(TEXT("grouped"), ESearchCase::IgnoreCase))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        Response->SetObjectField(TEXT("data"), BuildMaterialFunctionGraphGroupedData(Function, Payload, bWithPosition));
        return Response;
    }
    if (!Format.Equals(TEXT("text"), ESearchCase::IgnoreCase))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        Response->SetObjectField(TEXT("data"), BuildMaterialFunctionGraphIndexedData(Function, Payload, bWithPosition));
        return Response;
    }

    const int32 MaxNodes = ReadGraphNodeInfoMaxNodes(Payload);
    const TSharedPtr<FJsonObject> NodePayload = MakeGraphNodeInfoPayload(Payload);
    const TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Function->GetExpressions();

    FString Text = FString::Printf(TEXT("Graph.Asset = %s\nGraph.Kind = material_function\nGraph.Name = MaterialFunctionGraph\nGraph.Nodes = %d\n\n"), *Function->GetPathName(), Expressions.Num());
    int32 ReturnedNodes = 0;
    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Expressions)
    {
        UMaterialExpression* Expression = ExpressionPtr.Get();
        if (Expression == nullptr)
        {
            continue;
        }
        if (MaxNodes > 0 && ReturnedNodes >= MaxNodes)
        {
            break;
        }

        TSharedPtr<FJsonObject> NodeData = BuildMaterialFunctionNodeInterfaceData(Function, Expression, NodePayload, FString());
        Text += FString::Printf(TEXT("--- node_%02d ---\n"), ReturnedNodes);
        Text += NodeData->GetStringField(TEXT("text"));
        Text += TEXT("\n");
        ++ReturnedNodes;
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), MakeGraphNodeInfoData(Function->GetPathName(), TEXT("material_function"), TEXT("MaterialFunctionGraph"), Expressions.Num(), ReturnedNodes, Text));
    return Response;
}

static TSharedPtr<FJsonObject> BuildBlueprintGraphNodeInfo(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload)
{
    UEdGraph* Graph = ResolveBlueprintNodeInterfaceGraph(Blueprint, Payload);
    if (Graph == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("graph_not_found"), TEXT("Blueprint graph was not found")));
        return Response;
    }

    FString Format = TEXT("indexed");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bWithPosition = WantsGraphNodePositions(Payload);
    if (Format.Equals(TEXT("grouped"), ESearchCase::IgnoreCase))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        Response->SetObjectField(TEXT("data"), BuildBlueprintGraphGroupedData(Blueprint, Graph, Payload, bWithPosition));
        return Response;
    }
    if (!Format.Equals(TEXT("text"), ESearchCase::IgnoreCase))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        Response->SetObjectField(TEXT("data"), BuildBlueprintGraphIndexedData(Blueprint, Graph, Payload, bWithPosition));
        return Response;
    }

    const int32 MaxNodes = ReadGraphNodeInfoMaxNodes(Payload);
    const TSharedPtr<FJsonObject> NodePayload = MakeGraphNodeInfoPayload(Payload);
    FString Text = FString::Printf(TEXT("Graph.Asset = %s\nGraph.Kind = blueprint\nGraph.Name = %s\nGraph.Nodes = %d\n\n"), *Blueprint->GetPathName(), *Graph->GetName(), Graph->Nodes.Num());

    int32 ReturnedNodes = 0;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr)
        {
            continue;
        }
        if (MaxNodes > 0 && ReturnedNodes >= MaxNodes)
        {
            break;
        }

        TSharedPtr<FJsonObject> NodeData = BuildBlueprintNodeInterfaceData(Blueprint, Graph, Node, NodePayload, TEXT("node_info_text"));
        Text += FString::Printf(TEXT("--- node_%02d ---\n"), ReturnedNodes);
        Text += NodeData->GetStringField(TEXT("text"));
        Text += TEXT("\n");
        ++ReturnedNodes;
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), MakeGraphNodeInfoData(Blueprint->GetPathName(), TEXT("blueprint"), Graph->GetName(), Graph->Nodes.Num(), ReturnedNodes, Text));
    return Response;
}

TSharedPtr<FJsonObject> HandleGraphNodeInfoGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return Response;
    }

    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (UMaterial* Material = Cast<UMaterial>(Asset))
    {
        return BuildMaterialGraphNodeInfo(Operation, RequestId, Material, Payload);
    }
    if (UMaterialFunction* Function = Cast<UMaterialFunction>(Asset))
    {
        return BuildMaterialFunctionGraphNodeInfo(Operation, RequestId, Function, Payload);
    }
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        return BuildBlueprintGraphNodeInfo(Operation, RequestId, Blueprint, Payload);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("unsupported_asset_class"), TEXT("graph_node_info_get supports Blueprint, Material, and MaterialFunction assets")));
    return Response;
}
}
