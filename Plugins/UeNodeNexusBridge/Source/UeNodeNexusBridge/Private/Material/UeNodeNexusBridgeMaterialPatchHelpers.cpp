#include "UeNodeNexusBridgeMaterialPatchHelpers.h"

#include "Dom/JsonValue.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "UeNodeNexusBridgeMaterialNodeInterfaceShared.h"

namespace UeNodeNexusBridge
{
FString MaterialExpressionNodeId(UMaterialExpression* Expression)
{
    return Expression ? Expression->GetMaterialExpressionId().ToString(EGuidFormats::DigitsWithHyphens) : FString();
}

UMaterialExpression* FindMaterialExpression(UMaterial* Material, const FString& NodeId)
{
    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Material->GetExpressions())
    {
        UMaterialExpression* Expression = ExpressionPtr.Get();
        if (Expression == nullptr)
        {
            continue;
        }
        if (MaterialExpressionNodeId(Expression).Equals(NodeId, ESearchCase::IgnoreCase) || Expression->GetPathName().Equals(NodeId, ESearchCase::IgnoreCase))
        {
            return Expression;
        }
    }
    return nullptr;
}

UClass* ResolveMaterialExpressionClass(const FString& NodeClass)
{
    if (UClass* Direct = LoadClass<UMaterialExpression>(nullptr, *NodeClass))
    {
        return Direct->IsChildOf(UMaterialExpression::StaticClass()) ? Direct : nullptr;
    }
    const FString ShortName = NodeClass.StartsWith(TEXT("MaterialExpression")) ? NodeClass : TEXT("MaterialExpression") + NodeClass;
    return LoadClass<UMaterialExpression>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ShortName));
}

static UMaterialExpressionNamedRerouteDeclaration* FindNamedRerouteDeclaration(UMaterial* Material, const FString& Value)
{
    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Material->GetExpressions())
    {
        UMaterialExpressionNamedRerouteDeclaration* Declaration = Cast<UMaterialExpressionNamedRerouteDeclaration>(ExpressionPtr.Get());
        if (Declaration == nullptr)
        {
            continue;
        }
        if (MaterialNodeAlias(Material, Declaration).Equals(Value, ESearchCase::IgnoreCase)
            || Declaration->Name.ToString().Equals(Value, ESearchCase::IgnoreCase)
            || MaterialExpressionNodeId(Declaration).Equals(Value, ESearchCase::IgnoreCase))
        {
            return Declaration;
        }
    }
    return nullptr;
}

bool TrySetMaterialSyntheticParam(UMaterial* Material, UMaterialExpression* Expression, const FString& Name, const FString& Value, bool bApply, FString& OutOldValue)
{
    UMaterialExpressionNamedRerouteUsage* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Expression);
    if (Usage == nullptr)
    {
        return false;
    }
    if (!Name.Equals(TEXT("DeclarationName"), ESearchCase::IgnoreCase)
        && !Name.Equals(TEXT("Declaration"), ESearchCase::IgnoreCase)
        && !Name.Equals(TEXT("DeclarationAlias"), ESearchCase::IgnoreCase))
    {
        return false;
    }

    OutOldValue = Usage->Declaration ? Usage->Declaration->Name.ToString() : FString();
    UMaterialExpressionNamedRerouteDeclaration* Declaration = FindNamedRerouteDeclaration(Material, Value);
    if (Declaration == nullptr)
    {
        return false;
    }

    if (bApply)
    {
        Usage->Modify();
        Usage->Declaration = Declaration;
        Usage->DeclarationGuid = Declaration->VariableGuid;
    }
    return true;
}

bool ReadMaterialPosition(const TSharedPtr<FJsonObject>& Json, int32& OutX, int32& OutY)
{
    const TSharedPtr<FJsonObject>* Position = nullptr;
    const TSharedPtr<FJsonObject> Source = Json->TryGetObjectField(TEXT("position"), Position) && Position != nullptr ? *Position : Json;
    double X = 0.0;
    double Y = 0.0;
    if (!Source->TryGetNumberField(TEXT("x"), X) || !Source->TryGetNumberField(TEXT("y"), Y))
    {
        return false;
    }
    OutX = static_cast<int32>(X);
    OutY = static_cast<int32>(Y);
    return true;
}

void AppendMaterialDiff(TSharedPtr<FJsonObject> Diff, const FString& Field, const TSharedPtr<FJsonObject>& Item)
{
    TArray<TSharedPtr<FJsonValue>> Items = Diff->GetArrayField(Field);
    Items.Add(MakeShared<FJsonValueObject>(Item));
    Diff->SetArrayField(Field, Items);
}

void AddMaterialParamChange(TSharedPtr<FJsonObject> Diff, const FString& NodeId, const FString& Name, const FString& Before, const FString& After)
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("node_id"), NodeId);
    Item->SetStringField(TEXT("name"), Name);
    Item->SetStringField(TEXT("before"), Before);
    Item->SetStringField(TEXT("after"), After);
    AppendMaterialDiff(Diff, TEXT("params_changed"), Item);
}

}
