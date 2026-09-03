#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeMaterialApply.h"

#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialFunction.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

namespace UeNodeNexusBridge::Transcode
{
namespace
{
bool SetSyntheticDeclaration(UObject* Owner, UMaterialExpression* Expression, const FString& Value)
{
    UMaterialExpressionNamedRerouteUsage* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Expression);
    if (Usage == nullptr)
    {
        return false;
    }
    for (const TObjectPtr<UMaterialExpression>& Candidate : OwnerExpressions(Owner))
    {
        UMaterialExpressionNamedRerouteDeclaration* Declaration = Cast<UMaterialExpressionNamedRerouteDeclaration>(Candidate.Get());
        if (Declaration && Declaration->Name.ToString().Equals(Value, ESearchCase::IgnoreCase))
        {
            Usage->Modify();
            Usage->Declaration = Declaration;
            Usage->DeclarationGuid = Declaration->VariableGuid;
            return true;
        }
    }
    return false;
}

bool SetExpressionParam(UObject* Owner, UMaterialExpression* Expression, const FString& Name, const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
    FString Text;
    if (!Value.IsValid() || Value->IsNull())
    {
        FProperty* Property = Expression->GetClass()->FindPropertyByName(FName(*Name));
        if (Property == nullptr)
        {
            OutError = FString::Printf(TEXT("unknown property %s"), *Name);
            return false;
        }
        Text = ExportPropertyValue(Expression->GetClass()->GetDefaultObject(), Property);
    }
    else if (!Value->TryGetString(Text))
    {
        Text = JsonValueToImportText(Value);
    }
    if (Name == TEXT("DeclarationName"))
    {
        if (!SetSyntheticDeclaration(Owner, Expression, Text))
        {
            OutError = FString::Printf(TEXT("named reroute declaration not found: %s"), *Text);
            return false;
        }
        return true;
    }
    return ImportPropertyValue(Expression, Name, Text, OutError);
}

void FinishExpressionEdit(UMaterialExpression* Expression)
{
    if (UMaterialExpressionMaterialFunctionCall* Call = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
    {
        Call->UpdateFromFunctionResource();
    }
    Expression->PostEditChange();
}

bool ApplyCreate(UObject* Owner, const TSharedPtr<FJsonObject>& Op, int32 Index, FApplyContext& Context)
{
    const FString Id = ReadMaterialOpString(Op, TEXT("id"));
    UClass* Class = ResolveMaterialExpressionClass(ReadMaterialOpString(Op, TEXT("class")));
    if (Class == nullptr)
    {
        Context.Fail(Index, TEXT("unknown_class"), FString::Printf(TEXT("unknown material expression class: %s"), *ReadMaterialOpString(Op, TEXT("class"))));
        return false;
    }
    if (Context.bDryRun)
    {
        Context.Ids.Add(Id, TEXT("dry-run"));
        return true;
    }
    int32 X = 0;
    int32 Y = 0;
    Op->TryGetNumberField(TEXT("x"), X);
    Op->TryGetNumberField(TEXT("y"), Y);
    UMaterialExpression* Expression = nullptr;
    if (UMaterial* Material = Cast<UMaterial>(Owner))
    {
        Expression = UMaterialEditingLibrary::CreateMaterialExpression(Material, Class, X, Y);
    }
    else if (UMaterialFunction* Function = Cast<UMaterialFunction>(Owner))
    {
        Expression = UMaterialEditingLibrary::CreateMaterialExpressionInFunction(Function, Class, X, Y);
    }
    if (Expression == nullptr)
    {
        Context.Fail(Index, TEXT("create_failed"), FString::Printf(TEXT("could not create %s"), *Class->GetName()));
        return false;
    }
    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (Op->TryGetObjectField(TEXT("params"), Params) && Params != nullptr)
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Params)->Values)
        {
            FString Error;
            if (!SetExpressionParam(Owner, Expression, Pair.Key, Pair.Value, Error))
            {
                Context.Fail(Index, TEXT("param_failed"), FString::Printf(TEXT("%s: %s"), *Id, *Error));
            }
        }
    }
    FinishExpressionEdit(Expression);
    const FString Guid = Expression->GetMaterialExpressionId().ToString(EGuidFormats::DigitsWithHyphens);
    Context.Ids.Add(Id, Guid);
    Context.Created.Add(Id, Guid);
    Context.bChanged = true;
    return true;
}

void ApplyNodeVerb(UObject* Owner, const FString& Verb, const TSharedPtr<FJsonObject>& Op, int32 Index, FApplyContext& Context)
{
    const FString Id = ReadMaterialOpString(Op, TEXT("id"));
    UMaterialExpression* Expression = ResolveMaterialNode(Owner, Context, Id);
    if (Expression == nullptr)
    {
        if (!Context.bDryRun || !Context.Ids.Contains(Id))
        {
            Context.Fail(Index, TEXT("node_not_found"), FString::Printf(TEXT("node not found: %s"), *Id));
        }
        return;
    }
    FString Error;
    if (Verb == TEXT("delete_node"))
    {
        if (UMaterial* Material = Cast<UMaterial>(Owner))
        {
            UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expression);
        }
        else if (UMaterialFunction* Function = Cast<UMaterialFunction>(Owner))
        {
            UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Function, Expression);
        }
        Context.bChanged = true;
    }
    else if (Verb == TEXT("set_node_param"))
    {
        if (!SetExpressionParam(Owner, Expression, ReadMaterialOpString(Op, TEXT("name")), Op->TryGetField(TEXT("value")), Error))
        {
            Context.Fail(Index, TEXT("param_failed"), FString::Printf(TEXT("%s: %s"), *Id, *Error));
            return;
        }
        FinishExpressionEdit(Expression);
        Context.bChanged = true;
    }
    else if (Verb == TEXT("set_node_position"))
    {
        int32 X = 0;
        int32 Y = 0;
        Op->TryGetNumberField(TEXT("x"), X);
        Op->TryGetNumberField(TEXT("y"), Y);
        Expression->Modify();
        Expression->MaterialExpressionEditorX = X;
        Expression->MaterialExpressionEditorY = Y;
        Context.bChanged = true;
    }
    else
    {
        Context.Fail(Index, TEXT("unsupported_verb"), FString::Printf(TEXT("%s is not supported on material graphs"), *Verb));
    }
}
}

void ApplyMaterialPlan(UObject* Owner, const TArray<TSharedPtr<FJsonValue>>& Plan, FApplyContext& Context)
{
    if (UMaterial* Material = Cast<UMaterial>(Owner); Material != nullptr && !Context.bDryRun && Plan.Num() > 0)
    {
        // never edit a graph whose shader maps are still compiling (see MaterialPatchOps)
        Material->CancelOutstandingCompilation();
    }
    for (int32 Index = 0; Index < Plan.Num(); ++Index)
    {
        const TSharedPtr<FJsonObject> Op = Plan[Index].IsValid() ? Plan[Index]->AsObject() : nullptr;
        if (!Op.IsValid())
        {
            Context.Fail(Index, TEXT("invalid_verb"), TEXT("plan entries must be objects"));
            continue;
        }
        const FString Verb = ReadMaterialOpString(Op, TEXT("op"));
        FString Error;
        if (Verb == TEXT("set_asset_prop"))
        {
            if (!Context.bDryRun && !ImportPropertyValue(Owner, ReadMaterialOpString(Op, TEXT("name")), ReadMaterialOpString(Op, TEXT("value")), Error))
            {
                Context.Fail(Index, TEXT("prop_failed"), Error);
            }
            Context.bChanged |= !Context.bDryRun;
        }
        else if (Verb == TEXT("create_node"))
        {
            ApplyCreate(Owner, Op, Index, Context);
        }
        else if (Verb == TEXT("connect_pins") || Verb == TEXT("disconnect_pins"))
        {
            ApplyMaterialLink(Owner, Op, Index, Context, Verb == TEXT("connect_pins"));
        }
        else if (Verb == TEXT("refresh_function_calls"))
        {
            RefreshMaterialFunctionCalls(Owner, ReadMaterialOpString(Op, TEXT("function")), Context);
        }
        else
        {
            ApplyNodeVerb(Owner, Verb, Op, Index, Context);
        }
    }
}

void ApplyGenericPlan(UObject* Asset, const TArray<TSharedPtr<FJsonValue>>& Plan, FApplyContext& Context)
{
    for (int32 Index = 0; Index < Plan.Num(); ++Index)
    {
        const TSharedPtr<FJsonObject> Op = Plan[Index].IsValid() ? Plan[Index]->AsObject() : nullptr;
        const FString Verb = Op.IsValid() ? ReadMaterialOpString(Op, TEXT("op")) : FString();
        if (Verb != TEXT("set_asset_prop"))
        {
            Context.Fail(Index, TEXT("unsupported_verb"), FString::Printf(TEXT("%s is not supported on property-bag assets"), *Verb));
            continue;
        }
        if (Context.bDryRun)
        {
            continue;
        }
        FString Error;
        if (!ImportPropertyValue(Asset, ReadMaterialOpString(Op, TEXT("name")), ReadMaterialOpString(Op, TEXT("value")), Error))
        {
            Context.Fail(Index, TEXT("prop_failed"), Error);
            continue;
        }
        Context.bChanged = true;
    }
}
}
