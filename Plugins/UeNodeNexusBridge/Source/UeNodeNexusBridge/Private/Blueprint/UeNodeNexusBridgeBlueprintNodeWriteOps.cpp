#include "NodeInterface/UeNodeNexusBridgeBlueprintNodeInterfaceOps.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeBlueprintNodeCreateConfig.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeGraphPatchShared.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> MakeBlueprintNodeError(const FString& Operation, const FString& RequestId, const FString& Code, const FString& Message)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(Code, Message));
    return Response;
}

static bool ReadBlueprintPositionPair(const TSharedPtr<FJsonObject>& Payload, int32& OutX, int32& OutY)
{
    double X = 0.0;
    double Y = 0.0;
    if (!Payload->TryGetNumberField(TEXT("x"), X) || !Payload->TryGetNumberField(TEXT("y"), Y))
    {
        return ReadGraphPosition(Payload, OutX, OutY);
    }
    OutX = static_cast<int32>(X);
    OutY = static_cast<int32>(Y);
    return true;
}

static UClass* ResolveBlueprintNodeClassForCreate(const FString& NodeClass)
{
    if (UClass* Direct = LoadClass<UEdGraphNode>(nullptr, *NodeClass))
    {
        return Direct->IsChildOf(UEdGraphNode::StaticClass()) ? Direct : nullptr;
    }
    const FString ShortName = NodeClass.StartsWith(TEXT("K2Node_")) ? NodeClass : TEXT("K2Node_") + NodeClass;
    if (UClass* K2Class = LoadClass<UEdGraphNode>(nullptr, *FString::Printf(TEXT("/Script/BlueprintGraph.%s"), *ShortName)))
    {
        return K2Class->IsChildOf(UEdGraphNode::StaticClass()) ? K2Class : nullptr;
    }
    if (UClass* EngineClass = LoadClass<UEdGraphNode>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *NodeClass)))
    {
        return EngineClass->IsChildOf(UEdGraphNode::StaticClass()) ? EngineClass : nullptr;
    }
    return nullptr;
}

static TSharedPtr<FJsonObject> LoadBlueprintNodeContext(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload, UEdGraph*& OutGraph, UEdGraphNode*& OutNode)
{
    OutGraph = ResolveBlueprintNodeInterfaceGraph(Blueprint, Payload);
    if (OutGraph == nullptr)
    {
        return MakeBlueprintNodeError(Operation, RequestId, TEXT("graph_not_found"), TEXT("Blueprint graph was not found"));
    }
    FString NodeId;
    if (!Payload->TryGetStringField(TEXT("node_id"), NodeId))
    {
        return MakeBlueprintNodeError(Operation, RequestId, TEXT("invalid_request"), TEXT("node_id is required"));
    }
    OutNode = ResolveBlueprintNodeInterfaceNode(OutGraph, Payload);
    if (OutNode == nullptr)
    {
        return MakeBlueprintNodeError(Operation, RequestId, TEXT("node_not_found"), TEXT("Blueprint node was not found"));
    }
    return nullptr;
}

TSharedPtr<FJsonObject> HandleBlueprintNodeInfoGet(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload)
{
    UEdGraph* Graph = nullptr;
    UEdGraphNode* Node = nullptr;
    if (TSharedPtr<FJsonObject> Error = LoadBlueprintNodeContext(Operation, RequestId, Blueprint, Payload, Graph, Node))
    {
        return Error;
    }
    FString Format = TEXT("text");
    Payload->TryGetStringField(TEXT("format"), Format);
    const FString DataFormat = Format.Equals(TEXT("compact_json"), ESearchCase::IgnoreCase) ? TEXT("compact_json") : TEXT("node_info_text");
    TSharedPtr<FJsonObject> Data = BuildBlueprintNodeInterfaceData(Blueprint, Graph, Node, Payload, DataFormat);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, Data->GetBoolField(TEXT("selection_ok")));
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleBlueprintNodePositionGet(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload)
{
    UEdGraph* Graph = nullptr;
    UEdGraphNode* Node = nullptr;
    if (TSharedPtr<FJsonObject> Error = LoadBlueprintNodeContext(Operation, RequestId, Blueprint, Payload, Graph, Node))
    {
        return Error;
    }
    TSharedPtr<FJsonObject> PositionPayload = MakeShared<FJsonObject>();
    PositionPayload->SetStringField(TEXT("section"), TEXT("brief"));
    TSharedPtr<FJsonObject> Data = BuildBlueprintNodeInterfaceData(Blueprint, Graph, Node, PositionPayload, TEXT("node_position_text"));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleBlueprintNodePositionSet(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload)
{
    UEdGraph* Graph = nullptr;
    UEdGraphNode* Node = nullptr;
    if (TSharedPtr<FJsonObject> Error = LoadBlueprintNodeContext(Operation, RequestId, Blueprint, Payload, Graph, Node))
    {
        return Error;
    }
    int32 X = 0;
    int32 Y = 0;
    if (!ReadBlueprintPositionPair(Payload, X, Y))
    {
        return MakeBlueprintNodeError(Operation, RequestId, TEXT("invalid_request"), TEXT("x and y are required"));
    }

    const int32 BeforeX = Node->NodePosX;
    const int32 BeforeY = Node->NodePosY;
    const int32 AfterX = X;
    const int32 AfterY = Y;
    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    if (!bDryRun)
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("UE Node Nexus Blueprint Node Position")));
        Blueprint->Modify();
        Graph->Modify();
        Node->Modify();
        Node->NodePosX = AfterX;
        Node->NodePosY = AfterY;
        Graph->NotifyGraphChanged();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }

    TSharedPtr<FJsonObject> PositionPayload = MakeShared<FJsonObject>();
    PositionPayload->SetStringField(TEXT("section"), TEXT("brief"));
    TSharedPtr<FJsonObject> Data = BuildBlueprintNodeInterfaceData(Blueprint, Graph, Node, PositionPayload, TEXT("node_position_text"));
    SetTextPayload(Data, FString::Printf(TEXT("moved = %s\nNode.Pos.Before = %d,%d\nNode.Pos.After = %d,%d\n"), bDryRun ? TEXT("dry_run") : TEXT("true"), BeforeX, BeforeY, AfterX, AfterY) + Data->GetStringField(TEXT("text")));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleBlueprintNodeCreate(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload)
{
    UEdGraph* Graph = ResolveBlueprintNodeInterfaceGraph(Blueprint, Payload);
    if (Graph == nullptr)
    {
        return MakeBlueprintNodeError(Operation, RequestId, TEXT("graph_not_found"), TEXT("Blueprint graph was not found"));
    }

    FString NodeClassName;
    if (!Payload->TryGetStringField(TEXT("node_class"), NodeClassName) || NodeClassName.IsEmpty())
    {
        return MakeBlueprintNodeError(Operation, RequestId, TEXT("invalid_request"), TEXT("node_class is required"));
    }
    UClass* NodeClass = ResolveBlueprintNodeClassForCreate(NodeClassName);
    if (NodeClass == nullptr || !NodeClass->IsChildOf(UEdGraphNode::StaticClass()))
    {
        return MakeBlueprintNodeError(Operation, RequestId, TEXT("unknown_node_class"), FString::Printf(TEXT("Blueprint node class not found: %s"), *NodeClassName));
    }
    FString ConfigError;
    if (!ValidateBlueprintNodeCreateConfig(NodeClass, Payload, ConfigError))
    {
        return MakeBlueprintNodeError(Operation, RequestId, TEXT("node_config_required"), ConfigError);
    }

    int32 X = 0;
    int32 Y = 0;
    ReadGraphPosition(Payload, X, Y);
    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);

    UEdGraphNode* NewNode = nullptr;
    if (!bDryRun)
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("UE Node Nexus Blueprint Node Create")));
        Blueprint->Modify();
        Graph->Modify();
        FGraphNodeCreator<UEdGraphNode> Creator(*Graph);
        NewNode = Creator.CreateNode(false, NodeClass);
        NewNode->NodePosX = X;
        NewNode->NodePosY = Y;
        if (!ConfigureCreatedBlueprintNode(NewNode, Payload, ConfigError))
        {
            Creator.Finalize();
            Graph->RemoveNode(NewNode);
            return MakeBlueprintNodeError(Operation, RequestId, TEXT("node_config_failed"), ConfigError);
        }
        Creator.Finalize();
        Graph->NotifyGraphChanged();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }

    TSharedPtr<FJsonObject> Data = !bDryRun && NewNode != nullptr
        ? BuildBlueprintNodeInterfaceData(Blueprint, Graph, NewNode, Payload, TEXT("node_create_text"))
        : MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("node_create_text"));
    Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("blueprint"));
    Data->SetStringField(TEXT("graph_name"), Graph->GetName());
    Data->SetBoolField(TEXT("created"), !bDryRun && NewNode != nullptr);
    if (bDryRun)
    {
        SetTextPayload(Data, FString::Printf(TEXT("created = dry_run\nNode.Class = %s\nNode.Pos = %d,%d\n"), *NodeClass->GetPathName(), X, Y));
    }
    if (NewNode != nullptr)
    {
        Data->SetStringField(TEXT("node_id"), NewNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        Data->SetStringField(TEXT("node_alias"), BlueprintNodeAlias(Graph, NewNode));
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
