#include "Patch/UeNodeNexusBridgeMaterialPatchOps.h"

#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeJson.h"
#include "NodeInterface/UeNodeNexusBridgeMaterialNodeInterfaceShared.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleMaterialFunctionNodeParamsGet(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload)
{
    FString NodeId;
    if (!Payload->TryGetStringField(TEXT("node_id"), NodeId))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("node_id is required")));
        return Response;
    }

    UMaterialExpression* Expression = ResolveMaterialInterfaceNode(Function, NodeId);
    if (Expression == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("node_not_found"), TEXT("Material function node was not found")));
        return Response;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Function->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("material_function"));
    Data->SetStringField(TEXT("graph_name"), TEXT("MaterialFunctionGraph"));
    Data->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(Expression));
    Data->SetStringField(TEXT("node_alias"), MaterialNodeAlias(Function, Expression));
    Data->SetArrayField(TEXT("params"), BuildMaterialExpressionParams(Expression));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleMaterialFunctionNodeParamsSet(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload)
{
    FString NodeId;
    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (!Payload->TryGetStringField(TEXT("node_id"), NodeId) || !Payload->TryGetObjectField(TEXT("params"), Params) || Params == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("node_id and params are required")));
        return Response;
    }

    UMaterialExpression* Expression = ResolveMaterialInterfaceNode(Function, NodeId);
    if (Expression == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("node_not_found"), TEXT("Material function node was not found")));
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
        Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("UE Node Nexus Material Function Node Params")));
        Function->Modify();
        Expression->Modify();
    }

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Params)->Values)
    {
        FString FailureReason;
        if (!ApplyMaterialExpressionParamJsonValue(nullptr, Expression, Pair.Key, Pair.Value, bDryRun, Diff, FailureReason))
        {
            Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("param_import_failed"), FailureReason.IsEmpty() ? Pair.Key : FailureReason, Function->GetPathName(), TEXT("UeNodeNexusBridge"))));
            continue;
        }
        bChanged = true;
    }

    if (!bDryRun && bChanged)
    {
        if (bCompileAfter)
        {
            UMaterialEditingLibrary::UpdateMaterialFunction(Function, nullptr);
        }
        else
        {
            Function->MarkPackageDirty();
        }
    }

    TSharedPtr<FJsonObject> Compile = MakeCompilePostCheck(bCompileAfter, !bDryRun && bCompileAfter, true, 0, 0);
    TSharedPtr<FJsonObject> PinIntegrity = BuildMaterialFunctionPinIntegrity(Function);
    const bool bOk = Diagnostics.Num() == 0 && PinIntegrity->GetBoolField(TEXT("ok"));
    TSharedPtr<FJsonObject> Data = MakeWriteData(bDryRun, !bDryRun && bChanged, bChanged, Diff, PinIntegrity, Compile, MakeDirtyState(Function));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bOk);
    Response->SetObjectField(TEXT("data"), Data);
    Response->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Response;
}
}
