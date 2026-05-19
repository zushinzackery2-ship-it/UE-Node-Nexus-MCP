#include "UeNodeNexusBridgeMaterialPatchOps.h"

#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
static bool IsEditableExpressionProperty(FProperty* Property)
{
    return Property != nullptr && Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance);
}

static TSharedPtr<FJsonObject> PropertyToJson(UMaterialExpression* Expression, FProperty* Property)
{
    FString Value;
    Property->ExportText_InContainer(0, Value, Expression, nullptr, Expression, PPF_None);
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("name"), Property->GetName());
    Json->SetStringField(TEXT("type"), Property->GetCPPType());
    Json->SetStringField(TEXT("value"), Value);
    Json->SetBoolField(TEXT("editable"), true);
    return Json;
}

TArray<TSharedPtr<FJsonValue>> BuildMaterialExpressionParams(UMaterialExpression* Expression)
{
    TArray<TSharedPtr<FJsonValue>> Params;
    if (Expression == nullptr)
    {
        return Params;
    }

    for (TFieldIterator<FProperty> It(Expression->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        if (IsEditableExpressionProperty(Property))
        {
            Params.Add(MakeShared<FJsonValueObject>(PropertyToJson(Expression, Property)));
        }
    }
    return Params;
}

TSharedPtr<FJsonObject> HandleMaterialNodeParamsGet(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload)
{
    FString NodeId;
    if (!Payload->TryGetStringField(TEXT("node_id"), NodeId))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("node_id is required")));
        return Response;
    }

    UMaterialExpression* Expression = FindMaterialExpression(Material, NodeId);
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
    Data->SetArrayField(TEXT("params"), BuildMaterialExpressionParams(Expression));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

static bool JsonValueToPropertyText(const TSharedPtr<FJsonValue>& Value, FString& OutText)
{
    if (!Value.IsValid() || Value->Type == EJson::Null)
    {
        return false;
    }
    if (Value->Type == EJson::String)
    {
        OutText = Value->AsString();
        return true;
    }
    if (Value->Type == EJson::Boolean)
    {
        OutText = Value->AsBool() ? TEXT("True") : TEXT("False");
        return true;
    }
    if (Value->Type == EJson::Number)
    {
        OutText = FString::SanitizeFloat(Value->AsNumber());
        return true;
    }
    return false;
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

    UMaterialExpression* Expression = FindMaterialExpression(Material, NodeId);
    if (Expression == nullptr)
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
        Expression->Modify();
    }

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Params)->Values)
    {
        FProperty* Property = Expression->GetClass()->FindPropertyByName(FName(*Pair.Key));
        FString NewValue;
        if (!IsEditableExpressionProperty(Property) || !JsonValueToPropertyText(Pair.Value, NewValue))
        {
            Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("param_write_failed"), Pair.Key, Material->GetPathName(), TEXT("UeNodeNexusBridge"))));
            continue;
        }

        FString OldValue;
        Property->ExportText_InContainer(0, OldValue, Expression, nullptr, Expression, PPF_None);
        AddMaterialParamChange(Diff, MaterialExpressionNodeId(Expression), Pair.Key, OldValue, NewValue);
        if (!bDryRun && Property->ImportText_InContainer(*NewValue, Expression, Expression, PPF_None) == nullptr)
        {
            Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("param_import_failed"), Pair.Key, Material->GetPathName(), TEXT("UeNodeNexusBridge"))));
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
