#include "UeNodeNexusBridgeOperations.h"

#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "UeNodeNexusBridgeBlueprintGraphFilter.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialGraphSnapshot.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> BuildBlueprintGraphSnapshot(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const FString& GraphName, bool bIncludeNodeParams, bool bIncludeLinks, bool bCompact, bool bWire, bool bWireMin, bool bWireTiny, const FBlueprintGraphFilter& Filter);

static TArray<TSharedPtr<FJsonValue>> BuildAvailableGraphs(UBlueprint* Blueprint)
{
    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);

    TArray<TSharedPtr<FJsonValue>> GraphList;
    for (UEdGraph* Graph : AllGraphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), Graph->GetName());
        Entry->SetNumberField(TEXT("nodes"), Graph->Nodes.Num());
        GraphList.Add(MakeShared<FJsonValueObject>(Entry));
    }
    return GraphList;
}

static bool IsSupportedGraphSnapshotFormat(const FString& Format)
{
    return Format.Equals(TEXT("wires_tiny"), ESearchCase::IgnoreCase)
        || Format.Equals(TEXT("wires_min"), ESearchCase::IgnoreCase)
        || Format.Equals(TEXT("wires"), ESearchCase::IgnoreCase)
        || Format.Equals(TEXT("compact"), ESearchCase::IgnoreCase)
        || Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
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

    FString Format = TEXT("wires_tiny");
    Payload->TryGetStringField(TEXT("format"), Format);
    if (!IsSupportedGraphSnapshotFormat(Format))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("format must be wires_tiny, wires_min, wires, compact, or full")));
        return Response;
    }

    const bool bWireTiny = Format.Equals(TEXT("wires_tiny"), ESearchCase::IgnoreCase);
    const bool bWireMin = Format.Equals(TEXT("wires_min"), ESearchCase::IgnoreCase);
    const bool bWire = bWireTiny || bWireMin || Format.Equals(TEXT("wires"), ESearchCase::IgnoreCase);
    const bool bCompact = !Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
    bool bIncludeNodeParams = false;
    bool bIncludeLinks = true;
    Payload->TryGetBoolField(TEXT("include_node_params"), bIncludeNodeParams);
    Payload->TryGetBoolField(TEXT("include_links"), bIncludeLinks);

    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        FBlueprintGraphFilter Filter = FBlueprintGraphFilter::FromPayload(Payload);
        TSharedPtr<FJsonObject> Response = BuildBlueprintGraphSnapshot(Operation, RequestId, Blueprint, GraphName, bIncludeNodeParams, bIncludeLinks, bCompact, bWire, bWireMin, bWireTiny, Filter);

        TSharedPtr<FJsonObject> Data = Response->GetObjectField(TEXT("data"));
        if (Data.IsValid())
        {
            Data->SetArrayField(TEXT("available_graphs"), BuildAvailableGraphs(Blueprint));
        }

        return Response;
    }
    if (UMaterial* Material = Cast<UMaterial>(Asset))
    {
        return BuildMaterialGraphSnapshot(Operation, RequestId, Material, bIncludeNodeParams, bIncludeLinks, bCompact, bWire, bWireMin, bWireTiny);
    }
    if (UMaterialFunction* Function = Cast<UMaterialFunction>(Asset))
    {
        return BuildMaterialFunctionGraphSnapshot(Operation, RequestId, Function, bIncludeNodeParams, bIncludeLinks, bCompact, bWire, bWireMin, bWireTiny);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(TEXT("unsupported_asset_class"), TEXT("graph_snapshot_get supports Blueprint, Material, and MaterialFunction assets")));
    return Response;
}
}
