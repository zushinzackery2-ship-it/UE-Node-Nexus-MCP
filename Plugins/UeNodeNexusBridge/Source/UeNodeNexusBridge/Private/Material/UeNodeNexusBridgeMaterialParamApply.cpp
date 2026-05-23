#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphNode.h"
#include "Materials/Material.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialFunctionInterface.h"
#include "UeNodeNexusBridgeMaterialCustomParamApply.h"
#include "UeNodeNexusBridgeMaterialPropertySchema.h"

namespace UeNodeNexusBridge
{
static bool JsonValueToPropertyText(const TSharedPtr<FJsonValue>& Value, FString& OutText)
{
    if (!Value.IsValid() || Value->Type == EJson::Null)
    {
        return false;
    }
    if (Value->Type == EJson::String)
    {
        OutText = Value->AsString();
        return true;
    }
    if (Value->Type == EJson::Boolean)
    {
        OutText = Value->AsBool() ? TEXT("True") : TEXT("False");
        return true;
    }
    if (Value->Type == EJson::Number)
    {
        OutText = FString::SanitizeFloat(Value->AsNumber());
        return true;
    }
    return false;
}

static bool ApplyMaterialFunctionCallParam(UMaterialExpression* Expression, const FString& Name, const FString& Value, bool bDryRun, FString& OutOldValue)
{
    UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
    if (FunctionCall == nullptr || !Name.Equals(TEXT("MaterialFunction"), ESearchCase::IgnoreCase))
    {
        return false;
    }

    OutOldValue = FunctionCall->MaterialFunction ? FunctionCall->MaterialFunction->GetPathName() : TEXT("None");
    UMaterialFunctionInterface* Function = nullptr;
    if (!Value.Equals(TEXT("None"), ESearchCase::IgnoreCase) && !Value.IsEmpty())
    {
        Function = LoadObject<UMaterialFunctionInterface>(nullptr, *Value);
        if (Function == nullptr)
        {
            return false;
        }
    }

    if (!bDryRun)
    {
        FunctionCall->Modify();
        if (Function == nullptr)
        {
            FunctionCall->MaterialFunction = nullptr;
            FunctionCall->FunctionInputs.Reset();
            FunctionCall->FunctionOutputs.Reset();
            FunctionCall->Outputs.Reset();
        }
        else
        {
            UEdGraphNode* SavedGraphNode = FunctionCall->GraphNode;
            FunctionCall->GraphNode = nullptr;
            const bool bSet = FunctionCall->SetMaterialFunction(Function);
            FunctionCall->GraphNode = SavedGraphNode;
            if (!bSet)
            {
                return false;
            }
        }
        FunctionCall->PostEditChange();
    }
    return true;
}

static void RefreshSetMaterialAttributesInputs(UMaterial* Material, UMaterialExpression* Expression, const FString& Name)
{
    UMaterialExpressionSetMaterialAttributes* SetAttributes = Cast<UMaterialExpressionSetMaterialAttributes>(Expression);
    if (Material == nullptr || SetAttributes == nullptr || !Name.Equals(TEXT("AttributeSetTypes"), ESearchCase::IgnoreCase))
    {
        return;
    }

    SetAttributes->Inputs.SetNum(SetAttributes->AttributeSetTypes.Num() + 1);
    if (SetAttributes->Inputs.Num() > 0)
    {
        SetAttributes->Inputs[0].InputName = FName(*NSLOCTEXT("SetMaterialAttributes", "InputName", "MaterialAttributes").ToString());
    }
    for (int32 Index = 0; Index < SetAttributes->AttributeSetTypes.Num(); ++Index)
    {
        SetAttributes->Inputs[Index + 1].InputName = FName(*FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(SetAttributes->AttributeSetTypes[Index], Material).ToString());
    }
}

bool ApplyMaterialExpressionParamValue(UMaterial* Material, UMaterialExpression* Expression, const FString& Name, const FString& Value, bool bDryRun, TSharedPtr<FJsonObject> Diff)
{
    if (Expression == nullptr)
    {
        return false;
    }

    FString OldValue;
    if (ApplyMaterialFunctionCallParam(Expression, Name, Value, bDryRun, OldValue))
    {
        AddMaterialParamChange(Diff, MaterialExpressionNodeId(Expression), Name, OldValue, Value);
        return true;
    }

    if (ImportMaterialExpressionPropertyText(Expression, Name, Value, bDryRun, OldValue))
    {
        AddMaterialParamChange(Diff, MaterialExpressionNodeId(Expression), Name, OldValue, Value);
        if (!bDryRun)
        {
            RefreshSetMaterialAttributesInputs(Material, Expression, Name);
        }
        return true;
    }
    if (Material != nullptr && TrySetMaterialSyntheticParam(Material, Expression, Name, Value, !bDryRun, OldValue))
    {
        AddMaterialParamChange(Diff, MaterialExpressionNodeId(Expression), Name, OldValue, Value);
        return true;
    }
    return false;
}

bool ApplyMaterialExpressionParamJsonValue(UMaterial* Material, UMaterialExpression* Expression, const FString& Name, const TSharedPtr<FJsonValue>& Value, bool bDryRun, TSharedPtr<FJsonObject> Diff, FString& OutFailureReason)
{
    if (Expression == nullptr)
    {
        OutFailureReason = TEXT("Expression is null");
        return false;
    }

    if (Value.IsValid() && (Value->Type == EJson::Array || Value->Type == EJson::Object))
    {
        if (ApplyMaterialCustomJsonParam(Material, Expression, Name, Value, bDryRun, Diff, OutFailureReason))
        {
            return true;
        }

        FString StructuredTextValue;
        FString StructuredError;
        if (MaterialExpressionJsonValueToPropertyText(Expression, Name, Value, StructuredTextValue, StructuredError))
        {
            if (ApplyMaterialExpressionParamValue(Material, Expression, Name, StructuredTextValue, bDryRun, Diff))
            {
                return true;
            }
            OutFailureReason = FString::Printf(TEXT("Could not import parameter %s from structured JSON value"), *Name);
            return false;
        }
        if (!StructuredError.IsEmpty())
        {
            OutFailureReason = FString::Printf(TEXT("Parameter %s structured value rejected: %s"), *Name, *StructuredError);
            return false;
        }
    }

    FString TextValue;
    if (!JsonValueToPropertyText(Value, TextValue))
    {
        OutFailureReason = FString::Printf(TEXT("Parameter %s expects a scalar value or a supported structured Custom node value"), *Name);
        return false;
    }
    if (ApplyMaterialExpressionParamValue(Material, Expression, Name, TextValue, bDryRun, Diff))
    {
        return true;
    }

    OutFailureReason = FString::Printf(TEXT("Could not import parameter %s"), *Name);
    return false;
}
}
