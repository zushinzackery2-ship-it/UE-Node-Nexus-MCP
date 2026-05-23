#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"

#include "MaterialExpressionIO.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"

namespace UeNodeNexusBridge
{
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

static bool IsSingleOutputAlias(const FString& TargetName)
{
    return TargetName.Equals(TEXT("Out"), ESearchCase::IgnoreCase)
        || TargetName.Equals(TEXT("Output"), ESearchCase::IgnoreCase)
        || TargetName.Equals(TEXT("Result"), ESearchCase::IgnoreCase)
        || TargetName.Equals(TEXT("Value"), ESearchCase::IgnoreCase)
        || TargetName.Equals(TEXT("Color"), ESearchCase::IgnoreCase)
        || TargetName.Equals(TEXT("RGB"), ESearchCase::IgnoreCase)
        || TargetName.Equals(TEXT("RGBA"), ESearchCase::IgnoreCase);
}

static bool ResolveVectorOutputAlias(const FString& TargetName, int32 OutputCount, int32& OutIndex)
{
    if (OutputCount <= 1)
    {
        return false;
    }

    if (TargetName.Equals(TEXT("RGB"), ESearchCase::IgnoreCase)
        || TargetName.Equals(TEXT("RGBA"), ESearchCase::IgnoreCase)
        || TargetName.Equals(TEXT("Color"), ESearchCase::IgnoreCase)
        || TargetName.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
    {
        OutIndex = 0;
        return true;
    }
    if (OutputCount >= 5 && TargetName.Len() == 1)
    {
        const TCHAR Channel = FChar::ToUpper(TargetName[0]);
        if (Channel == TEXT('R') || Channel == TEXT('X'))
        {
            OutIndex = 1;
            return true;
        }
        if (Channel == TEXT('G') || Channel == TEXT('Y'))
        {
            OutIndex = 2;
            return true;
        }
        if (Channel == TEXT('B') || Channel == TEXT('Z'))
        {
            OutIndex = 3;
            return true;
        }
        if (Channel == TEXT('A') || Channel == TEXT('W'))
        {
            OutIndex = 4;
            return true;
        }
    }
    return false;
}

static bool MaterialPinLabelMatches(const FString& Actual, const FString& Target)
{
    if (Actual.Equals(Target, ESearchCase::IgnoreCase))
    {
        return true;
    }

    int32 TypeSuffixIndex = INDEX_NONE;
    if (Actual.FindChar(TEXT('('), TypeSuffixIndex))
    {
        FString WithoutType = Actual.Left(TypeSuffixIndex);
        WithoutType.TrimEndInline();
        return WithoutType.Equals(Target, ESearchCase::IgnoreCase);
    }
    return false;
}

static FExpressionInput* ResolveMaterialFunctionCallInputPin(UMaterialExpressionMaterialFunctionCall* FunctionCall, const FString& TargetName)
{
    if (FunctionCall == nullptr)
    {
        return nullptr;
    }

    for (int32 Index = 0; Index < FunctionCall->FunctionInputs.Num(); ++Index)
    {
        FFunctionExpressionInput& FunctionInput = FunctionCall->FunctionInputs[Index];
        if (MaterialPinLabelMatches(FunctionInput.Input.InputName.ToString(), TargetName))
        {
            return &FunctionInput.Input;
        }
        if (MaterialPinLabelMatches(FunctionCall->GetInputName(Index).ToString(), TargetName))
        {
            return &FunctionInput.Input;
        }
    }
    return nullptr;
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
    if (Cast<UMaterialExpressionFunctionOutput>(Expression) != nullptr && TargetName.Equals(TEXT("A"), ESearchCase::IgnoreCase))
    {
        return Expression->GetInput(0);
    }
    if (FExpressionInput* FunctionCallInput = ResolveMaterialFunctionCallInputPin(Cast<UMaterialExpressionMaterialFunctionCall>(Expression), TargetName))
    {
        return FunctionCallInput;
    }

    for (FExpressionInputIterator It{ Expression }; It; ++It)
    {
        if (MaterialPinLabelMatches(Expression->GetInputName(It.Index).ToString(), TargetName))
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
    if (ResolveVectorOutputAlias(TargetName, Outputs.Num(), OutIndex))
    {
        return Outputs.IsValidIndex(OutIndex);
    }
    if (Outputs.Num() == 1 && IsSingleOutputAlias(TargetName))
    {
        OutIndex = 0;
        return true;
    }
    return false;
}

FString DescribeMaterialOutputPins(UMaterialExpression* Expression)
{
    if (Expression == nullptr)
    {
        return TEXT("none");
    }

    TArray<FString> Parts;
    TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
    for (int32 Index = 0; Index < Outputs.Num(); ++Index)
    {
        const FString OutputName = Outputs[Index].OutputName.IsNone() ? FString::FromInt(Index) : Outputs[Index].OutputName.ToString();
        Parts.Add(FString::Printf(TEXT("%d:%s"), Index, *OutputName));
    }
    return Parts.Num() == 0 ? TEXT("none") : FString::Join(Parts, TEXT(","));
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
}
