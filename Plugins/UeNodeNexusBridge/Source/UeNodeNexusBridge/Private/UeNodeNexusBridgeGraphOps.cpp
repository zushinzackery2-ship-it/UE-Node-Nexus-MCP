#include "UeNodeNexusBridgeOperations.h"

#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialPatchHelpers.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> BuildBlueprintGraphSnapshot(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const FString& GraphName);

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

static TSharedPtr<FJsonObject> MaterialNodeToJson(UMaterialExpression* Expression)
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
        const FString InputName = Expression->GetInputName(Index).ToString();
        Pins.Add(MakeShared<FJsonValueObject>(MaterialPinToJson(FString::Printf(TEXT("%s:in:%d"), *NodeId, Index), InputName, TEXT("input"))));
    }

    TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
    for (int32 Index = 0; Index < Outputs.Num(); ++Index)
    {
        const FString OutputName = Outputs[Index].OutputName.IsNone() ? FString::FromInt(Index) : Outputs[Index].OutputName.ToString();
        Pins.Add(MakeShared<FJsonValueObject>(MaterialPinToJson(FString::Printf(TEXT("%s:out:%d"), *NodeId, Index), OutputName, TEXT("output"))));
    }

    Node->SetArrayField(TEXT("pins"), Pins);
    Node->SetObjectField(TEXT("params"), MakeShared<FJsonObject>());
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

static TSharedPtr<FJsonObject> BuildMaterialGraphSnapshot(const FString& Operation, const FString& RequestId, UMaterial* Material)
{
    TArray<TSharedPtr<FJsonValue>> Nodes;
    TArray<TSharedPtr<FJsonValue>> Links;
    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Material->GetExpressions())
    {
        UMaterialExpression* Expression = ExpressionPtr.Get();
        if (Expression != nullptr)
        {
            Nodes.Add(MakeShared<FJsonValueObject>(MaterialNodeToJson(Expression)));
            AddMaterialLinks(Expression, Links);
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

TSharedPtr<FJsonObject> HandleGraphSnapshotGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return Response;
    }

    FString GraphName;
    Payload->TryGetStringField(TEXT("graph_name"), GraphName);

    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        return BuildBlueprintGraphSnapshot(Operation, RequestId, Blueprint, GraphName);
    }
    if (UMaterial* Material = Cast<UMaterial>(Asset))
    {
        return BuildMaterialGraphSnapshot(Operation, RequestId, Material);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(TEXT("unsupported_asset_class"), TEXT("graph_snapshot_get supports Blueprint and Material assets")));
    return Response;
}
}
