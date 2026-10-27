#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Material/Functions/NexusFunctionCallRebind.h"
#include "Materials/Material.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Material/CustomParams/UeNodeNexusBridgeMaterialCustomParamApply.h"
#include "Material/Properties/UeNodeNexusBridgeMaterialPropertySchema.h"

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

static UMaterialExpressionMaterialFunctionCall* CalledFunctionTarget(UMaterialExpression* Expression, const FString& Name)
{
    return Name.Equals(TEXT("MaterialFunction"), ESearchCase::IgnoreCase) ? Cast<UMaterialExpressionMaterialFunctionCall>(Expression) : nullptr;
}

// A call's MaterialFunction is rebound rather than imported: links that read the call
// follow their output names onto the new function (see RebindFunctionCall).
static bool ApplyCalledFunction(UMaterialExpressionMaterialFunctionCall* Call, const FString& Value, bool bDryRun, TSharedPtr<FJsonObject> Diff, FString& OutFailureReason)
{
    UMaterialFunctionInterface* Function = nullptr;
    if (!ResolveCalledFunction(Value, Function, OutFailureReason))
    {
        return false;
    }
    const FString OldValue = Call->MaterialFunction ? Call->MaterialFunction->GetPathName() : TEXT("None");
    if (!bDryRun)
    {
        FFunctionCallRebind Rebind;
        if (!RebindFunctionCall(Call, Function, Rebind, OutFailureReason))
        {
            return false;
        }
        Call->PostEditChange();
    }
    AddMaterialParamChange(Diff, MaterialExpressionNodeId(Call), TEXT("MaterialFunction"), OldValue, Value);
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

    if (UMaterialExpressionMaterialFunctionCall* Call = CalledFunctionTarget(Expression, Name))
    {
        FString FailureReason;
        return ApplyCalledFunction(Call, Value, bDryRun, Diff, FailureReason);
    }

    FString OldValue;
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

    if (UMaterialExpressionMaterialFunctionCall* Call = CalledFunctionTarget(Expression, Name))
    {
        FString Path;
        if (!Value.IsValid() || !Value->TryGetString(Path))
        {
            OutFailureReason = TEXT("MaterialFunction expects a material function object path or None");
            return false;
        }
        return ApplyCalledFunction(Call, Path, bDryRun, Diff, OutFailureReason);
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
