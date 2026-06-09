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
#include "NodeInterface/UeNodeNexusBridgeBlueprintNodeInterfaceOps.h"
#include "UeNodeNexusBridgeBlueprintNodeCreateConfig.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeGraphPatchShared.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
struct FBlueprintPatchContext
{
    TMap<FString, UEdGraphNode*> ClientNodes;
    TArray<UEdGraphNode*> DryRunNodes;
};

static FString ReadBlueprintPatchNodeRef(const TSharedPtr<FJsonObject>& Op, const TCHAR* CanonicalField, const TCHAR* AliasField)
{
    FString Value;
    if (Op->TryGetStringField(CanonicalField, Value) && !Value.IsEmpty())
    {
        return Value;
    }
    if (Op->TryGetStringField(AliasField, Value) && !Value.IsEmpty())
    {
        return Value;
    }
    return FString();
}

static UEdGraphNode* ResolveBlueprintPatchNode(UEdGraph* Graph, const FString& NodeId, const FBlueprintPatchContext& Context)
{
    if (UEdGraphNode* const* Found = Context.ClientNodes.Find(NodeId))
    {
        return *Found;
    }

    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("node_id"), NodeId);
    return ResolveBlueprintNodeInterfaceNode(Graph, Payload);
}

static UEdGraphPin* ResolveBlueprintPatchPinByIdOrName(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Op, const TCHAR* IdField, const TCHAR* NameField, EEdGraphPinDirection Direction)
{
    if (Node == nullptr)
    {
        return nullptr;
    }

    FString PinId;
    if (Op->TryGetStringField(IdField, PinId))
    {
        return FindBlueprintPin(Node, PinId);
    }

    FString PinName;
    if (!Op->TryGetStringField(NameField, PinName))
    {
        return nullptr;
    }

    if (UEdGraphPin* ByName = Node->FindPin(FName(*PinName), Direction))
    {
        return ByName;
    }
    if (UEdGraphPin* ByAnyDirection = Node->FindPin(FName(*PinName)))
    {
        return ByAnyDirection;
    }
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin == nullptr)
        {
            continue;
        }
        const FString Friendly = Pin->PinFriendlyName.IsEmpty() ? Pin->PinName.ToString() : Pin->PinFriendlyName.ToString();
        if (Friendly.Equals(PinName, ESearchCase::IgnoreCase) && (Pin->Direction == Direction || Direction == EGPD_MAX))
        {
            return Pin;
        }
    }
    return nullptr;
}

static bool ResolveBlueprintPatchLinkPins(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, const FBlueprintPatchContext& Context, UEdGraphPin*& OutFrom, UEdGraphPin*& OutTo)
{
    const FString FromNodeId = ReadBlueprintPatchNodeRef(Op, TEXT("from_node_id"), TEXT("from_node"));
    const FString ToNodeId = ReadBlueprintPatchNodeRef(Op, TEXT("to_node_id"), TEXT("to_node"));
    if (FromNodeId.IsEmpty() || ToNodeId.IsEmpty())
    {
        return false;
    }

    UEdGraphNode* FromNode = ResolveBlueprintPatchNode(Graph, FromNodeId, Context);
    UEdGraphNode* ToNode = ResolveBlueprintPatchNode(Graph, ToNodeId, Context);
    OutFrom = ResolveBlueprintPatchPinByIdOrName(FromNode, Op, TEXT("from_pin_id"), TEXT("from_pin"), EGPD_Output);
    OutTo = ResolveBlueprintPatchPinByIdOrName(ToNode, Op, TEXT("to_pin_id"), TEXT("to_pin"), EGPD_Input);
    return OutFrom != nullptr && OutTo != nullptr;
}

static UEdGraphNode* ResolveBlueprintPatchOpNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, const FBlueprintPatchContext& Context, FString& OutNodeId)
{
    OutNodeId = ReadBlueprintPatchNodeRef(Op, TEXT("node_id"), TEXT("node"));
    return OutNodeId.IsEmpty() ? nullptr : ResolveBlueprintPatchNode(Graph, OutNodeId, Context);
}

static bool ApplySetNodeParam(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, const FBlueprintPatchContext& Context, bool bDryRun, TSharedPtr<FJsonObject> Diff)
{
    FString NodeId;
    FString PinId;
    FString Value;
    if (!ReadJsonScalarAsString(Op, TEXT("value"), Value))
    {
        return false;
    }

    UEdGraphNode* Node = ResolveBlueprintPatchOpNode(Graph, Op, Context, NodeId);
    UEdGraphPin* Pin = nullptr;
    if (Op->TryGetStringField(TEXT("pin_id"), PinId))
    {
        Pin = FindBlueprintPin(Node, PinId);
    }
    else if (Op->HasField(TEXT("name")))
    {
        Pin = ResolveBlueprintPatchPinByIdOrName(Node, Op, TEXT("pin_id"), TEXT("name"), EGPD_Input);
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

static bool ApplyCreateNode(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, TArray<TSharedPtr<FJsonValue>>& Diagnostics, FBlueprintPatchContext& Context)
{
    FString ClassPath;
    FString NodeClassName;
    int32 X = 0;
    int32 Y = 0;
    if (!ReadGraphPosition(Op, X, Y))
    {
        return false;
    }
    if (!Op->TryGetStringField(TEXT("class_path"), ClassPath))
    {
        Op->TryGetStringField(TEXT("node_class"), ClassPath);
    }
    NodeClassName = ClassPath;
    UClass* NodeClass = ResolveBlueprintNodeClassForCreate(NodeClassName);
    if (NodeClass == nullptr || !NodeClass->IsChildOf(UEdGraphNode::StaticClass()))
    {
        Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("unknown_node_class"), FString::Printf(TEXT("Blueprint node class not found: %s"), *NodeClassName), Blueprint->GetPathName(), TEXT("UeNodeNexusBridge"))));
        return false;
    }

    FString ConfigError;
    if (!ValidateBlueprintNodeCreateConfig(NodeClass, Blueprint, Op, ConfigError))
    {
        Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("node_config_required"), ConfigError, Blueprint->GetPathName(), TEXT("UeNodeNexusBridge"))));
        return false;
    }

    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    FString ClientId;
    if (Op->TryGetStringField(TEXT("client_id"), ClientId) && !ClientId.IsEmpty())
    {
        Item->SetStringField(TEXT("client_id"), ClientId);
    }
    Item->SetStringField(TEXT("class_path"), NodeClass->GetPathName());
    {
        FGraphNodeCreator<UEdGraphNode> Creator(*Graph);
        UEdGraphNode* NewNode = Creator.CreateNode(false, NodeClass);
        NewNode->NodePosX = X;
        NewNode->NodePosY = Y;
        if (!ConfigureCreatedBlueprintNode(NewNode, Blueprint, Op, ConfigError))
        {
            Creator.Finalize();
            Graph->RemoveNode(NewNode);
            Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("node_config_failed"), ConfigError, Blueprint->GetPathName(), TEXT("UeNodeNexusBridge"))));
            return false;
        }
        Creator.Finalize();
        Item->SetStringField(TEXT("node_id"), NewNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        Item->SetStringField(TEXT("node_alias"), BlueprintNodeAlias(Graph, NewNode));
        if (!ClientId.IsEmpty())
        {
            Context.ClientNodes.Add(ClientId, NewNode);
        }
        if (bDryRun)
        {
            Context.DryRunNodes.Add(NewNode);
        }
    }
    AppendDiffItem(Diff, TEXT("nodes_created"), Item);
    return true;
}

static bool ApplyBlueprintOperation(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, TArray<TSharedPtr<FJsonValue>>& Diagnostics, FBlueprintPatchContext& Context)
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
        if (!ResolveBlueprintPatchLinkPins(Graph, Op, Context, FromPin, ToPin))
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
        return ApplySetNodeParam(Graph, Op, Context, bDryRun, Diff);
    }
    if (OpName == TEXT("create_node"))
    {
        return ApplyCreateNode(Blueprint, Graph, Op, bDryRun, Diff, Diagnostics, Context);
    }
    if (OpName == TEXT("delete_node"))
    {
        FString NodeId;
        UEdGraphNode* Node = ResolveBlueprintPatchOpNode(Graph, Op, Context, NodeId);
        if (Node == nullptr)
        {
            return false;
        }
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
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
        UEdGraphNode* Node = ResolveBlueprintPatchOpNode(Graph, Op, Context, NodeId);
        if (Node == nullptr || !ReadGraphPosition(Op, X, Y))
        {
            return false;
        }
        AddGraphParamChange(Diff, Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens), TEXT("position"), FString::Printf(TEXT("%d,%d"), Node->NodePosX, Node->NodePosY), FString::Printf(TEXT("%d,%d"), X, Y));
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

static void CleanupBlueprintDryRunNodes(UEdGraph* Graph, FBlueprintPatchContext& Context)
{
    for (UEdGraphNode* Node : Context.DryRunNodes)
    {
        if (Node != nullptr && Graph != nullptr && Graph->Nodes.Contains(Node))
        {
            Graph->RemoveNode(Node);
        }
    }
    Context.ClientNodes.Empty();
    Context.DryRunNodes.Empty();
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
    FBlueprintPatchContext Context;
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
        if (!Op.IsValid() || !ApplyBlueprintOperation(Blueprint, Graph, Op, bDryRun, Diff, Diagnostics, Context))
        {
            Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("patch_operation_failed"), TEXT("Blueprint patch operation failed validation or application"), Blueprint->GetPathName(), TEXT("UeNodeNexusBridge"))));
            continue;
        }
        bChanged = true;
    }

    if (bDryRun)
    {
        CleanupBlueprintDryRunNodes(Graph, Context);
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
