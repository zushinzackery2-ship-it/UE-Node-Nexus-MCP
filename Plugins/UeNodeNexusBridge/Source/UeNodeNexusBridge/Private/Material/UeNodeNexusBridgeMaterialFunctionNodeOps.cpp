#include "UeNodeNexusBridgeMaterialNodeInterfaceOps.h"

#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialNodeInterfaceShared.h"
#include "UeNodeNexusBridgeMaterialPatchHelpers.h"

namespace UeNodeNexusBridge
{
static bool ReadFunctionPositionPair(const TSharedPtr<FJsonObject>& Payload, bool bOffset, int32& OutX, int32& OutY)
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
        return ReadMaterialPosition(Payload, OutX, OutY);
    }
    OutX = static_cast<int32>(X);
    OutY = static_cast<int32>(Y);
    return true;
}

static TSharedPtr<FJsonObject> MakeFunctionPositionData(UMaterialFunction* Function, UMaterialExpression* Expression, const FString& Prefix)
{
    const FString Alias = MaterialNodeAlias(Function, Expression);
    FString Text = Prefix;
    Text += FString::Printf(TEXT("Node.Name = %s\n"), *Alias);
    Text += FString::Printf(TEXT("Node.Id = %s\n"), *Alias);
    Text += FString::Printf(TEXT("Node.RealId = %s\n"), *MaterialExpressionNodeId(Expression));
    Text += FString::Printf(TEXT("Node.Pos = %d,%d\n"), Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("node_position_text"));
    Data->SetStringField(TEXT("asset_path"), Function->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("material_function"));
    Data->SetStringField(TEXT("graph_name"), TEXT("MaterialFunctionGraph"));
    Data->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(Expression));
    Data->SetStringField(TEXT("node_alias"), Alias);
    SetTextPayload(Data, Text);
    return Data;
}

static UMaterialExpression* ReadFunctionNodeOrError(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutResponse)
{
    FString NodeId;
    if (!Payload->TryGetStringField(TEXT("node_id"), NodeId))
    {
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("node_id is required")));
        return nullptr;
    }

    UMaterialExpression* Expression = ResolveMaterialInterfaceNode(Function, NodeId);
    if (Expression == nullptr)
    {
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), MakeError(TEXT("node_not_found"), TEXT("Material function node was not found")));
    }
    return Expression;
}

TSharedPtr<FJsonObject> HandleMaterialFunctionNodeInfoGet(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> Error;
    UMaterialExpression* Expression = ReadFunctionNodeOrError(Operation, RequestId, Function, Payload, Error);
    if (Expression == nullptr)
    {
        return Error;
    }

    TSharedPtr<FJsonObject> Data = BuildMaterialFunctionNodeInterfaceData(Function, Expression, Payload, FString());
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, Data->GetBoolField(TEXT("selection_ok")));
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleMaterialFunctionNodePositionGet(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> Error;
    UMaterialExpression* Expression = ReadFunctionNodeOrError(Operation, RequestId, Function, Payload, Error);
    if (Expression == nullptr)
    {
        return Error;
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), MakeFunctionPositionData(Function, Expression, FString()));
    return Response;
}

TSharedPtr<FJsonObject> HandleMaterialFunctionNodePositionSet(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload, bool bOffset)
{
    TSharedPtr<FJsonObject> Error;
    UMaterialExpression* Expression = ReadFunctionNodeOrError(Operation, RequestId, Function, Payload, Error);
    if (Expression == nullptr)
    {
        return Error;
    }

    int32 X = 0;
    int32 Y = 0;
    if (!ReadFunctionPositionPair(Payload, bOffset, X, Y))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), bOffset ? TEXT("dx and dy are required") : TEXT("x and y are required")));
        return Response;
    }

    const int32 BeforeX = Expression->MaterialExpressionEditorX;
    const int32 BeforeY = Expression->MaterialExpressionEditorY;
    const int32 AfterX = bOffset ? BeforeX + X : X;
    const int32 AfterY = bOffset ? BeforeY + Y : Y;
    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);

    TUniquePtr<FScopedTransaction> Transaction;
    if (!bDryRun)
    {
        Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("UE Node Nexus Material Function Node Position")));
        Function->Modify();
        Expression->Modify();
        Expression->MaterialExpressionEditorX = AfterX;
        Expression->MaterialExpressionEditorY = AfterY;
        Function->MarkPackageDirty();
    }

    const FString Prefix = FString::Printf(TEXT("moved = %s\nNode.Pos.Before = %d,%d\nNode.Pos.After = %d,%d\n"), bDryRun ? TEXT("dry_run") : TEXT("true"), BeforeX, BeforeY, AfterX, AfterY);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), MakeFunctionPositionData(Function, Expression, Prefix));
    return Response;
}

TSharedPtr<FJsonObject> HandleMaterialFunctionNodeCreate(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload)
{
    FString NodeClass;
    if (!Payload->TryGetStringField(TEXT("node_class"), NodeClass))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("node_class is required")));
        return Response;
    }

    int32 X = 0;
    int32 Y = 0;
    ReadMaterialPosition(Payload, X, Y);

    UClass* ExpressionClass = ResolveMaterialExpressionClass(NodeClass);
    if (ExpressionClass == nullptr)
    {
        const FString Message = FString::Printf(TEXT("Material expression class not found: %s"), *NodeClass);
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(FString(TEXT("unknown_node_class")), Message));
        return Response;
    }

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);

    UMaterialExpression* Expression = nullptr;
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    TSharedPtr<FJsonObject> Diff = MakeEmptyDiff();
    if (!bDryRun)
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("UE Node Nexus Material Function Node Create")));
        Function->Modify();
        Expression = UMaterialEditingLibrary::CreateMaterialExpressionInFunction(Function, ExpressionClass, X, Y);
        const TSharedPtr<FJsonObject>* Params = nullptr;
        if (Expression != nullptr && Payload->TryGetObjectField(TEXT("params"), Params) && Params != nullptr)
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Params)->Values)
            {
                FString FailureReason;
                if (!ApplyMaterialExpressionParamJsonValue(nullptr, Expression, Pair.Key, Pair.Value, false, Diff, FailureReason))
                {
                    Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("param_import_failed"), FailureReason.IsEmpty() ? Pair.Key : FailureReason, Function->GetPathName(), TEXT("UeNodeNexusBridge"))));
                }
            }
            Expression->PostEditChange();
        }
        UMaterialEditingLibrary::UpdateMaterialFunction(Function, nullptr);
    }

    TSharedPtr<FJsonObject> Data = !bDryRun && Expression != nullptr
        ? BuildMaterialFunctionNodeInterfaceData(Function, Expression, Payload, TEXT("created = true\n"))
        : MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("node_create_text"));
    Data->SetStringField(TEXT("asset_path"), Function->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("material_function"));
    Data->SetStringField(TEXT("graph_name"), TEXT("MaterialFunctionGraph"));
    Data->SetBoolField(TEXT("created"), !bDryRun && Expression != nullptr);
    if (bDryRun)
    {
        SetTextPayload(Data, FString::Printf(TEXT("created = dry_run\nNode.Class = %s\nNode.Pos = %d,%d\n"), *ExpressionClass->GetName(), X, Y));
    }
    if (Expression != nullptr)
    {
        Data->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(Expression));
        Data->SetStringField(TEXT("node_alias"), MaterialNodeAlias(Function, Expression));
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, Diagnostics.Num() == 0);
    Response->SetObjectField(TEXT("data"), Data);
    Response->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Response;
}
}
