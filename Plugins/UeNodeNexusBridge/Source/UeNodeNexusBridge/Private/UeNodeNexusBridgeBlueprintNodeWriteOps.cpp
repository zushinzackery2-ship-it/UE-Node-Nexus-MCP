#include "UeNodeNexusBridgeNodeInterfaceOps.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> MakeBlueprintNodeError(const FString& Operation, const FString& RequestId, const FString& Code, const FString& Message)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(Code, Message));
    return Response;
}

static bool ReadBlueprintPositionPair(const TSharedPtr<FJsonObject>& Payload, bool bOffset, int32& OutX, int32& OutY)
{
    double X = 0.0;
    double Y = 0.0;
    if (bOffset)
    {
        if (!Payload->TryGetNumberField(TEXT("dx"), X) || !Payload->TryGetNumberField(TEXT("dy"), Y))
        {
            return false;
        }
    }
    else if (!Payload->TryGetNumberField(TEXT("x"), X) || !Payload->TryGetNumberField(TEXT("y"), Y))
    {
        return ReadGraphPosition(Payload, OutX, OutY);
    }
    OutX = static_cast<int32>(X);
    OutY = static_cast<int32>(Y);
    return true;
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
    TSharedPtr<FJsonObject> Data = BuildBlueprintNodeInterfaceData(Blueprint, Graph, Node, Payload, TEXT("node_info_text"));
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
    TSharedPtr<FJsonObject> Data = BuildBlueprintNodeInterfaceData(Blueprint, Graph, Node, Payload, TEXT("node_position_text"));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleBlueprintNodePositionSet(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload, bool bOffset)
{
    UEdGraph* Graph = nullptr;
    UEdGraphNode* Node = nullptr;
    if (TSharedPtr<FJsonObject> Error = LoadBlueprintNodeContext(Operation, RequestId, Blueprint, Payload, Graph, Node))
    {
        return Error;
    }
    int32 X = 0;
    int32 Y = 0;
    if (!ReadBlueprintPositionPair(Payload, bOffset, X, Y))
    {
        return MakeBlueprintNodeError(Operation, RequestId, TEXT("invalid_request"), bOffset ? TEXT("dx and dy are required") : TEXT("x and y are required"));
    }

    const int32 BeforeX = Node->NodePosX;
    const int32 BeforeY = Node->NodePosY;
    const int32 AfterX = bOffset ? BeforeX + X : X;
    const int32 AfterY = bOffset ? BeforeY + Y : Y;
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

    TSharedPtr<FJsonObject> Data = BuildBlueprintNodeInterfaceData(Blueprint, Graph, Node, Payload, TEXT("node_position_text"));
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
    return MakeBlueprintNodeError(Operation, RequestId, TEXT("unsupported_operation"), TEXT("Blueprint node_create needs dedicated node factory support before it can be safe"));
}
}
