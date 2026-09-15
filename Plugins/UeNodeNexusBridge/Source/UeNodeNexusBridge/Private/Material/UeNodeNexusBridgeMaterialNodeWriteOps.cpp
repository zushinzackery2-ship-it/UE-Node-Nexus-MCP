#include "NodeInterface/UeNodeNexusBridgeMaterialNodeInterfaceOps.h"

#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "ScopedTransaction.h"
#include "UeNodeNexusBridgeJson.h"
#include "NodeInterface/UeNodeNexusBridgeMaterialNodeInterfaceShared.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "UeNodeNexusBridgeGraphPatchShared.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleMaterialNodeCreate(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload)
{
    FString NodeClass;
    int32 X = 0;
    int32 Y = 0;
    if (!Payload->TryGetStringField(TEXT("node_class"), NodeClass))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("node_class is required")));
        return Response;
    }
    ReadMaterialPosition(Payload, X, Y);

    UClass* ExpressionClass = ResolveMaterialExpressionClass(NodeClass);
    if (ExpressionClass == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("unknown_node_class"), FString::Printf(TEXT("Material node class not found: %s"), *NodeClass)));
        return Response;
    }

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    UMaterialExpression* Expression = nullptr;
    if (!bDryRun)
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("UE Node Nexus Material Node Create")));
        Material->Modify();
        Expression = UMaterialEditingLibrary::CreateMaterialExpression(Material, ExpressionClass, X, Y);
        const TSharedPtr<FJsonObject>* Params = nullptr;
        if (Payload->TryGetObjectField(TEXT("params"), Params) && Params != nullptr)
        {
            TSharedPtr<FJsonObject> Diff = MakeEmptyDiff();
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Params)->Values)
            {
                FString Value;
                TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
                Wrapper->SetField(TEXT("value"), Pair.Value);
                if (!ReadJsonScalarAsString(Wrapper, TEXT("value"), Value) || !ApplyMaterialExpressionParamValue(Material, Expression, Pair.Key, Value, false, Diff))
                {
                    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
                    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("param_import_failed"), Pair.Key));
                    return Response;
                }
            }
            Expression->PostEditChange();
        }
        Material->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Data = !bDryRun
        ? BuildMaterialNodeInterfaceData(Material, Expression, Payload, TEXT("created = true\n"))
        : MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("node_create_text"));
    Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("material"));
    Data->SetStringField(TEXT("graph_name"), TEXT("MaterialGraph"));
    Data->SetBoolField(TEXT("created"), !bDryRun);
    if (bDryRun)
    {
        SetTextPayload(Data, FString::Printf(TEXT("created = dry_run\nNode.Class = %s\nNode.Pos = %d,%d\n"), *ExpressionClass->GetName(), X, Y));
    }
    if (Expression != nullptr)
    {
        Data->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(Expression));
        Data->SetStringField(TEXT("node_alias"), MaterialNodeAlias(Material, Expression));
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
