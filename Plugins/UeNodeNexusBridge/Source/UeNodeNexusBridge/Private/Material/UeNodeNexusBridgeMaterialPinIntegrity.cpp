#include "UeNodeNexusBridgeMaterialPatchHelpers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
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

    for (EMaterialProperty Property : MaterialOutputProperties())
    {
        FExpressionInput* Input = Material->GetExpressionInputForProperty(Property);
        if (Input == nullptr || Input->Expression == nullptr)
        {
            continue;
        }
        if (!Expressions.Contains(Input->Expression))
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("node_id"), MaterialOutputNodeId());
            Item->SetStringField(TEXT("pin_id"), MaterialOutputPropertyName(Property));
            Item->SetStringField(TEXT("source_node_id"), MaterialExpressionNodeId(Input->Expression));
            Item->SetStringField(TEXT("reason"), TEXT("source_node_missing"));
            Missing.Add(MakeShared<FJsonValueObject>(Item));
        }
        else if (!Input->Expression->GetOutputs().IsValidIndex(Input->OutputIndex))
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("node_id"), MaterialOutputNodeId());
            Item->SetStringField(TEXT("pin_id"), MaterialOutputPropertyName(Property));
            Item->SetStringField(TEXT("source_node_id"), MaterialExpressionNodeId(Input->Expression));
            Item->SetNumberField(TEXT("output_index"), Input->OutputIndex);
            Item->SetNumberField(TEXT("source_output_count"), Input->Expression->GetOutputs().Num());
            Item->SetStringField(TEXT("reason"), TEXT("source_output_missing"));
            Broken.Add(MakeShared<FJsonValueObject>(Item));
        }
        else if (Property == MP_MaterialAttributes && !Material->bUseMaterialAttributes)
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("node_id"), MaterialOutputNodeId());
            Item->SetStringField(TEXT("pin_id"), MaterialOutputPropertyName(Property));
            Item->SetStringField(TEXT("source_node_id"), MaterialExpressionNodeId(Input->Expression));
            Item->SetStringField(TEXT("reason"), TEXT("bUseMaterialAttributes_false"));
            Broken.Add(MakeShared<FJsonValueObject>(Item));
        }
        else if (Property == MP_MaterialAttributes && !Input->Expression->IsResultMaterialAttributes(Input->OutputIndex))
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("node_id"), MaterialOutputNodeId());
            Item->SetStringField(TEXT("pin_id"), MaterialOutputPropertyName(Property));
            Item->SetStringField(TEXT("source_node_id"), MaterialExpressionNodeId(Input->Expression));
            Item->SetNumberField(TEXT("output_index"), Input->OutputIndex);
            Item->SetStringField(TEXT("reason"), TEXT("source_not_material_attributes"));
            Broken.Add(MakeShared<FJsonValueObject>(Item));
        }
    }
    return MakePinIntegrity(Missing.Num() == 0 && Broken.Num() == 0, Broken, Missing);
}

TSharedPtr<FJsonObject> BuildMaterialFunctionPinIntegrity(UMaterialFunction* Function)
{
    TArray<UMaterialExpression*> Expressions;
    if (Function != nullptr)
    {
        for (TObjectPtr<UMaterialExpression> ExpressionPtr : Function->GetExpressions())
        {
            if (ExpressionPtr.Get() != nullptr)
            {
                Expressions.Add(ExpressionPtr.Get());
            }
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
            if (Input == nullptr || Input->Expression == nullptr)
            {
                continue;
            }

            if (!Expressions.Contains(Input->Expression))
            {
                TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(Expression));
                Item->SetStringField(TEXT("pin_id"), FString::Printf(TEXT("%s:in:%d"), *MaterialExpressionNodeId(Expression), Index));
                Item->SetStringField(TEXT("source_node_id"), MaterialExpressionNodeId(Input->Expression));
                Item->SetStringField(TEXT("reason"), TEXT("source_node_missing"));
                Missing.Add(MakeShared<FJsonValueObject>(Item));
            }
            else if (!Input->Expression->GetOutputs().IsValidIndex(Input->OutputIndex))
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
