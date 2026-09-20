#include "UeNodeNexusBridgeBlueprintPatchOps.h"

#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "K2Node_InputAction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeBlueprintPinDefaults.h"
#include "UeNodeNexusBridgeGraphPatchShared.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> PinParamToJson(UEdGraphPin* Pin)
{
    TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
    Param->SetStringField(TEXT("name"), Pin->PinName.ToString());
    Param->SetStringField(TEXT("pin_id"), Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
    Param->SetStringField(TEXT("default_value"), BlueprintPinDefaultText(Pin));
    Param->SetBoolField(TEXT("editable"), Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() == 0);
    return Param;
}

static TSharedPtr<FJsonObject> NodePropertyParamToJson(const FString& Name, const FString& Value)
{
    TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
    Param->SetStringField(TEXT("name"), Name);
    Param->SetStringField(TEXT("pin_id"), FString());
    Param->SetStringField(TEXT("default_value"), Value);
    Param->SetStringField(TEXT("source"), TEXT("node_property"));
    Param->SetBoolField(TEXT("editable"), false);
    return Param;
}

TArray<TSharedPtr<FJsonValue>> BuildBlueprintNodeParams(UEdGraphNode* Node)
{
    TArray<TSharedPtr<FJsonValue>> Params;
    if (Node == nullptr)
    {
        return Params;
    }

    if (UK2Node_InputAction* InputAction = Cast<UK2Node_InputAction>(Node))
    {
        Params.Add(MakeShared<FJsonValueObject>(NodePropertyParamToJson(TEXT("InputActionName"), InputAction->InputActionName.ToString())));
    }
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin != nullptr && Pin->Direction == EGPD_Input)
        {
            Params.Add(MakeShared<FJsonValueObject>(PinParamToJson(Pin)));
        }
    }
    return Params;
}

TSharedPtr<FJsonObject> HandleBlueprintNodeParamsGet(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload)
{
    FString GraphName;
    FString NodeId;
    Payload->TryGetStringField(TEXT("graph_name"), GraphName);
    if (!Payload->TryGetStringField(TEXT("node_id"), NodeId) || GraphName.IsEmpty())
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("graph_name and node_id are required")));
        return Response;
    }

    UEdGraph* Graph = FindBlueprintGraph(Blueprint, GraphName);
    UEdGraphNode* Node = FindBlueprintNode(Graph, NodeId);
    if (Node == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("node_not_found"), TEXT("Blueprint node was not found")));
        return Response;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Data->SetStringField(TEXT("graph_name"), Graph ? Graph->GetName() : FString());
    Data->SetStringField(TEXT("node_id"), NodeId);
    Data->SetArrayField(TEXT("params"), BuildBlueprintNodeParams(Node));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

static bool ApplyParamObject(UEdGraph* Graph, UEdGraphNode* Node, const FString& NodeId, const FString& Name, const TSharedPtr<FJsonValue>& Value, bool bDryRun, TSharedPtr<FJsonObject> Diff)
{
    UEdGraphPin* Pin = Node->FindPin(FName(*Name), EGPD_Input);
    if (Pin == nullptr || Pin->LinkedTo.Num() > 0)
    {
        return false;
    }

    FString ValueString;
    TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
    Wrapper->SetField(TEXT("value"), Value);
    if (!ReadJsonScalarAsString(Wrapper, TEXT("value"), ValueString))
    {
        return false;
    }

    AddGraphParamChange(Diff, NodeId, Name, BlueprintPinDefaultText(Pin), ValueString);
    if (!bDryRun)
    {
        Node->Modify();
        Pin->Modify();
        Graph->GetSchema()->TrySetDefaultValue(*Pin, ValueString, true);
    }
    return true;
}

TSharedPtr<FJsonObject> HandleBlueprintNodeParamsSet(const FString& Operation, const FString& RequestId, UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Payload)
{
    FString GraphName;
    FString NodeId;
    Payload->TryGetStringField(TEXT("graph_name"), GraphName);
    Payload->TryGetStringField(TEXT("node_id"), NodeId);

    UEdGraph* Graph = FindBlueprintGraph(Blueprint, GraphName);
    UEdGraphNode* Node = FindBlueprintNode(Graph, NodeId);
    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (GraphName.IsEmpty() || Node == nullptr || !Payload->TryGetObjectField(TEXT("params"), Params) || Params == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("graph_name, node_id and params are required")));
        return Response;
    }

    bool bDryRun = true;
    bool bCompileAfter = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("compile_after"), bCompileAfter);

    TSharedPtr<FJsonObject> Diff = MakeEmptyDiff();
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    bool bChanged = false;
    TUniquePtr<FScopedTransaction> Transaction;
    if (!bDryRun)
    {
        Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("UE Node Nexus Blueprint Node Params")));
        Blueprint->Modify();
        Graph->Modify();
    }

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Params)->Values)
    {
        if (!ApplyParamObject(Graph, Node, NodeId, Pair.Key, Pair.Value, bDryRun, Diff))
        {
            Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("param_write_failed"), FString::Printf(TEXT("Blueprint pin parameter failed: %s"), *Pair.Key), Blueprint->GetPathName(), TEXT("UeNodeNexusBridge"))));
            continue;
        }
        bChanged = true;
    }

    if (!bDryRun && bChanged)
    {
        Graph->NotifyGraphChanged();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }

    TSharedPtr<FJsonObject> Compile = MakeCompilePostCheck(bCompileAfter, false, bDryRun || !bCompileAfter, 0, 0);
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
