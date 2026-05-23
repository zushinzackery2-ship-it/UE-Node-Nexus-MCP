#include "Patch/UeNodeNexusBridgeMaterialPatchNodeOps.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "NodeInterface/UeNodeNexusBridgeMaterialNodeInterfaceShared.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchContext.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchShared.h"

namespace UeNodeNexusBridge
{
bool ApplyMaterialPatchCreateNode(UMaterial* Material, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, TArray<TSharedPtr<FJsonValue>>& Diagnostics, FMaterialPatchContext& Context)
{
    FString ClassPath;
    FString NodeClass;
    int32 X = 0;
    int32 Y = 0;
    if (!ReadMaterialPosition(Op, X, Y))
    {
        return false;
    }
    if (!Op->TryGetStringField(TEXT("class_path"), ClassPath))
    {
        Op->TryGetStringField(TEXT("node_class"), NodeClass);
        ClassPath = NodeClass;
    }
    UClass* ExpressionClass = ResolveMaterialExpressionClass(ClassPath);
    if (ExpressionClass == nullptr)
    {
        return false;
    }

    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    FString ClientId;
    if (Op->TryGetStringField(TEXT("client_id"), ClientId))
    {
        Item->SetStringField(TEXT("client_id"), ClientId);
    }
    Item->SetStringField(TEXT("class_path"), ExpressionClass->GetPathName());
    if (!bDryRun)
    {
        UMaterialExpression* NewExpression = UMaterialEditingLibrary::CreateMaterialExpression(Material, ExpressionClass, X, Y);
        const TSharedPtr<FJsonObject>* Params = nullptr;
        if (NewExpression != nullptr && Op->TryGetObjectField(TEXT("params"), Params) && Params != nullptr)
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Params)->Values)
            {
                FString FailureReason;
                if (!ApplyMaterialExpressionParamJsonValue(Material, NewExpression, Pair.Key, Pair.Value, false, Diff, FailureReason))
                {
                    AddMaterialPatchDiagnostic(Diagnostics, TEXT("param_import_failed"), FailureReason.IsEmpty() ? Pair.Key : FailureReason, Material);
                    return false;
                }
            }
            NewExpression->PostEditChange();
        }
        Item->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(NewExpression));
        Item->SetStringField(TEXT("node_alias"), MaterialNodeAlias(Material, NewExpression));
        if (!ClientId.IsEmpty())
        {
            Context.ClientNodes.Add(ClientId, NewExpression);
        }
    }
    AppendMaterialDiff(Diff, TEXT("nodes_created"), Item);
    return true;
}

bool ApplyMaterialPatchNodeOperation(UMaterial* Material, const FString& OpName, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, TArray<TSharedPtr<FJsonValue>>& Diagnostics, FMaterialPatchContext& Context)
{
    FString NodeId;
    if (!Op->TryGetStringField(TEXT("node_id"), NodeId))
    {
        Op->TryGetStringField(TEXT("node"), NodeId);
    }
    UMaterialExpression* Expression = !NodeId.IsEmpty() ? ResolveMaterialPatchNode(Material, NodeId, Context) : nullptr;
    if (Expression == nullptr)
    {
        return false;
    }
    if (OpName == TEXT("delete_node"))
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(Expression));
        AppendMaterialDiff(Diff, TEXT("nodes_deleted"), Item);
        if (!bDryRun)
        {
            UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expression);
        }
        return true;
    }
    if (OpName == TEXT("set_node_position"))
    {
        int32 X = 0;
        int32 Y = 0;
        if (!ReadMaterialPosition(Op, X, Y))
        {
            return false;
        }
        AddMaterialParamChange(Diff, MaterialExpressionNodeId(Expression), TEXT("position"), FString::Printf(TEXT("%d,%d"), Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY), FString::Printf(TEXT("%d,%d"), X, Y));
        if (!bDryRun)
        {
            Expression->Modify();
            Expression->MaterialExpressionEditorX = X;
            Expression->MaterialExpressionEditorY = Y;
        }
        return true;
    }
    if (OpName == TEXT("set_node_param"))
    {
        FString Name;
        if (!Op->TryGetStringField(TEXT("name"), Name))
        {
            return false;
        }
        FString FailureReason;
        if (!ApplyMaterialExpressionParamJsonValue(Material, Expression, Name, Op->TryGetField(TEXT("value")), bDryRun, Diff, FailureReason))
        {
            AddMaterialPatchDiagnostic(Diagnostics, TEXT("param_import_failed"), FailureReason.IsEmpty() ? Name : FailureReason, Material);
            return false;
        }
        return true;
    }
    return false;
}
}
