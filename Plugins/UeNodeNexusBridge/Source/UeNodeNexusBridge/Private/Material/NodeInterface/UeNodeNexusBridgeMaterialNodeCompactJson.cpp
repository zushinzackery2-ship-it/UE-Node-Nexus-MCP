#include "UeNodeNexusBridgeMaterialNodeInterfaceShared.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonValueArray> MakeStringRow(const TArray<FString>& Cells)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    for (const FString& Cell : Cells)
    {
        Row.Add(MakeShared<FJsonValueString>(Cell));
    }
    return MakeShared<FJsonValueArray>(Row);
}

static TSharedPtr<FJsonValueArray> MakeMaterialNullLinkRow(const FString& PinName)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(PinName));
    Row.Add(MakeShared<FJsonValueNull>());
    return MakeShared<FJsonValueArray>(Row);
}

static TSharedPtr<FJsonValueArray> MakeMaterialLinkRow(const FString& PinName, const FString& Node, const FString& Pin)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(PinName));
    Row.Add(MakeShared<FJsonValueString>(Node));
    Row.Add(MakeShared<FJsonValueString>(Pin));
    return MakeShared<FJsonValueArray>(Row);
}

TArray<TSharedPtr<FJsonValue>> BuildCompactParamRows(const TArray<TSharedPtr<FJsonValue>>& Params, const FString& ValueField)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const TSharedPtr<FJsonValue>& Value : Params)
    {
        const TSharedPtr<FJsonObject> Param = Value.IsValid() ? Value->AsObject() : nullptr;
        if (!Param.IsValid())
        {
            continue;
        }

        FString Name;
        FString ParamValue;
        Param->TryGetStringField(TEXT("name"), Name);
        Param->TryGetStringField(ValueField, ParamValue);
        Rows.Add(MakeStringRow({ Name, ParamValue }));
    }
    return Rows;
}

TArray<TSharedPtr<FJsonValue>> BuildMaterialCompactInputRows(UMaterial* Material, UMaterialExpression* Expression)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (FExpressionInputIterator It{ Expression }; It; ++It)
    {
        FExpressionInput* Input = It.Input;
        const FString InputName = Expression->GetInputName(It.Index).ToString();
        Rows.Add((Input != nullptr && Input->Expression != nullptr)
            ? MakeMaterialLinkRow(InputName, MaterialNodeAlias(Material, Input->Expression), MaterialOutputName(Input->Expression, Input->OutputIndex))
            : MakeMaterialNullLinkRow(InputName));
    }
    return Rows;
}

TArray<TSharedPtr<FJsonValue>> BuildMaterialCompactOutputRows(UMaterial* Material, UMaterialExpression* Expression)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
    for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex)
    {
        const FString OutputName = MaterialOutputName(Expression, OutputIndex);
        const int32 BeforeCount = Rows.Num();
        for (TObjectPtr<UMaterialExpression> OtherPtr : Material->GetExpressions())
        {
            UMaterialExpression* Other = OtherPtr.Get();
            if (Other == nullptr)
            {
                continue;
            }
            for (FExpressionInputIterator It{ Other }; It; ++It)
            {
                if (It.Input != nullptr && It.Input->Expression == Expression && It.Input->OutputIndex == OutputIndex)
                {
                    Rows.Add(MakeMaterialLinkRow(OutputName, MaterialNodeAlias(Material, Other), Other->GetInputName(It.Index).ToString()));
                }
            }
        }
        for (EMaterialProperty Property : MaterialOutputProperties())
        {
            FExpressionInput* Input = Material->GetExpressionInputForProperty(Property);
            if (Input != nullptr && Input->Expression == Expression && Input->OutputIndex == OutputIndex)
            {
                Rows.Add(MakeMaterialLinkRow(OutputName, TEXT("MaterialOutput"), MaterialOutputPropertyName(Property)));
            }
        }
        if (Rows.Num() == BeforeCount)
        {
            Rows.Add(MakeMaterialNullLinkRow(OutputName));
        }
    }
    return Rows;
}

TArray<TSharedPtr<FJsonValue>> BuildMaterialFunctionCompactInputRows(UMaterialFunction* Function, UMaterialExpression* Expression)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (FExpressionInputIterator It{ Expression }; It; ++It)
    {
        FExpressionInput* Input = It.Input;
        const FString InputName = Expression->GetInputName(It.Index).ToString();
        Rows.Add((Input != nullptr && Input->Expression != nullptr)
            ? MakeMaterialLinkRow(InputName, MaterialNodeAlias(Function, Input->Expression), MaterialOutputName(Input->Expression, Input->OutputIndex))
            : MakeMaterialNullLinkRow(InputName));
    }
    return Rows;
}

TArray<TSharedPtr<FJsonValue>> BuildMaterialFunctionCompactOutputRows(UMaterialFunction* Function, UMaterialExpression* Expression)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
    for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex)
    {
        const FString OutputName = MaterialOutputName(Expression, OutputIndex);
        const int32 BeforeCount = Rows.Num();
        for (TObjectPtr<UMaterialExpression> OtherPtr : Function->GetExpressions())
        {
            UMaterialExpression* Other = OtherPtr.Get();
            if (Other == nullptr)
            {
                continue;
            }
            for (FExpressionInputIterator It{ Other }; It; ++It)
            {
                if (It.Input != nullptr && It.Input->Expression == Expression && It.Input->OutputIndex == OutputIndex)
                {
                    Rows.Add(MakeMaterialLinkRow(OutputName, MaterialNodeAlias(Function, Other), Other->GetInputName(It.Index).ToString()));
                }
            }
        }
        if (Rows.Num() == BeforeCount)
        {
            Rows.Add(MakeMaterialNullLinkRow(OutputName));
        }
    }
    return Rows;
}
}
