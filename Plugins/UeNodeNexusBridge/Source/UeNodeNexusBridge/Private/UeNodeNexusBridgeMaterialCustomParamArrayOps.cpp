#include "UeNodeNexusBridgeMaterialCustomParamOps.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
bool ApplyMaterialCustomAdditionalOutputsJson(UMaterialExpressionCustom* Custom, const TSharedPtr<FJsonValue>& Value, bool bDryRun, FString& OutOldValue, FString& OutFailureReason)
{
    if (Custom == nullptr)
    {
        return false;
    }
    if (!Value.IsValid() || Value->Type != EJson::Array)
    {
        OutFailureReason = TEXT("Custom AdditionalOutputs expects an array of strings or objects with output_name/output_type");
        return false;
    }

    if (FArrayProperty* OutputsProperty = FindFProperty<FArrayProperty>(UMaterialExpressionCustom::StaticClass(), GET_MEMBER_NAME_CHECKED(UMaterialExpressionCustom, AdditionalOutputs)))
    {
        OutputsProperty->ExportTextItem_InContainer(OutOldValue, Custom, nullptr, Custom, PPF_None);
    }

    TArray<FCustomOutput> NewOutputs;
    int32 Index = 0;
    for (const TSharedPtr<FJsonValue>& ItemValue : Value->AsArray())
    {
        FString OutputName;
        ECustomMaterialOutputType OutputType = CMOT_Float1;
        if (ItemValue->Type == EJson::String)
        {
            OutputName = ItemValue->AsString();
        }
        else if (ItemValue->Type == EJson::Object)
        {
            TSharedPtr<FJsonObject> Item = ItemValue->AsObject();
            if (!Item->TryGetStringField(TEXT("name"), OutputName))
            {
                Item->TryGetStringField(TEXT("output_name"), OutputName);
            }
            if (OutputName.IsEmpty())
            {
                Item->TryGetStringField(TEXT("OutputName"), OutputName);
            }
            if (!ReadMaterialCustomOutputType(Item, OutputType))
            {
                OutFailureReason = FString::Printf(TEXT("Custom AdditionalOutputs[%d] has unsupported output_type"), Index);
                return false;
            }
        }
        else
        {
            OutFailureReason = FString::Printf(TEXT("Custom AdditionalOutputs[%d] must be string or object"), Index);
            return false;
        }

        OutputName.TrimStartAndEndInline();
        OutputName.ReplaceInline(TEXT(" "), TEXT(""));
        if (OutputName.IsEmpty())
        {
            OutFailureReason = FString::Printf(TEXT("Custom AdditionalOutputs[%d] is missing a non-empty output name"), Index);
            return false;
        }

        FCustomOutput Output;
        Output.OutputName = FName(*OutputName);
        Output.OutputType = OutputType;
        NewOutputs.Add(Output);
        ++Index;
    }

    if (!bDryRun)
    {
        Custom->Modify();
        Custom->AdditionalOutputs = NewOutputs;
        RefreshMaterialCustomExpressionPins(Custom);
    }
    return true;
}

bool ApplyMaterialCustomAdditionalDefinesJson(UMaterialExpressionCustom* Custom, const TSharedPtr<FJsonValue>& Value, bool bDryRun, FString& OutOldValue, FString& OutFailureReason)
{
    if (Custom == nullptr)
    {
        return false;
    }
    if (!Value.IsValid() || Value->Type != EJson::Array)
    {
        OutFailureReason = TEXT("Custom AdditionalDefines expects an array of objects with define_name/define_value");
        return false;
    }

    if (FArrayProperty* DefinesProperty = FindFProperty<FArrayProperty>(UMaterialExpressionCustom::StaticClass(), GET_MEMBER_NAME_CHECKED(UMaterialExpressionCustom, AdditionalDefines)))
    {
        DefinesProperty->ExportTextItem_InContainer(OutOldValue, Custom, nullptr, Custom, PPF_None);
    }

    TArray<FCustomDefine> Defines;
    int32 Index = 0;
    for (const TSharedPtr<FJsonValue>& ItemValue : Value->AsArray())
    {
        if (ItemValue->Type != EJson::Object)
        {
            OutFailureReason = FString::Printf(TEXT("Custom AdditionalDefines[%d] must be an object"), Index);
            return false;
        }

        TSharedPtr<FJsonObject> Item = ItemValue->AsObject();
        FString DefineName;
        FString DefineValue;
        if (!Item->TryGetStringField(TEXT("define_name"), DefineName))
        {
            Item->TryGetStringField(TEXT("DefineName"), DefineName);
        }
        if (!Item->TryGetStringField(TEXT("define_value"), DefineValue))
        {
            Item->TryGetStringField(TEXT("DefineValue"), DefineValue);
        }
        DefineName.TrimStartAndEndInline();
        if (DefineName.IsEmpty())
        {
            OutFailureReason = FString::Printf(TEXT("Custom AdditionalDefines[%d] is missing define_name"), Index);
            return false;
        }

        FCustomDefine Define;
        Define.DefineName = DefineName;
        Define.DefineValue = DefineValue;
        Defines.Add(Define);
        ++Index;
    }

    if (!bDryRun)
    {
        Custom->Modify();
        Custom->AdditionalDefines = Defines;
        Custom->PostEditChange();
    }
    return true;
}

bool ApplyMaterialCustomIncludeFilePathsJson(UMaterialExpressionCustom* Custom, const TSharedPtr<FJsonValue>& Value, bool bDryRun, FString& OutOldValue, FString& OutFailureReason)
{
    if (Custom == nullptr)
    {
        return false;
    }
    if (!Value.IsValid() || Value->Type != EJson::Array)
    {
        OutFailureReason = TEXT("Custom IncludeFilePaths expects an array of strings");
        return false;
    }

    if (FArrayProperty* PathsProperty = FindFProperty<FArrayProperty>(UMaterialExpressionCustom::StaticClass(), GET_MEMBER_NAME_CHECKED(UMaterialExpressionCustom, IncludeFilePaths)))
    {
        PathsProperty->ExportTextItem_InContainer(OutOldValue, Custom, nullptr, Custom, PPF_None);
    }

    TArray<FString> Paths;
    int32 Index = 0;
    for (const TSharedPtr<FJsonValue>& ItemValue : Value->AsArray())
    {
        if (ItemValue->Type != EJson::String)
        {
            OutFailureReason = FString::Printf(TEXT("Custom IncludeFilePaths[%d] must be a string"), Index);
            return false;
        }
        Paths.Add(ItemValue->AsString());
        ++Index;
    }

    if (!bDryRun)
    {
        Custom->Modify();
        Custom->IncludeFilePaths = Paths;
        Custom->PostEditChange();
    }
    return true;
}
}
