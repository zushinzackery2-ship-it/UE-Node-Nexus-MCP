#include "UeNodeNexusBridgeNodeInterfaceOps.h"

#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialNodeInterfaceShared.h"
#include "UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
static bool ReadPositionPair(const TSharedPtr<FJsonObject>& Payload, bool bOffset, int32& OutX, int32& OutY)
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

static TSharedPtr<FJsonObject> MakePositionData(UMaterial* Material, UMaterialExpression* Expression, const FString& Prefix)
{
    const FString Alias = MaterialNodeAlias(Material, Expression);
    FString Text = Prefix;
    Text += FString::Printf(TEXT("Node.Name = %s\n"), *Alias);
    Text += FString::Printf(TEXT("Node.Id = %s\n"), *Alias);
    Text += FString::Printf(TEXT("Node.RealId = %s\n"), *MaterialExpressionNodeId(Expression));
    Text += FString::Printf(TEXT("Node.Pos = %d,%d\n"), Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("node_position_text"));
    Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("material"));
    Data->SetStringField(TEXT("graph_name"), TEXT("MaterialGraph"));
    Data->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(Expression));
    Data->SetStringField(TEXT("node_alias"), Alias);
    SetTextPayload(Data, Text);
    return Data;
}

static UMaterialExpression* ReadMaterialNodeOrError(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutResponse)
{
    FString NodeId;
    if (!Payload->TryGetStringField(TEXT("node_id"), NodeId))
    {
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("node_id is required")));
        return nullptr;
    }
    UMaterialExpression* Expression = ResolveMaterialInterfaceNode(Material, NodeId);
    if (Expression == nullptr)
    {
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("node_not_found"), TEXT("Material node was not found")));
    }
    return Expression;
}

TSharedPtr<FJsonObject> HandleMaterialNodePositionSet(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload, bool bOffset)
{
    TSharedPtr<FJsonObject> Error;
    UMaterialExpression* Expression = ReadMaterialNodeOrError(Operation, RequestId, Material, Payload, Error);
    if (Expression == nullptr)
    {
        return Error;
    }

    int32 X = 0;
    int32 Y = 0;
    if (!ReadPositionPair(Payload, bOffset, X, Y))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), bOffset ? TEXT("dx and dy are required") : TEXT("x and y are required")));
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
        Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("UE Node Nexus Material Node Position")));
        Material->Modify();
        Expression->Modify();
        Expression->MaterialExpressionEditorX = AfterX;
        Expression->MaterialExpressionEditorY = AfterY;
        Material->MarkPackageDirty();
    }

    FString Prefix = FString::Printf(TEXT("moved = %s\nNode.Pos.Before = %d,%d\nNode.Pos.After = %d,%d\n"), bDryRun ? TEXT("dry_run") : TEXT("true"), BeforeX, BeforeY, AfterX, AfterY);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), MakePositionData(Material, Expression, Prefix));
    return Response;
}

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
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Params)->Values)
            {
                FProperty* Property = Expression->GetClass()->FindPropertyByName(FName(*Pair.Key));
                FString Value;
                TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
                Wrapper->SetField(TEXT("value"), Pair.Value);
                if (Property != nullptr && Property->HasAnyPropertyFlags(CPF_Edit) && ReadJsonScalarAsString(Wrapper, TEXT("value"), Value))
                {
                    Property->ImportText_InContainer(*Value, Expression, Expression, PPF_None);
                }
            }
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
