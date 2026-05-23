#include "UeNodeNexusBridgeObjectHelpers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
UObject* ResolveObjectByPath(const FString& ObjectPath)
{
    if (ObjectPath.IsEmpty())
    {
        return nullptr;
    }

    if (UObject* Object = FindObject<UObject>(nullptr, *ObjectPath))
    {
        return Object;
    }
    return LoadObject<UObject>(nullptr, *ObjectPath);
}

TSharedPtr<FJsonObject> MakeVectorJson(const FVector& Value)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("x"), Value.X);
    Json->SetNumberField(TEXT("y"), Value.Y);
    Json->SetNumberField(TEXT("z"), Value.Z);
    return Json;
}

TSharedPtr<FJsonObject> MakeRotatorJson(const FRotator& Value)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("pitch"), Value.Pitch);
    Json->SetNumberField(TEXT("yaw"), Value.Yaw);
    Json->SetNumberField(TEXT("roll"), Value.Roll);
    return Json;
}

TSharedPtr<FJsonObject> MakeTransformJson(const FTransform& Value)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetObjectField(TEXT("location"), MakeVectorJson(Value.GetLocation()));
    Json->SetObjectField(TEXT("rotation"), MakeRotatorJson(Value.Rotator()));
    Json->SetObjectField(TEXT("scale"), MakeVectorJson(Value.GetScale3D()));
    return Json;
}

FString PropertyValueToText(UObject* Object, FProperty* Property)
{
    FString Text;
    if (Object != nullptr && Property != nullptr)
    {
        Property->ExportText_InContainer(0, Text, Object, nullptr, Object, PPF_None);
    }
    return Text;
}

static TSharedPtr<FJsonValue> StructPropertyToJson(UObject* Object, FStructProperty* Property)
{
    const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
    if (Property->Struct == TBaseStructure<FVector>::Get())
    {
        return MakeShared<FJsonValueObject>(MakeVectorJson(*static_cast<const FVector*>(ValuePtr)));
    }
    if (Property->Struct == TBaseStructure<FRotator>::Get())
    {
        return MakeShared<FJsonValueObject>(MakeRotatorJson(*static_cast<const FRotator*>(ValuePtr)));
    }
    if (Property->Struct == TBaseStructure<FTransform>::Get())
    {
        return MakeShared<FJsonValueObject>(MakeTransformJson(*static_cast<const FTransform*>(ValuePtr)));
    }
    if (Property->Struct == TBaseStructure<FLinearColor>::Get())
    {
        const FLinearColor& Color = *static_cast<const FLinearColor*>(ValuePtr);
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetNumberField(TEXT("r"), Color.R);
        Json->SetNumberField(TEXT("g"), Color.G);
        Json->SetNumberField(TEXT("b"), Color.B);
        Json->SetNumberField(TEXT("a"), Color.A);
        return MakeShared<FJsonValueObject>(Json);
    }
    return MakeShared<FJsonValueString>(PropertyValueToText(Object, Property));
}

TSharedPtr<FJsonValue> PropertyValueToJson(UObject* Object, FProperty* Property)
{
    if (Object == nullptr || Property == nullptr)
    {
        return MakeShared<FJsonValueNull>();
    }

    if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
    {
        return MakeShared<FJsonValueBoolean>(BoolProperty->GetPropertyValue_InContainer(Object));
    }
    if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
    {
        const void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Object);
        if (NumericProperty->IsInteger())
        {
            return MakeShared<FJsonValueNumber>(static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr)));
        }
        return MakeShared<FJsonValueNumber>(NumericProperty->GetFloatingPointPropertyValue(ValuePtr));
    }
    if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
    {
        return MakeShared<FJsonValueString>(StringProperty->GetPropertyValue_InContainer(Object));
    }
    if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
    {
        return MakeShared<FJsonValueString>(NameProperty->GetPropertyValue_InContainer(Object).ToString());
    }
    if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
    {
        return MakeShared<FJsonValueString>(TextProperty->GetPropertyValue_InContainer(Object).ToString());
    }
    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        UObject* Value = ObjectProperty->GetObjectPropertyValue_InContainer(Object);
        return MakeShared<FJsonValueString>(Value ? Value->GetPathName() : FString());
    }
    if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        return StructPropertyToJson(Object, StructProperty);
    }
    return MakeShared<FJsonValueString>(PropertyValueToText(Object, Property));
}

TSharedPtr<FJsonObject> PropertyToJson(UObject* Object, FProperty* Property, bool bFull)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("name"), Property ? Property->GetName() : FString());
    Json->SetStringField(TEXT("type"), Property ? Property->GetClass()->GetName() : FString());
    Json->SetField(TEXT("value"), PropertyValueToJson(Object, Property));
    if (bFull)
    {
        Json->SetStringField(TEXT("value_text"), PropertyValueToText(Object, Property));
        Json->SetBoolField(TEXT("editable"), Property && Property->HasAnyPropertyFlags(CPF_Edit));
        Json->SetBoolField(TEXT("blueprint_visible"), Property && Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
    }
    return Json;
}

bool ShouldExposeProperty(FProperty* Property, bool bIncludeNonEditable)
{
    if (Property == nullptr || Property->HasAnyPropertyFlags(CPF_Deprecated))
    {
        return false;
    }
    return bIncludeNonEditable || Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
}

bool ApplyPropertyText(UObject* Object, FProperty* Property, const FString& ValueText)
{
    if (Object == nullptr || Property == nullptr)
    {
        return false;
    }
    void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
    return Property->ImportText_Direct(*ValueText, ValuePtr, Object, PPF_None) != nullptr;
}

FString JsonValueToImportText(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid())
    {
        return FString();
    }
    FString Text;
    if (Value->TryGetString(Text))
    {
        return Text;
    }
    double Number = 0.0;
    if (Value->TryGetNumber(Number))
    {
        return FString::SanitizeFloat(Number, 0);
    }
    bool bBool = false;
    if (Value->TryGetBool(bBool))
    {
        return bBool ? TEXT("True") : TEXT("False");
    }
    return FString();
}

bool SaveObjectConfig(UObject* Object, FString& OutConfigFile)
{
    OutConfigFile.Reset();
    if (Object == nullptr || Object->GetClass() == nullptr || !Object->GetClass()->HasAnyClassFlags(CLASS_Config))
    {
        return false;
    }

    Object->SaveConfig();
    if (Object->GetClass()->HasAnyClassFlags(CLASS_DefaultConfig))
    {
        OutConfigFile = Object->GetDefaultConfigFilename();
        return Object->TryUpdateDefaultConfigFile(OutConfigFile);
    }

    OutConfigFile = Object->GetClass()->ClassConfigName.ToString();
    if (GConfig != nullptr)
    {
        GConfig->Flush(false, *OutConfigFile);
    }
    return true;
}
}
