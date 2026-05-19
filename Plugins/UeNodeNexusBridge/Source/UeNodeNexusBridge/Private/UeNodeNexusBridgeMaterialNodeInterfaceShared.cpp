#include "UeNodeNexusBridgeMaterialNodeInterfaceShared.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
FString ShortMaterialExpressionClass(UMaterialExpression* Expression)
{
    FString Name = Expression ? Expression->GetClass()->GetName() : FString();
    Name.RemoveFromStart(TEXT("MaterialExpression"));
    return Name;
}

FString ReadObjectPropertyText(UObject* Object, const FName& Name)
{
    FProperty* Property = Object ? Object->GetClass()->FindPropertyByName(Name) : nullptr;
    FString Value;
    if (Property != nullptr)
    {
        Property->ExportText_InContainer(0, Value, Object, nullptr, Object, PPF_None);
        Value.RemoveFromStart(TEXT("("));
        Value.RemoveFromEnd(TEXT(")"));
    }
    return Value;
}

FString MaterialNodeAlias(UMaterial* Material, UMaterialExpression* Target)
{
    TMap<FString, int32> Counts;
    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Material->GetExpressions())
    {
        UMaterialExpression* Expression = ExpressionPtr.Get();
        if (Expression == nullptr)
        {
            continue;
        }

        const FString ParamName = ReadObjectPropertyText(Expression, TEXT("ParameterName"));
        const bool bNamedParam = !ParamName.IsEmpty() && !ParamName.Equals(TEXT("None"), ESearchCase::IgnoreCase);
        const FString Base = bNamedParam ? ParamName : ShortMaterialExpressionClass(Expression);
        const int32 Index = Counts.FindOrAdd(Base)++;
        const FString Alias = bNamedParam && Index == 0 ? Base : FString::Printf(TEXT("%s_%02d"), *Base, Index);
        if (Expression == Target)
        {
            return Alias;
        }
    }
    return FString();
}

UMaterialExpression* ResolveMaterialInterfaceNode(UMaterial* Material, const FString& NodeId)
{
    if (UMaterialExpression* Found = FindMaterialExpression(Material, NodeId))
    {
        return Found;
    }
    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Material->GetExpressions())
    {
        UMaterialExpression* Expression = ExpressionPtr.Get();
        if (Expression != nullptr && (MaterialNodeAlias(Material, Expression).Equals(NodeId, ESearchCase::IgnoreCase) || Expression->GetName().Equals(NodeId, ESearchCase::IgnoreCase)))
        {
            return Expression;
        }
    }
    return nullptr;
}

FString MaterialOutputName(UMaterialExpression* Expression, int32 Index)
{
    TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
    if (!Outputs.IsValidIndex(Index) || Outputs[Index].OutputName.IsNone())
    {
        return FString::FromInt(Index);
    }
    return Outputs[Index].OutputName.ToString();
}
}
