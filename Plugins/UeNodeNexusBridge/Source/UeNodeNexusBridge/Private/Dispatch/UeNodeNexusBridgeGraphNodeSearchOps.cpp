#include "UeNodeNexusBridgeOperations.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "UeNodeNexusBridgeJson.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"

namespace UeNodeNexusBridge
{
namespace
{
// Payload "filters": each non-empty field must be a substring of the node's value.
struct FNodeSearchFilter
{
    FString NodeClass;
    FString NodeName;
    FString DisplayName;

    // Material expressions carry no title, so they are matched without DisplayName.
    bool Matches(const FString& ClassName, const FString& ObjectName, const FString* Title) const
    {
        return (NodeClass.IsEmpty() || ClassName.Contains(NodeClass))
            && (NodeName.IsEmpty() || ObjectName.Contains(NodeName))
            && (Title == nullptr || DisplayName.IsEmpty() || Title->Contains(DisplayName));
    }
};

void AddBlueprintMatches(UEdGraph* Graph, const FNodeSearchFilter& Filter, int32 Limit, TArray<TSharedPtr<FJsonValue>>& Results)
{
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Results.Num() >= Limit)
        {
            return;
        }
        if (Node == nullptr)
        {
            continue;
        }
        const FString ClassName = Node->GetClass()->GetName();
        const FString ObjName = Node->GetName();
        const FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
        if (!Filter.Matches(ClassName, ObjName, &Title))
        {
            continue;
        }
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        Entry->SetStringField(TEXT("node_class"), ClassName);
        Entry->SetStringField(TEXT("node_name"), ObjName);
        Entry->SetStringField(TEXT("display_name"), Title);
        Entry->SetStringField(TEXT("graph_name"), Graph->GetName());
        Results.Add(MakeShared<FJsonValueObject>(Entry));
    }
}

void AddMaterialMatches(UMaterial* Material, const FNodeSearchFilter& Filter, int32 Limit, TArray<TSharedPtr<FJsonValue>>& Results)
{
    for (TObjectPtr<UMaterialExpression> ExprPtr : Material->GetExpressions())
    {
        UMaterialExpression* Expression = ExprPtr.Get();
        if (Results.Num() >= Limit)
        {
            return;
        }
        if (Expression == nullptr)
        {
            continue;
        }
        const FString ClassName = Expression->GetClass()->GetName();
        const FString ObjName = Expression->GetName();
        if (!Filter.Matches(ClassName, ObjName, nullptr))
        {
            continue;
        }
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(Expression));
        Entry->SetStringField(TEXT("node_class"), ClassName);
        Entry->SetStringField(TEXT("node_name"), ObjName);
        Results.Add(MakeShared<FJsonValueObject>(Entry));
    }
}
}

TSharedPtr<FJsonObject> HandleGraphNodeSearch(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return Response;
    }

    int32 Limit = 10;
    Payload->TryGetNumberField(TEXT("limit"), Limit);
    Limit = FMath::Clamp(Limit, 1, 100);

    const TSharedPtr<FJsonObject>* FiltersObj = nullptr;
    FNodeSearchFilter Filter;
    if (Payload->TryGetObjectField(TEXT("filters"), FiltersObj) && FiltersObj != nullptr)
    {
        (*FiltersObj)->TryGetStringField(TEXT("node_class"), Filter.NodeClass);
        (*FiltersObj)->TryGetStringField(TEXT("node_name"), Filter.NodeName);
        (*FiltersObj)->TryGetStringField(TEXT("display_name"), Filter.DisplayName);
    }

    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    TArray<TSharedPtr<FJsonValue>> Results;

    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        FString GraphName;
        Payload->TryGetStringField(TEXT("graph_name"), GraphName);

        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            if (Graph != nullptr && (GraphName.IsEmpty() || Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase)))
            {
                AddBlueprintMatches(Graph, Filter, Limit, Results);
            }
        }
    }
    else if (UMaterial* Material = Cast<UMaterial>(Asset))
    {
        AddMaterialMatches(Material, Filter, Limit, Results);
    }
    else
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("unsupported_asset_class"), TEXT("graph_node_search supports Blueprint and Material assets")));
        return Response;
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("nodes"), Results);
    Data->SetNumberField(TEXT("count"), Results.Num());
    Data->SetBoolField(TEXT("has_more"), Results.Num() >= Limit);
    Response->SetObjectField(TEXT("data"), Data);

    return Response;
}
}
