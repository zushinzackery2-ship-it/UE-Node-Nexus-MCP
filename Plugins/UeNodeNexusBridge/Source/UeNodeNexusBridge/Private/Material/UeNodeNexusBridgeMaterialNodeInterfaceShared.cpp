#include "UeNodeNexusBridgeMaterialNodeInterfaceShared.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
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

static FString MaterialExpressionAliasBase(UMaterialExpression* Expression)
{
    const FString ParamName = ReadObjectPropertyText(Expression, TEXT("ParameterName"));
    if (!ParamName.IsEmpty() && !ParamName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
    {
        return ParamName;
    }

    const FString InputName = ReadObjectPropertyText(Expression, TEXT("InputName"));
    if (!InputName.IsEmpty() && !InputName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
    {
        return InputName;
    }

    const FString OutputName = ReadObjectPropertyText(Expression, TEXT("OutputName"));
    if (!OutputName.IsEmpty() && !OutputName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
    {
        return OutputName;
    }

    return ShortMaterialExpressionClass(Expression);
}

static FString MaterialNodeAliasFromExpressions(TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions, UMaterialExpression* Target)
{
    TMap<FString, int32> Counts;
    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Expressions)
    {
        UMaterialExpression* Expression = ExpressionPtr.Get();
        if (Expression == nullptr)
        {
            continue;
        }

        const FString Base = MaterialExpressionAliasBase(Expression);
        const int32 Index = Counts.FindOrAdd(Base)++;
        const FString Alias = Index == 0 && Base != ShortMaterialExpressionClass(Expression) ? Base : FString::Printf(TEXT("%s_%02d"), *Base, Index);
        if (Expression == Target)
        {
            return Alias;
        }
    }
    return FString();
}

FString MaterialNodeAlias(UMaterial* Material, UMaterialExpression* Target)
{
    return Material != nullptr ? MaterialNodeAliasFromExpressions(Material->GetExpressions(), Target) : FString();
}

FString MaterialNodeAlias(UMaterialFunction* Function, UMaterialExpression* Target)
{
    return Function != nullptr ? MaterialNodeAliasFromExpressions(Function->GetExpressions(), Target) : FString();
}

static UMaterialExpression* ResolveMaterialInterfaceNodeFromExpressions(TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions, const FString& NodeId, TFunctionRef<FString(UMaterialExpression*)> AliasFor)
{
    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Expressions)
    {
        UMaterialExpression* Expression = ExpressionPtr.Get();
        if (Expression == nullptr)
        {
            continue;
        }
        if (MaterialExpressionNodeId(Expression).Equals(NodeId, ESearchCase::IgnoreCase)
            || Expression->GetPathName().Equals(NodeId, ESearchCase::IgnoreCase)
            || Expression->GetName().Equals(NodeId, ESearchCase::IgnoreCase)
            || AliasFor(Expression).Equals(NodeId, ESearchCase::IgnoreCase))
        {
            return Expression;
        }
    }
    return nullptr;
}

UMaterialExpression* ResolveMaterialInterfaceNode(UMaterial* Material, const FString& NodeId)
{
    if (Material == nullptr)
    {
        return nullptr;
    }
    return ResolveMaterialInterfaceNodeFromExpressions(Material->GetExpressions(), NodeId, [Material](UMaterialExpression* Expression)
    {
        return MaterialNodeAlias(Material, Expression);
    });
}

UMaterialExpression* ResolveMaterialInterfaceNode(UMaterialFunction* Function, const FString& NodeId)
{
    if (Function == nullptr)
    {
        return nullptr;
    }
    return ResolveMaterialInterfaceNodeFromExpressions(Function->GetExpressions(), NodeId, [Function](UMaterialExpression* Expression)
    {
        return MaterialNodeAlias(Function, Expression);
    });
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
