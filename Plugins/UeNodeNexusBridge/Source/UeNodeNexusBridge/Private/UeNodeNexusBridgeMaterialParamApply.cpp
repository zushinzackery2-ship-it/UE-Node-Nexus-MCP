#include "UeNodeNexusBridgeMaterialPatchHelpers.h"

#include "Dom/JsonObject.h"
#include "Materials/Material.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialFunctionInterface.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
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
    if (SetAttributes == nullptr || !Name.Equals(TEXT("AttributeSetTypes"), ESearchCase::IgnoreCase))
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

    FProperty* Property = Expression->GetClass()->FindPropertyByName(FName(*Name));
    if (Property != nullptr && Property->HasAnyPropertyFlags(CPF_Edit))
    {
        Property->ExportTextItem_InContainer(OldValue, Expression, nullptr, Expression, PPF_None);
        AddMaterialParamChange(Diff, MaterialExpressionNodeId(Expression), Name, OldValue, Value);
        const bool bImported = bDryRun || Property->ImportText_InContainer(*Value, Expression, Expression, PPF_None) != nullptr;
        if (bImported && !bDryRun)
        {
            RefreshSetMaterialAttributesInputs(Material, Expression, Name);
        }
        return bImported;
    }
    if (TrySetMaterialSyntheticParam(Material, Expression, Name, Value, !bDryRun, OldValue))
    {
        AddMaterialParamChange(Diff, MaterialExpressionNodeId(Expression), Name, OldValue, Value);
        return true;
    }
    return false;
}
}
