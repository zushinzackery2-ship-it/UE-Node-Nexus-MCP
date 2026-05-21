#include "UeNodeNexusBridgeMaterialCustomParamOps.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphNode.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
FString MaterialCustomJsonValueToCompactText(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid() || Value->Type == EJson::Null)
    {
        return TEXT("<null>");
    }
    if (Value->Type == EJson::String)
    {
        return Value->AsString();
    }
    if (Value->Type == EJson::Boolean)
    {
        return Value->AsBool() ? TEXT("True") : TEXT("False");
    }
    if (Value->Type == EJson::Number)
    {
        return FString::SanitizeFloat(Value->AsNumber());
    }
    if (Value->Type == EJson::Array)
    {
        return FString::Printf(TEXT("<array:%d>"), Value->AsArray().Num());
    }
    if (Value->Type == EJson::Object)
    {
        return TEXT("<object>");
    }
    return TEXT("<value>");
}

void RefreshMaterialCustomExpressionPins(UMaterialExpressionCustom* Custom)
{
    if (Custom == nullptr)
    {
        return;
    }

    for (FCustomInput& Input : Custom->Inputs)
    {
        FString InputName = Input.InputName.ToString();
        if (InputName.ReplaceInline(TEXT(" "), TEXT("")) > 0)
        {
            Input.InputName = FName(*InputName);
        }
    }
    Custom->Outputs.Reset(Custom->AdditionalOutputs.Num() + 1);
    if (Custom->AdditionalOutputs.Num() == 0)
    {
        Custom->bShowOutputNameOnPin = false;
        Custom->Outputs.Add(FExpressionOutput(TEXT("")));
    }
    else
    {
        Custom->bShowOutputNameOnPin = true;
        Custom->Outputs.Add(FExpressionOutput(TEXT("return")));
        for (const FCustomOutput& CustomOutput : Custom->AdditionalOutputs)
        {
            if (!CustomOutput.OutputName.IsNone())
            {
                Custom->Outputs.Add(FExpressionOutput(CustomOutput.OutputName));
            }
        }
    }
    if (Custom->GraphNode != nullptr)
    {
        Custom->GraphNode->ReconstructNode();
    }
    Custom->PostEditChange();
}

bool ReadMaterialCustomOutputType(const TSharedPtr<FJsonObject>& Object, ECustomMaterialOutputType& OutType)
{
    FString TypeText;
    if (!Object->TryGetStringField(TEXT("output_type"), TypeText))
    {
        Object->TryGetStringField(TEXT("OutputType"), TypeText);
    }
    if (TypeText.IsEmpty())
    {
        OutType = CMOT_Float1;
        return true;
    }

    TypeText.RemoveFromStart(TEXT("CMOT_"));
    if (TypeText.Equals(TEXT("Float1"), ESearchCase::IgnoreCase))
    {
        OutType = CMOT_Float1;
        return true;
    }
    if (TypeText.Equals(TEXT("Float2"), ESearchCase::IgnoreCase))
    {
        OutType = CMOT_Float2;
        return true;
    }
    if (TypeText.Equals(TEXT("Float3"), ESearchCase::IgnoreCase))
    {
        OutType = CMOT_Float3;
        return true;
    }
    if (TypeText.Equals(TEXT("Float4"), ESearchCase::IgnoreCase))
    {
        OutType = CMOT_Float4;
        return true;
    }
    if (TypeText.Equals(TEXT("MaterialAttributes"), ESearchCase::IgnoreCase))
    {
        OutType = CMOT_MaterialAttributes;
        return true;
    }
    return false;
}

bool ApplyMaterialCustomInputsJson(UMaterialExpressionCustom* Custom, const TSharedPtr<FJsonValue>& Value, bool bDryRun, FString& OutOldValue, FString& OutFailureReason)
{
    if (Custom == nullptr)
    {
        return false;
    }
    if (!Value.IsValid() || Value->Type != EJson::Array)
    {
        OutFailureReason = TEXT("Custom Inputs expects an array of strings or objects with name/input_name");
        return false;
    }

    if (FArrayProperty* InputsProperty = FindFProperty<FArrayProperty>(UMaterialExpressionCustom::StaticClass(), GET_MEMBER_NAME_CHECKED(UMaterialExpressionCustom, Inputs)))
    {
        InputsProperty->ExportTextItem_InContainer(OutOldValue, Custom, nullptr, Custom, PPF_None);
    }

    TArray<FName> InputNames;
    int32 Index = 0;
    for (const TSharedPtr<FJsonValue>& ItemValue : Value->AsArray())
    {
        FString InputName;
        if (ItemValue->Type == EJson::String)
        {
            InputName = ItemValue->AsString();
        }
        else if (ItemValue->Type == EJson::Object)
        {
            TSharedPtr<FJsonObject> Item = ItemValue->AsObject();
            if (!Item->TryGetStringField(TEXT("name"), InputName))
            {
                Item->TryGetStringField(TEXT("input_name"), InputName);
            }
            if (InputName.IsEmpty())
            {
                Item->TryGetStringField(TEXT("InputName"), InputName);
            }
        }

        InputName.TrimStartAndEndInline();
        InputName.ReplaceInline(TEXT(" "), TEXT(""));
        if (InputName.IsEmpty())
        {
            OutFailureReason = FString::Printf(TEXT("Custom Inputs[%d] is missing a non-empty input name"), Index);
            return false;
        }
        InputNames.Add(FName(*InputName));
        ++Index;
    }

    if (!bDryRun)
    {
        Custom->Modify();
        TArray<FCustomInput> PreviousInputs = Custom->Inputs;
        Custom->Inputs.SetNum(InputNames.Num());
        for (int32 InputIndex = 0; InputIndex < InputNames.Num(); ++InputIndex)
        {
            FExpressionInput PreservedInput;
            for (const FCustomInput& PreviousInput : PreviousInputs)
            {
                if (PreviousInput.InputName == InputNames[InputIndex])
                {
                    PreservedInput = PreviousInput.Input;
                    break;
                }
            }
            Custom->Inputs[InputIndex].InputName = InputNames[InputIndex];
            Custom->Inputs[InputIndex].Input = PreservedInput;
        }
        RefreshMaterialCustomExpressionPins(Custom);
    }
    return true;
}
}
