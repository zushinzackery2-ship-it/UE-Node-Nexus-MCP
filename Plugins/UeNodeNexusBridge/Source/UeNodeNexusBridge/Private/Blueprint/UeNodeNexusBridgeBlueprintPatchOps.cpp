#include "UeNodeNexusBridgeBlueprintPatchOps.h"

#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeGraphPatchShared.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static bool ApplySetNodeParam(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff)
{
    FString NodeId;
    FString PinId;
    FString Name;
    FString Value;
    if (!Op->TryGetStringField(TEXT("node_id"), NodeId) || !ReadJsonScalarAsString(Op, TEXT("value"), Value))
    {
        return false;
    }

    UEdGraphNode* Node = FindBlueprintNode(Graph, NodeId);
    UEdGraphPin* Pin = nullptr;
    if (Op->TryGetStringField(TEXT("pin_id"), PinId))
    {
        Pin = FindBlueprintPin(Node, PinId);
    }
    else if (Op->TryGetStringField(TEXT("name"), Name) && Node != nullptr)
    {
        Pin = Node->FindPin(FName(*Name), EGPD_Input);
    }

    if (Pin == nullptr || Pin->Direction != EGPD_Input || Pin->LinkedTo.Num() > 0)
    {
        return false;
    }

    AddGraphParamChange(Diff, NodeId, Pin->PinName.ToString(), Pin->DefaultValue, Value);
    if (!bDryRun)
    {
        Graph->Modify();
        Node->Modify();
        Pin->Modify();
        Graph->GetSchema()->TrySetDefaultValue(*Pin, Value, true);
    }
    return true;
}

static bool ApplyCreateNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff)
{
    FString ClassPath;
    int32 X = 0;
    int32 Y = 0;
    if (!Op->TryGetStringField(TEXT("class_path"), ClassPath) || !ReadGraphPosition(Op, X, Y))
    {
        return false;
    }
    UClass* NodeClass = LoadClass<UEdGraphNode>(nullptr, *ClassPath);
    if (NodeClass == nullptr || !NodeClass->IsChildOf(UEdGraphNode::StaticClass()))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("class_path"), ClassPath);
    if (!bDryRun)
    {
        FGraphNodeCreator<UEdGraphNode> Creator(*Graph);
        UEdGraphNode* NewNode = Creator.CreateNode(false, NodeClass);
        NewNode->NodePosX = X;
        NewNode->NodePosY = Y;
        Creator.Finalize();
        Item->SetStringField(TEXT("node_id"), NewNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    }
    AppendDiffItem(Diff, TEXT("nodes_created"), Item);
    return true;
}

static bool ApplyBlueprintOperation(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff)
{
    FString OpName;
    if (!Op->TryGetStringField(TEXT("op"), OpName))
    {
        return false;
    }

    if (OpName == TEXT("connect_pins") || OpName == TEXT("disconnect_pins"))
    {
        UEdGraphPin* FromPin = nullptr;
        UEdGraphPin* ToPin = nullptr;
        if (!ResolveBlueprintLinkPins(Graph, Op, FromPin, ToPin))
        {
            return false;
        }
        AppendDiffItem(Diff, OpName == TEXT("connect_pins") ? TEXT("links_added") : TEXT("links_removed"), MakeBlueprintLinkJson(FromPin, ToPin));
        if (!bDryRun)
        {
            return OpName == TEXT("connect_pins") ? Graph->GetSchema()->TryCreateConnection(FromPin, ToPin) : (FromPin->BreakLinkTo(ToPin), true);
        }
        return true;
    }
    if (OpName == TEXT("set_node_param"))
    {
        return ApplySetNodeParam(Graph, Op, bDryRun, Diff);
    }
    if (OpName == TEXT("create_node"))
    {
        return ApplyCreateNode(Graph, Op, bDryRun, Diff);
    }
    if (OpName == TEXT("delete_node"))
    {
        FString NodeId;
        UEdGraphNode* Node = Op->TryGetStringField(TEXT("node_id"), NodeId) ? FindBlueprintNode(Graph, NodeId) : nullptr;
        if (Node == nullptr)
        {
            return false;
        }
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), NodeId);
        AppendDiffItem(Diff, TEXT("nodes_deleted"), Item);
        if (!bDryRun)
        {
            FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
        }
        return true;
    }
    if (OpName == TEXT("set_node_position"))
    {
        FString NodeId;
        int32 X = 0;
        int32 Y = 0;
        UEdGraphNode* Node = Op->TryGetStringField(TEXT("node_id"), NodeId) ? FindBlueprintNode(Graph, NodeId) : nullptr;
        if (Node == nullptr || !ReadGraphPosition(Op, X, Y))
        {
            return false;
        }
        AddGraphParamChange(Diff, NodeId, TEXT("position"), FString::Printf(TEXT("%d,%d"), Node->NodePosX, Node->NodePosY), FString::Printf(TEXT("%d,%d"), X, Y));
        if (!bDryRun)
        {
            Node->Modify();
            Node->NodePosX = X;
            Node->NodePosY = Y;
        }
        return true;
    }
    return false;
}

bool IsBlueprintPatchAsset(UObject* Asset)
{
    return Cast<UBlueprint>(Asset) != nullptr;
}

TSharedPtr<FJsonObject> HandleBlueprintGraphPatch(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload)
{
    FString GraphName;
    Payload->TryGetStringField(TEXT("graph_name"), GraphName);
    UEdGraph* Graph = FindBlueprintGraph(Blueprint, GraphName);
    if (Graph == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("graph_not_found"), TEXT("Blueprint graph was not found")));
        return Response;
    }

    bool bDryRun = true;
    bool bCompileAfter = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("compile_after"), bCompileAfter);

    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (!Payload->TryGetArrayField(TEXT("operations"), Operations) || Operations == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("operations must be an array")));
        return Response;
    }

    TSharedPtr<FJsonObject> Diff = MakeEmptyDiff();
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    bool bChanged = false;
    TUniquePtr<FScopedTransaction> Transaction;
    if (!bDryRun)
    {
        Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("UE Node Nexus Blueprint Patch")));
        Blueprint->Modify();
        Graph->Modify();
    }

    for (const TSharedPtr<FJsonValue>& Value : *Operations)
    {
        TSharedPtr<FJsonObject> Op = Value->AsObject();
        if (!Op.IsValid() || !ApplyBlueprintOperation(Blueprint, Graph, Op, bDryRun, Diff))
        {
            Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("patch_operation_failed"), TEXT("Blueprint patch operation failed validation or application"), Blueprint->GetPathName(), TEXT("UeNodeNexusBridge"))));
            continue;
        }
        bChanged = true;
    }

    if (!bDryRun && bChanged)
    {
        Graph->NotifyGraphChanged();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }

    TSharedPtr<FJsonObject> Compile = MakeCompilePostCheck(bCompileAfter, false, !bCompileAfter, 0, 0);
    if (!bDryRun && bCompileAfter)
    {
        Diagnostics.Append(CompileBlueprintWithDiagnostics(Blueprint, Blueprint->GetPathName(), Compile));
    }

    TSharedPtr<FJsonObject> PinIntegrity = BuildBlueprintPinIntegrity(Graph);
    const bool bOk = Diagnostics.Num() == 0 && PinIntegrity->GetBoolField(TEXT("ok")) && (!bCompileAfter || Compile->GetBoolField(TEXT("ok")));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bOk);
    Response->SetObjectField(TEXT("data"), MakeWriteData(bDryRun, !bDryRun && bChanged, bChanged, Diff, PinIntegrity, Compile, MakeDirtyState(Blueprint)));
    Response->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Response;
}
}
