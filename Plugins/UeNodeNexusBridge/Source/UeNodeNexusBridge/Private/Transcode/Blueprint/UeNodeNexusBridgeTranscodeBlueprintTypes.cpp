#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"

#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace UeNodeNexusBridge::Transcode
{
static FString ContainerText(EPinContainerType Container)
{
    switch (Container)
    {
    case EPinContainerType::Array: return TEXT("array");
    case EPinContainerType::Set: return TEXT("set");
    case EPinContainerType::Map: return TEXT("map");
    default: return TEXT("none");
    }
}

static FString ObjectPathOrEmpty(const UObject* Object)
{
    return Object ? Object->GetPathName() : FString();
}

TSharedPtr<FJsonObject> PinTypeJson(const FEdGraphPinType& PinType)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("category"), PinType.PinCategory.ToString());
    Json->SetStringField(TEXT("subcategory"), PinType.PinSubCategory.ToString());
    Json->SetStringField(TEXT("subobject"), ObjectPathOrEmpty(PinType.PinSubCategoryObject.Get()));
    Json->SetStringField(TEXT("container"), ContainerText(PinType.ContainerType));
    Json->SetBoolField(TEXT("is_ref"), PinType.bIsReference);
    if (PinType.ContainerType == EPinContainerType::Map)
    {
        Json->SetStringField(TEXT("value_category"), PinType.PinValueType.TerminalCategory.ToString());
        Json->SetStringField(TEXT("value_subcategory"), PinType.PinValueType.TerminalSubCategory.ToString());
        Json->SetStringField(TEXT("value_subobject"), ObjectPathOrEmpty(PinType.PinValueType.TerminalSubCategoryObject.Get()));
    }
    return Json;
}

static UObject* ResolveTypeObject(const FString& Path)
{
    if (Path.IsEmpty())
    {
        return nullptr;
    }
    if (UObject* Found = FindObject<UObject>(nullptr, *Path))
    {
        return Found;
    }
    if (UObject* Loaded = LoadObject<UObject>(nullptr, *Path))
    {
        return Loaded;
    }
    // Blueprint class references may be written as the asset path; append _C.
    if (!Path.EndsWith(TEXT("_C")))
    {
        return LoadObject<UObject>(nullptr, *(Path + TEXT("_C")));
    }
    return nullptr;
}

bool PinTypeFromJson(const TSharedPtr<FJsonObject>& Json, FEdGraphPinType& OutType, FString& OutError)
{
    if (!Json.IsValid())
    {
        OutError = TEXT("type must be an object");
        return false;
    }
    FString Category = Json->GetStringField(TEXT("category"));
    FString Subcategory;
    FString Subobject;
    Json->TryGetStringField(TEXT("subcategory"), Subcategory);
    Json->TryGetStringField(TEXT("subobject"), Subobject);
    if (Category == TEXT("float") || Category == TEXT("double"))
    {
        Subcategory = Category;
        Category = UEdGraphSchema_K2::PC_Real.ToString();
    }
    if (Category == TEXT("real") && Subcategory.IsEmpty())
    {
        Subcategory = UEdGraphSchema_K2::PC_Float.ToString();
    }
    OutType = FEdGraphPinType();
    OutType.PinCategory = FName(*Category);
    OutType.PinSubCategory = FName(*Subcategory);
    if (!Subobject.IsEmpty())
    {
        UObject* Object = ResolveTypeObject(Subobject);
        if (Object == nullptr)
        {
            OutError = FString::Printf(TEXT("type object not found: %s"), *Subobject);
            return false;
        }
        OutType.PinSubCategoryObject = Object;
    }
    FString Container;
    Json->TryGetStringField(TEXT("container"), Container);
    if (Container == TEXT("array"))
    {
        OutType.ContainerType = EPinContainerType::Array;
    }
    else if (Container == TEXT("set"))
    {
        OutType.ContainerType = EPinContainerType::Set;
    }
    else if (Container == TEXT("map"))
    {
        OutType.ContainerType = EPinContainerType::Map;
        FString ValueCategory, ValueSubcategory, ValueSubobject;
        Json->TryGetStringField(TEXT("value_category"), ValueCategory);
        Json->TryGetStringField(TEXT("value_subcategory"), ValueSubcategory);
        Json->TryGetStringField(TEXT("value_subobject"), ValueSubobject);
        if (ValueCategory == TEXT("float") || ValueCategory == TEXT("double"))
        {
            ValueSubcategory = ValueCategory;
            ValueCategory = UEdGraphSchema_K2::PC_Real.ToString();
        }
        OutType.PinValueType.TerminalCategory = FName(*ValueCategory);
        OutType.PinValueType.TerminalSubCategory = FName(*ValueSubcategory);
        if (!ValueSubobject.IsEmpty())
        {
            OutType.PinValueType.TerminalSubCategoryObject = ResolveTypeObject(ValueSubobject);
        }
    }
    bool bIsRef = false;
    Json->TryGetBoolField(TEXT("is_ref"), bIsRef);
    OutType.bIsReference = bIsRef;
    return true;
}

TSharedPtr<FJsonObject> VariableJson(const FBPVariableDescription& Variable, UObject* Cdo)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("name"), Variable.VarName.ToString());
    Json->SetStringField(TEXT("guid"), Variable.VarGuid.ToString(EGuidFormats::DigitsWithHyphens));
    Json->SetObjectField(TEXT("type"), PinTypeJson(Variable.VarType));
    // The compiler consumes DefaultValue into the CDO; the CDO is the truth afterwards.
    FString Default = Variable.DefaultValue;
    if (Cdo != nullptr)
    {
        if (FProperty* Property = FindFProperty<FProperty>(Cdo->GetClass(), Variable.VarName))
        {
            Default = ExportPropertyValue(Cdo, Property);
        }
    }
    Json->SetStringField(TEXT("default"), Default);
    const bool bDefaultCategory = Variable.Category.IsEmpty() || Variable.Category.EqualToCaseIgnored(UEdGraphSchema_K2::VR_DefaultCategory) || Variable.Category.ToString() == TEXT("Default");
    Json->SetStringField(TEXT("category"), bDefaultCategory ? FString() : Variable.Category.ToString());
    Json->SetStringField(TEXT("rep_notify"), Variable.RepNotifyFunc.IsNone() ? FString() : Variable.RepNotifyFunc.ToString());

    TArray<TSharedPtr<FJsonValue>> Flags;
    const uint64 PropertyFlags = Variable.PropertyFlags;
    if ((PropertyFlags & CPF_Edit) && !(PropertyFlags & CPF_DisableEditOnInstance))
    {
        Flags.Add(MakeShared<FJsonValueString>(TEXT("InstanceEditable")));
    }
    if (PropertyFlags & CPF_BlueprintReadOnly)
    {
        Flags.Add(MakeShared<FJsonValueString>(TEXT("BlueprintReadOnly")));
    }
    if (PropertyFlags & CPF_ExposeOnSpawn)
    {
        Flags.Add(MakeShared<FJsonValueString>(TEXT("ExposeOnSpawn")));
    }
    if (PropertyFlags & CPF_Net)
    {
        Flags.Add(MakeShared<FJsonValueString>(TEXT("Replicated")));
    }
    if (PropertyFlags & CPF_Transient)
    {
        Flags.Add(MakeShared<FJsonValueString>(TEXT("Transient")));
    }
    if (PropertyFlags & CPF_SaveGame)
    {
        Flags.Add(MakeShared<FJsonValueString>(TEXT("SaveGame")));
    }
    if (PropertyFlags & CPF_Config)
    {
        Flags.Add(MakeShared<FJsonValueString>(TEXT("Config")));
    }
    if (PropertyFlags & CPF_Interp)
    {
        Flags.Add(MakeShared<FJsonValueString>(TEXT("ExposeToCinematics")));
    }
    TSharedPtr<FJsonObject> Metadata = MakeShared<FJsonObject>();
    FString Tooltip;
    for (const FBPVariableMetaDataEntry& Entry : Variable.MetaDataArray)
    {
        Metadata->SetStringField(Entry.DataKey.ToString(), Entry.DataValue);
        if (Entry.DataKey == FBlueprintMetadata::MD_Tooltip)
        {
            Tooltip = Entry.DataValue;
        }
        else if (Entry.DataKey == FBlueprintMetadata::MD_Private && Entry.DataValue.Equals(TEXT("true"), ESearchCase::IgnoreCase))
        {
            Flags.Add(MakeShared<FJsonValueString>(TEXT("Private")));
        }
        else if (Entry.DataKey == TEXT("MultiLine") && Entry.DataValue.Equals(TEXT("true"), ESearchCase::IgnoreCase))
        {
            Flags.Add(MakeShared<FJsonValueString>(TEXT("Multiline")));
        }
    }
    Json->SetStringField(TEXT("tooltip"), Tooltip);
    Json->SetArrayField(TEXT("flags"), Flags);
    Json->SetObjectField(TEXT("metadata"), Metadata);
    return Json;
}

uint64 VariableFlagsFromList(const TArray<FString>& Flags, bool& bOutPrivate, bool& bOutMultiline)
{
    uint64 Result = CPF_Edit | CPF_BlueprintVisible | CPF_DisableEditOnInstance;
    bOutPrivate = false;
    bOutMultiline = false;
    for (const FString& Flag : Flags)
    {
        if (Flag == TEXT("InstanceEditable"))
        {
            Result &= ~CPF_DisableEditOnInstance;
        }
        else if (Flag == TEXT("BlueprintReadOnly"))
        {
            Result |= CPF_BlueprintReadOnly;
        }
        else if (Flag == TEXT("ExposeOnSpawn"))
        {
            Result |= CPF_ExposeOnSpawn;
        }
        else if (Flag == TEXT("Replicated"))
        {
            Result |= CPF_Net;
        }
        else if (Flag == TEXT("Transient"))
        {
            Result |= CPF_Transient;
        }
        else if (Flag == TEXT("SaveGame"))
        {
            Result |= CPF_SaveGame;
        }
        else if (Flag == TEXT("Config"))
        {
            Result |= CPF_Config;
        }
        else if (Flag == TEXT("ExposeToCinematics"))
        {
            Result |= CPF_Interp;
        }
        else if (Flag == TEXT("Private"))
        {
            bOutPrivate = true;
        }
        else if (Flag == TEXT("Multiline"))
        {
            bOutMultiline = true;
        }
    }
    return Result;
}
}
