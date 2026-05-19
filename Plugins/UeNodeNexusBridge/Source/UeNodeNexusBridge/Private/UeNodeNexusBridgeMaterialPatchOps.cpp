#include "UeNodeNexusBridgeMaterialPatchOps.h"

#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"

namespace UeNodeNexusBridge
{
bool IsMaterialPatchAsset(UObject* Asset)
{
    return Cast<UMaterial>(Asset) != nullptr;
}

static TSharedPtr<FJsonObject> MakeMaterialLinkJson(UMaterialExpression* FromExpression, const FString& FromPinId, UMaterialExpression* ToExpression, const FString& ToPinId)
{
    TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
    Link->SetStringField(TEXT("from_node_id"), MaterialExpressionNodeId(FromExpression));
    Link->SetStringField(TEXT("from_pin_id"), FromPinId);
    Link->SetStringField(TEXT("to_node_id"), MaterialExpressionNodeId(ToExpression));
    Link->SetStringField(TEXT("to_pin_id"), ToPinId);
    return Link;
}

static bool ApplyMaterialConnect(UMaterial* Material, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff)
{
    FString FromNodeId;
    FString FromPinId;
    FString ToNodeId;
    FString ToPinId;
    if (!Op->TryGetStringField(TEXT("from_node_id"), FromNodeId) || !Op->TryGetStringField(TEXT("from_pin_id"), FromPinId) || !Op->TryGetStringField(TEXT("to_node_id"), ToNodeId) || !Op->TryGetStringField(TEXT("to_pin_id"), ToPinId))
    {
        return false;
    }

    UMaterialExpression* FromExpression = FindMaterialExpression(Material, FromNodeId);
    UMaterialExpression* ToExpression = FindMaterialExpression(Material, ToNodeId);
    bool bFromInput = false;
    int32 FromOutputIndex = INDEX_NONE;
    if (!ParseMaterialPinId(FromPinId, bFromInput, FromOutputIndex) || bFromInput)
    {
        return false;
    }

    FExpressionInput* ToInput = FindMaterialInput(ToExpression, ToPinId);
    if (FromExpression == nullptr || ToInput == nullptr || !FromExpression->GetOutputs().IsValidIndex(FromOutputIndex))
    {
        return false;
    }

    AppendMaterialDiff(Diff, TEXT("links_added"), MakeMaterialLinkJson(FromExpression, FromPinId, ToExpression, ToPinId));
    if (!bDryRun)
    {
        ToInput->Connect(FromOutputIndex, FromExpression);
    }
    return true;
}

static bool ApplyMaterialDisconnect(UMaterial* Material, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff)
{
    FString ToNodeId;
    FString ToPinId;
    FString FromNodeId;
    FString FromPinId;
    if (!Op->TryGetStringField(TEXT("to_node_id"), ToNodeId) || !Op->TryGetStringField(TEXT("to_pin_id"), ToPinId))
    {
        return false;
    }

    UMaterialExpression* ToExpression = FindMaterialExpression(Material, ToNodeId);
    FExpressionInput* Input = FindMaterialInput(ToExpression, ToPinId);
    if (Input == nullptr || Input->Expression == nullptr)
    {
        return false;
    }

    UMaterialExpression* FromExpression = Input->Expression;
    FromNodeId = MaterialExpressionNodeId(FromExpression);
    FromPinId = FString::Printf(TEXT("%s:out:%d"), *FromNodeId, Input->OutputIndex);
    AppendMaterialDiff(Diff, TEXT("links_removed"), MakeMaterialLinkJson(FromExpression, FromPinId, ToExpression, ToPinId));
    if (!bDryRun)
    {
        Input->Expression = nullptr;
        Input->OutputIndex = 0;
    }
    return true;
}

static bool ApplyMaterialCreateNode(UMaterial* Material, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff)
{
    FString ClassPath;
    int32 X = 0;
    int32 Y = 0;
    if (!Op->TryGetStringField(TEXT("class_path"), ClassPath) || !ReadMaterialPosition(Op, X, Y))
    {
        return false;
    }
    UClass* ExpressionClass = LoadClass<UMaterialExpression>(nullptr, *ClassPath);
    if (ExpressionClass == nullptr || !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass()))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("class_path"), ClassPath);
    if (!bDryRun)
    {
        UMaterialExpression* NewExpression = UMaterialEditingLibrary::CreateMaterialExpression(Material, ExpressionClass, X, Y);
        Item->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(NewExpression));
    }
    AppendMaterialDiff(Diff, TEXT("nodes_created"), Item);
    return true;
}

static bool ApplyMaterialOperation(UMaterial* Material, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff)
{
    FString OpName;
    if (!Op->TryGetStringField(TEXT("op"), OpName))
    {
        return false;
    }
    if (OpName == TEXT("connect_pins"))
    {
        return ApplyMaterialConnect(Material, Op, bDryRun, Diff);
    }
    if (OpName == TEXT("disconnect_pins"))
    {
        return ApplyMaterialDisconnect(Material, Op, bDryRun, Diff);
    }
    if (OpName == TEXT("create_node"))
    {
        return ApplyMaterialCreateNode(Material, Op, bDryRun, Diff);
    }

    FString NodeId;
    UMaterialExpression* Expression = Op->TryGetStringField(TEXT("node_id"), NodeId) ? FindMaterialExpression(Material, NodeId) : nullptr;
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
        FString Value;
        if (!Op->TryGetStringField(TEXT("name"), Name) || !ReadJsonScalarAsString(Op, TEXT("value"), Value))
        {
            return false;
        }
        FProperty* Property = Expression->GetClass()->FindPropertyByName(FName(*Name));
        if (Property == nullptr || !Property->HasAnyPropertyFlags(CPF_Edit))
        {
            return false;
        }
        FString OldValue;
        Property->ExportTextItem_InContainer(OldValue, Expression, nullptr, Expression, PPF_None);
        AddMaterialParamChange(Diff, MaterialExpressionNodeId(Expression), Name, OldValue, Value);
        return bDryRun || Property->ImportText_InContainer(*Value, Expression, Expression, PPF_None) != nullptr;
    }
    return false;
}

TSharedPtr<FJsonObject> HandleMaterialGraphPatch(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload)
{
    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (!Payload->TryGetArrayField(TEXT("operations"), Operations) || Operations == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("operations must be an array")));
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
        Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("UE Node Nexus Material Patch")));
        Material->Modify();
    }

    for (const TSharedPtr<FJsonValue>& Value : *Operations)
    {
        TSharedPtr<FJsonObject> Op = Value->AsObject();
        if (!Op.IsValid() || !ApplyMaterialOperation(Material, Op, bDryRun, Diff))
        {
            Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("patch_operation_failed"), TEXT("Material patch operation failed validation or application"), Material->GetPathName(), TEXT("UeNodeNexusBridge"))));
            continue;
        }
        bChanged = true;
    }

    TSharedPtr<FJsonObject> Compile = MakeCompilePostCheck(bCompileAfter, !bDryRun && bCompileAfter, true, 0, 0);
    if (!bDryRun && bChanged && bCompileAfter)
    {
        UMaterialEditingLibrary::RecompileMaterial(Material);
    }
    else if (!bDryRun && bChanged)
    {
        Material->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> PinIntegrity = BuildMaterialPinIntegrity(Material);
    const bool bOk = Diagnostics.Num() == 0 && PinIntegrity->GetBoolField(TEXT("ok"));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bOk);
    Response->SetObjectField(TEXT("data"), MakeWriteData(bDryRun, !bDryRun && bChanged, bChanged, Diff, PinIntegrity, Compile, MakeDirtyState(Material)));
    Response->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Response;
}
}
