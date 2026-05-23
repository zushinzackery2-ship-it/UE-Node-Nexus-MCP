#include "Patch/UeNodeNexusBridgeMaterialFunctionPatchShared.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "MaterialExpressionIO.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "UeNodeNexusBridgeMaterialCustomParamApply.h"
#include "Patch/UeNodeNexusBridgeMaterialFunctionPatchContext.h"
#include "NodeInterface/UeNodeNexusBridgeMaterialNodeInterfaceShared.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"

namespace UeNodeNexusBridge
{
static bool ApplyFunctionConnect(UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, TArray<TSharedPtr<FJsonValue>>& Diagnostics, const FMaterialFunctionPatchContext& Context)
{
    const FString FromNodeId = ReadFunctionPatchNodeRef(Op, TEXT("from_node_id"), TEXT("from_node"));
    const FString ToNodeId = ReadFunctionPatchNodeRef(Op, TEXT("to_node_id"), TEXT("to_node"));
    FString FromPinId;
    FString ToPinId;
    if (!Op->TryGetStringField(TEXT("from_pin_id"), FromPinId))
    {
        Op->TryGetStringField(TEXT("from_pin"), FromPinId);
    }
    if (!Op->TryGetStringField(TEXT("to_pin_id"), ToPinId))
    {
        Op->TryGetStringField(TEXT("to_pin"), ToPinId);
    }
    if (FromNodeId.IsEmpty() || FromPinId.IsEmpty() || ToNodeId.IsEmpty() || ToPinId.IsEmpty())
    {
        AddFunctionPatchDiagnostic(Diagnostics, TEXT("invalid_connect_request"), TEXT("connect_pins requires from_node_id/from_pin_id/to_node_id/to_pin_id"), Function);
        return false;
    }

    UMaterialExpression* FromExpression = ResolveFunctionPatchNode(Function, FromNodeId, Context);
    bool bFromInput = false;
    int32 FromOutputIndex = INDEX_NONE;
    if (!ResolveMaterialOutputPin(FromExpression, FromPinId, bFromInput, FromOutputIndex) || bFromInput)
    {
        AddFunctionPatchDiagnostic(Diagnostics, TEXT("invalid_source_pin"), FString::Printf(TEXT("Source pin is not a valid output: node=%s pin=%s outputs=%s"), *FromNodeId, *FromPinId, *DescribeMaterialOutputPins(FromExpression)), Function);
        return false;
    }

    UMaterialExpression* ToExpression = ResolveFunctionPatchNode(Function, ToNodeId, Context);
    FExpressionInput* ToInput = ResolveMaterialInputPin(ToExpression, ToPinId);
    if (FromExpression == nullptr || ToExpression == nullptr || ToInput == nullptr || !FromExpression->GetOutputs().IsValidIndex(FromOutputIndex))
    {
        AddFunctionPatchDiagnostic(Diagnostics, TEXT("pin_resolution_failed"), FString::Printf(TEXT("Could not resolve material function link: %s.%s -> %s.%s"), *FromNodeId, *FromPinId, *ToNodeId, *ToPinId), Function);
        return false;
    }

    AppendMaterialDiff(Diff, TEXT("links_added"), MakeFunctionPatchLinkJson(MaterialExpressionNodeId(FromExpression), FromPinId, MaterialExpressionNodeId(ToExpression), ToPinId));
    if (!bDryRun)
    {
        ToInput->Connect(FromOutputIndex, FromExpression);
    }
    return true;
}

static bool ApplyFunctionDisconnect(UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, const FMaterialFunctionPatchContext& Context)
{
    const FString ToNodeId = ReadFunctionPatchNodeRef(Op, TEXT("to_node_id"), TEXT("to_node"));
    FString ToPinId;
    if (!Op->TryGetStringField(TEXT("to_pin_id"), ToPinId))
    {
        Op->TryGetStringField(TEXT("to_pin"), ToPinId);
    }
    if (ToNodeId.IsEmpty() || ToPinId.IsEmpty())
    {
        return false;
    }

    UMaterialExpression* ToExpression = ResolveFunctionPatchNode(Function, ToNodeId, Context);
    FExpressionInput* Input = ResolveMaterialInputPin(ToExpression, ToPinId);
    if (Input == nullptr || Input->Expression == nullptr)
    {
        return false;
    }

    UMaterialExpression* FromExpression = Input->Expression;
    AppendMaterialDiff(Diff, TEXT("links_removed"), MakeFunctionPatchLinkJson(MaterialExpressionNodeId(FromExpression), FString::Printf(TEXT("%s:out:%d"), *MaterialExpressionNodeId(FromExpression), Input->OutputIndex), MaterialExpressionNodeId(ToExpression), ToPinId));
    if (!bDryRun)
    {
        Input->Expression = nullptr;
        Input->OutputIndex = 0;
    }
    return true;
}

static bool ApplyFunctionCreateNode(UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, TArray<TSharedPtr<FJsonValue>>& Diagnostics, FMaterialFunctionPatchContext& Context)
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
        AddFunctionPatchDiagnostic(Diagnostics, TEXT("unknown_node_class"), FString::Printf(TEXT("Material expression class not found: %s"), *ClassPath), Function);
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
        UMaterialExpression* NewExpression = UMaterialEditingLibrary::CreateMaterialExpressionInFunction(Function, ExpressionClass, X, Y);
        const TSharedPtr<FJsonObject>* Params = nullptr;
        if (NewExpression != nullptr && Op->TryGetObjectField(TEXT("params"), Params) && Params != nullptr)
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Params)->Values)
            {
                FString FailureReason;
                if (!ApplyMaterialExpressionParamJsonValue(nullptr, NewExpression, Pair.Key, Pair.Value, false, Diff, FailureReason))
                {
                    AddFunctionPatchDiagnostic(Diagnostics, TEXT("param_import_failed"), FailureReason.IsEmpty() ? Pair.Key : FailureReason, Function);
                    return false;
                }
            }
            NewExpression->PostEditChange();
        }
        Item->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(NewExpression));
        Item->SetStringField(TEXT("node_alias"), MaterialNodeAlias(Function, NewExpression));
        if (!ClientId.IsEmpty())
        {
            Context.ClientNodes.Add(ClientId, NewExpression);
        }
    }
    AppendMaterialDiff(Diff, TEXT("nodes_created"), Item);
    return true;
}

bool ApplyMaterialFunctionPatchOperation(UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, TArray<TSharedPtr<FJsonValue>>& Diagnostics, FMaterialFunctionPatchContext& Context)
{
    FString OpName;
    if (!Op->TryGetStringField(TEXT("op"), OpName))
    {
        AddFunctionPatchDiagnostic(Diagnostics, TEXT("missing_patch_op"), TEXT("Material function patch operation is missing op"), Function);
        return false;
    }
    if (OpName == TEXT("connect_pins"))
    {
        return ApplyFunctionConnect(Function, Op, bDryRun, Diff, Diagnostics, Context);
    }
    if (OpName == TEXT("disconnect_pins"))
    {
        return ApplyFunctionDisconnect(Function, Op, bDryRun, Diff, Context);
    }
    if (OpName == TEXT("create_node"))
    {
        return ApplyFunctionCreateNode(Function, Op, bDryRun, Diff, Diagnostics, Context);
    }

    FString NodeId;
    if (!Op->TryGetStringField(TEXT("node_id"), NodeId))
    {
        Op->TryGetStringField(TEXT("node"), NodeId);
    }
    UMaterialExpression* Expression = !NodeId.IsEmpty() ? ResolveFunctionPatchNode(Function, NodeId, Context) : nullptr;
    if (Expression == nullptr)
    {
        AddFunctionPatchDiagnostic(Diagnostics, TEXT("node_not_found"), FString::Printf(TEXT("Material function node was not found: %s"), *NodeId), Function);
        return false;
    }
    if (OpName == TEXT("delete_node"))
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(Expression));
        AppendMaterialDiff(Diff, TEXT("nodes_deleted"), Item);
        if (!bDryRun)
        {
            UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Function, Expression);
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
        if (!ApplyMaterialExpressionParamJsonValue(nullptr, Expression, Name, Op->TryGetField(TEXT("value")), bDryRun, Diff, FailureReason))
        {
            AddFunctionPatchDiagnostic(Diagnostics, TEXT("param_import_failed"), FailureReason.IsEmpty() ? Name : FailureReason, Function);
            return false;
        }
        return true;
    }
    return false;
}
}
