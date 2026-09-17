#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeMaterialApply.h"

#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"

namespace UeNodeNexusBridge::Transcode
{
TConstArrayView<TObjectPtr<UMaterialExpression>> OwnerExpressions(UObject* Owner)
{
    if (UMaterial* Material = Cast<UMaterial>(Owner))
    {
        return Material->GetExpressions();
    }
    if (UMaterialFunction* Function = Cast<UMaterialFunction>(Owner))
    {
        return Function->GetExpressions();
    }
    return TConstArrayView<TObjectPtr<UMaterialExpression>>();
}

UMaterialExpression* ResolveMaterialNode(UObject* Owner, const FApplyContext& Context, const FString& LocalId)
{
    const FString* Guid = Context.Ids.Find(LocalId);
    if (Guid == nullptr)
    {
        return nullptr;
    }
    const TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = OwnerExpressions(Owner);
    for (const TObjectPtr<UMaterialExpression>& Expression : Expressions)
    {
        if (Expression && MaterialExpressionKey(Expression.Get(), Expressions).Equals(*Guid, ESearchCase::IgnoreCase))
        {
            return Expression.Get();
        }
    }
    return nullptr;
}

FString ReadMaterialOpString(const TSharedPtr<FJsonObject>& Op, const TCHAR* Field, const FString& Default)
{
    FString Value;
    return Op->TryGetStringField(Field, Value) ? Value : Default;
}

bool ApplyMaterialLink(UObject* Owner, const TSharedPtr<FJsonObject>& Op, int32 Index, FApplyContext& Context, bool bConnect)
{
    const FString FromId = ReadMaterialOpString(Op, TEXT("from"));
    const FString ToId = ReadMaterialOpString(Op, TEXT("to"));
    const FString FromPin = ReadMaterialOpString(Op, TEXT("from_pin"), TEXT("0"));
    const FString ToPin = ReadMaterialOpString(Op, TEXT("to_pin"), TEXT("0"));
    UMaterial* Material = Cast<UMaterial>(Owner);
    UMaterialExpression* From = bConnect ? ResolveMaterialNode(Owner, Context, FromId) : nullptr;
    if (bConnect && From == nullptr && !Context.bDryRun)
    {
        Context.Fail(Index, TEXT("node_not_found"), FString::Printf(TEXT("source node not found: %s"), *FromId));
        return false;
    }
    FExpressionInput* Input = nullptr;
    if (Material != nullptr && ToId == TEXT("out") && !Context.Ids.Contains(ToId))
    {
        Input = ResolveMaterialOutputInput(Material, ToPin);
    }
    else if (UMaterialExpression* To = ResolveMaterialNode(Owner, Context, ToId))
    {
        Input = ResolveMaterialInputPin(To, ToPin);
    }
    if (Context.bDryRun)
    {
        return true;
    }
    if (Input == nullptr)
    {
        Context.Fail(Index, TEXT("pin_not_found"), FString::Printf(TEXT("target pin not found: %s.%s"), *ToId, *ToPin));
        return false;
    }
    Owner->Modify();
    if (!bConnect)
    {
        Input->Expression = nullptr;
        Input->OutputIndex = 0;
        Context.bChanged = true;
        return true;
    }
    bool bIsInput = false;
    int32 OutputIndex = INDEX_NONE;
    if (!ResolveMaterialOutputPin(From, FromPin, bIsInput, OutputIndex) || bIsInput)
    {
        Context.Fail(Index, TEXT("pin_not_found"), FString::Printf(TEXT("source pin not found: %s.%s (outputs: %s)"), *FromId, *FromPin, *DescribeMaterialOutputPins(From)));
        return false;
    }
    Input->Connect(OutputIndex, From);
    Context.bChanged = true;
    return true;
}

void RefreshMaterialFunctionCalls(UObject* Owner, const FString& FunctionPath, FApplyContext& Context)
{
    for (const TObjectPtr<UMaterialExpression>& Expression : OwnerExpressions(Owner))
    {
        UMaterialExpressionMaterialFunctionCall* Call = Cast<UMaterialExpressionMaterialFunctionCall>(Expression.Get());
        if (Call == nullptr || Call->MaterialFunction == nullptr)
        {
            continue;
        }
        const FString Path = Call->MaterialFunction->GetPathName();
        const bool bMatches = Path == FunctionPath || Path.StartsWith(FunctionPath + TEXT(".")) || FunctionPath.StartsWith(Path + TEXT("."));
        if (bMatches && !Context.bDryRun)
        {
            Call->Modify();
            Call->UpdateFromFunctionResource();
            Context.bChanged = true;
        }
    }
}
}
