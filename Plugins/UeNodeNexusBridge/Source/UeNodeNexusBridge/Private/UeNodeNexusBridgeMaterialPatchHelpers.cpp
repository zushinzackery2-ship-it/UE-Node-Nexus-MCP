#include "UeNodeNexusBridgeMaterialPatchHelpers.h"

#include "Dom/JsonValue.h"
#include "MaterialExpressionIO.h"
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

bool ParseMaterialPinId(const FString& PinId, bool& bOutInput, int32& OutIndex)
{
    TArray<FString> Parts;
    PinId.ParseIntoArray(Parts, TEXT(":"));
    if (Parts.Num() < 3)
    {
        return false;
    }
    bOutInput = Parts[Parts.Num() - 2] == TEXT("in");
    const bool bOutput = Parts[Parts.Num() - 2] == TEXT("out");
    OutIndex = FCString::Atoi(*Parts.Last());
    return (bOutInput || bOutput) && OutIndex >= 0;
}

FExpressionInput* FindMaterialInput(UMaterialExpression* Expression, const FString& PinId)
{
    if (Expression == nullptr)
    {
        return nullptr;
    }

    bool bInput = false;
    int32 Index = 0;
    if (!ParseMaterialPinId(PinId, bInput, Index) || !bInput)
    {
        return nullptr;
    }

    for (FExpressionInputIterator It{ Expression }; It; ++It)
    {
        if (It.Index == Index)
        {
            return It.Input;
        }
    }
    return nullptr;
}

static FString NormalizeMaterialPinLabel(FString Value)
{
    Value.TrimStartAndEndInline();
    return Value;
}

FExpressionInput* ResolveMaterialInputPin(UMaterialExpression* Expression, const FString& PinId)
{
    if (Expression == nullptr)
    {
        return nullptr;
    }

    if (FExpressionInput* Input = FindMaterialInput(Expression, PinId))
    {
        return Input;
    }

    if (PinId.IsNumeric())
    {
        const int32 TargetIndex = FCString::Atoi(*PinId);
        for (FExpressionInputIterator It{ Expression }; It; ++It)
        {
            if (It.Index == TargetIndex)
            {
                return It.Input;
            }
        }
    }

    const FString TargetName = NormalizeMaterialPinLabel(PinId);
    for (FExpressionInputIterator It{ Expression }; It; ++It)
    {
        if (Expression->GetInputName(It.Index).ToString().Equals(TargetName, ESearchCase::IgnoreCase))
        {
            return It.Input;
        }
    }
    return nullptr;
}

bool ResolveMaterialOutputPin(UMaterialExpression* Expression, const FString& PinId, bool& bOutInput, int32& OutIndex)
{
    bOutInput = false;
    OutIndex = INDEX_NONE;
    if (Expression == nullptr)
    {
        return false;
    }

    if (ParseMaterialPinId(PinId, bOutInput, OutIndex))
    {
        return !bOutInput && Expression->GetOutputs().IsValidIndex(OutIndex);
    }

    if (PinId.IsNumeric())
    {
        OutIndex = FCString::Atoi(*PinId);
        return Expression->GetOutputs().IsValidIndex(OutIndex);
    }

    const FString TargetName = NormalizeMaterialPinLabel(PinId);
    TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
    for (int32 Index = 0; Index < Outputs.Num(); ++Index)
    {
        const FString OutputName = Outputs[Index].OutputName.IsNone() ? FString::FromInt(Index) : Outputs[Index].OutputName.ToString();
        if (OutputName.Equals(TargetName, ESearchCase::IgnoreCase))
        {
            OutIndex = Index;
            return true;
        }
    }
    return false;
}

FString FindMaterialInputName(UMaterialExpression* Expression, const FString& PinId)
{
    if (Expression == nullptr)
    {
        return FString();
    }

    bool bInput = false;
    int32 Index = 0;
    if (!ParseMaterialPinId(PinId, bInput, Index) || !bInput)
    {
        return FString();
    }

    for (FExpressionInputIterator It{ Expression }; It; ++It)
    {
        if (It.Index == Index)
        {
            return Expression->GetInputName(Index).ToString();
        }
    }
    return FString();
}

FString FindMaterialOutputName(UMaterialExpression* Expression, const FString& PinId)
{
    bool bInput = false;
    int32 Index = 0;
    if (!ParseMaterialPinId(PinId, bInput, Index) || bInput)
    {
        return FString();
    }
    TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
    if (!Outputs.IsValidIndex(Index))
    {
        return FString();
    }
    return Outputs[Index].OutputName.IsNone() ? FString() : Outputs[Index].OutputName.ToString();
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
