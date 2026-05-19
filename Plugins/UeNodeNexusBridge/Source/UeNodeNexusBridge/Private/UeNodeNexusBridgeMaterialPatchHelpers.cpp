#include "UeNodeNexusBridgeMaterialPatchHelpers.h"

#include "Dom/JsonValue.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "UeNodeNexusBridgeJson.h"

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

TSharedPtr<FJsonObject> BuildMaterialPinIntegrity(UMaterial* Material)
{
    TArray<UMaterialExpression*> Expressions;
    for (TObjectPtr<UMaterialExpression> ExpressionPtr : Material->GetExpressions())
    {
        if (ExpressionPtr.Get() != nullptr)
        {
            Expressions.Add(ExpressionPtr.Get());
        }
    }

    TArray<TSharedPtr<FJsonValue>> Missing;
    TArray<TSharedPtr<FJsonValue>> Broken;
    for (UMaterialExpression* Expression : Expressions)
    {
        for (FExpressionInputIterator It{ Expression }; It; ++It)
        {
            FExpressionInput* Input = It.Input;
            const int32 Index = It.Index;
            if (Input->Expression != nullptr && !Expressions.Contains(Input->Expression))
            {
                TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(Expression));
                Item->SetStringField(TEXT("pin_id"), FString::Printf(TEXT("%s:in:%d"), *MaterialExpressionNodeId(Expression), Index));
                Missing.Add(MakeShared<FJsonValueObject>(Item));
            }
            else if (Input->Expression != nullptr && !Input->Expression->GetOutputs().IsValidIndex(Input->OutputIndex))
            {
                TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(Expression));
                Item->SetStringField(TEXT("pin_id"), FString::Printf(TEXT("%s:in:%d"), *MaterialExpressionNodeId(Expression), Index));
                Item->SetStringField(TEXT("source_node_id"), MaterialExpressionNodeId(Input->Expression));
                Item->SetNumberField(TEXT("output_index"), Input->OutputIndex);
                Item->SetNumberField(TEXT("source_output_count"), Input->Expression->GetOutputs().Num());
                Broken.Add(MakeShared<FJsonValueObject>(Item));
            }
        }
    }
    return MakePinIntegrity(Missing.Num() == 0 && Broken.Num() == 0, Broken, Missing);
}
}
