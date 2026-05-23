#include "Patch/UeNodeNexusBridgeMaterialPatchOps.h"

#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeGraphPatchShared.h"
#include "UeNodeNexusBridgeJson.h"
#include "NodeInterface/UeNodeNexusBridgeMaterialNodeInterfaceShared.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleMaterialNodeParamsGet(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload)
{
    FString NodeId;
    if (!Payload->TryGetStringField(TEXT("node_id"), NodeId))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("node_id is required")));
        return Response;
    }

    if (IsMaterialOutputNodeId(NodeId))
    {
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
        Data->SetStringField(TEXT("graph_name"), TEXT("MaterialGraph"));
        Data->SetStringField(TEXT("node_id"), MaterialOutputNodeId());
        Data->SetStringField(TEXT("node_alias"), MaterialOutputNodeId());
        Data->SetArrayField(TEXT("params"), BuildMaterialOutputParams(Material));

        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        Response->SetObjectField(TEXT("data"), Data);
        return Response;
    }

    UMaterialExpression* Expression = ResolveMaterialInterfaceNode(Material, NodeId);
    if (Expression == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("node_not_found"), TEXT("Material expression was not found")));
        return Response;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
    Data->SetStringField(TEXT("graph_name"), TEXT("MaterialGraph"));
    Data->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(Expression));
    Data->SetStringField(TEXT("node_alias"), MaterialNodeAlias(Material, Expression));
    Data->SetArrayField(TEXT("params"), BuildMaterialExpressionParams(Expression));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleMaterialNodeParamsSet(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload)
{
    FString NodeId;
    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (!Payload->TryGetStringField(TEXT("node_id"), NodeId) || !Payload->TryGetObjectField(TEXT("params"), Params) || Params == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("node_id and params are required")));
        return Response;
    }

    const bool bMaterialOutput = IsMaterialOutputNodeId(NodeId);
    UMaterialExpression* Expression = bMaterialOutput ? nullptr : ResolveMaterialInterfaceNode(Material, NodeId);
    if (!bMaterialOutput && Expression == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("node_not_found"), TEXT("Material expression was not found")));
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
        Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("UE Node Nexus Material Node Params")));
        Material->Modify();
        if (Expression != nullptr)
        {
            Expression->Modify();
        }
    }

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Params)->Values)
    {
        bool bApplied = false;
        FString FailureReason;
        if (bMaterialOutput)
        {
            FString NewValue;
            if (!ReadJsonScalarAsString(*Params, Pair.Key, NewValue))
            {
                Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("param_write_failed"), Pair.Key, Material->GetPathName(), TEXT("UeNodeNexusBridge"))));
                continue;
            }
            bApplied = ApplyMaterialOutputParamValue(Material, Pair.Key, NewValue, bDryRun, Diff);
        }
        else
        {
            bApplied = ApplyMaterialExpressionParamJsonValue(Material, Expression, Pair.Key, Pair.Value, bDryRun, Diff, FailureReason);
        }
        if (!bApplied)
        {
            Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("param_import_failed"), FailureReason.IsEmpty() ? Pair.Key : FailureReason, Material->GetPathName(), TEXT("UeNodeNexusBridge"))));
            continue;
        }
        bChanged = true;
    }

    if (!bDryRun && bChanged)
    {
        if (bCompileAfter)
        {
            UMaterialEditingLibrary::RecompileMaterial(Material);
        }
        else
        {
            Material->MarkPackageDirty();
        }
    }

    TSharedPtr<FJsonObject> Compile = MakeCompilePostCheck(bCompileAfter, !bDryRun && bCompileAfter, true, 0, 0);
    TSharedPtr<FJsonObject> PinIntegrity = BuildMaterialPinIntegrity(Material);
    const bool bOk = Diagnostics.Num() == 0 && PinIntegrity->GetBoolField(TEXT("ok"));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bOk);
    Response->SetObjectField(TEXT("data"), MakeWriteData(bDryRun, !bDryRun && bChanged, bChanged, Diff, PinIntegrity, Compile, MakeDirtyState(Material)));
    Response->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Response;
}
}
