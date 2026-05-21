#include "UeNodeNexusBridgeMaterialPropertySchema.h"

#include "Dom/JsonValue.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "UObject/Class.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
static UEnum* ResolvePropertyEnum(FProperty* Property);

static FString PropertyKind(FProperty* Property)
{
    if (ResolvePropertyEnum(Property) != nullptr)
    {
        return TEXT("enum");
    }
    if (CastField<FClassProperty>(Property) || CastField<FSoftClassProperty>(Property))
    {
        return TEXT("class");
    }
    if (CastField<FBoolProperty>(Property))
    {
        return TEXT("bool");
    }
    if (CastField<FNumericProperty>(Property))
    {
        return TEXT("number");
    }
    if (CastField<FNameProperty>(Property))
    {
        return TEXT("name");
    }
    if (CastField<FStrProperty>(Property))
    {
        return TEXT("string");
    }
    if (CastField<FTextProperty>(Property))
    {
        return TEXT("text");
    }
    if (CastField<FObjectPropertyBase>(Property))
    {
        return TEXT("object");
    }
    if (CastField<FStructProperty>(Property))
    {
        return TEXT("struct");
    }
    if (CastField<FArrayProperty>(Property))
    {
        return TEXT("array");
    }
    if (CastField<FSetProperty>(Property))
    {
        return TEXT("set");
    }
    if (CastField<FMapProperty>(Property))
    {
        return TEXT("map");
    }
    return TEXT("value");
}

static UEnum* ResolvePropertyEnum(FProperty* Property)
{
    if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
    {
        return EnumProperty->GetEnum();
    }
    if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
    {
        return ByteProperty->Enum;
    }
    return nullptr;
}

static TArray<TSharedPtr<FJsonValue>> BuildEnumValues(UEnum* Enum)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    if (Enum == nullptr)
    {
        return Values;
    }

    for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
    {
        if (Enum->HasMetaData(TEXT("Hidden"), Index))
        {
            continue;
        }

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Enum->GetNameStringByIndex(Index));
        Item->SetStringField(TEXT("display_name"), Enum->GetDisplayNameTextByIndex(Index).ToString());
        Item->SetNumberField(TEXT("value"), static_cast<double>(Enum->GetValueByIndex(Index)));
        Values.Add(MakeShared<FJsonValueObject>(Item));
    }
    return Values;
}

static void AddClassConstraint(TSharedPtr<FJsonObject> Json, FProperty* Property)
{
    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        Json->SetStringField(TEXT("object_class"), ObjectProperty->PropertyClass ? ObjectProperty->PropertyClass->GetPathName() : FString());
    }
    if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
    {
        Json->SetStringField(TEXT("meta_class"), ClassProperty->MetaClass ? ClassProperty->MetaClass->GetPathName() : FString());
    }
    if (FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
    {
        Json->SetStringField(TEXT("meta_class"), SoftClassProperty->MetaClass ? SoftClassProperty->MetaClass->GetPathName() : FString());
    }
}

static void AddNestedPropertySchema(TSharedPtr<FJsonObject> Json, const FString& FieldName, FProperty* Property)
{
    if (Property == nullptr)
    {
        return;
    }

    TSharedPtr<FJsonObject> Nested = MakeShared<FJsonObject>();
    Nested->SetStringField(TEXT("name"), Property->GetName());
    Nested->SetStringField(TEXT("type"), Property->GetCPPType());
    Nested->SetStringField(TEXT("kind"), PropertyKind(Property));
    if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        Nested->SetStringField(TEXT("struct_type"), StructProperty->Struct ? StructProperty->Struct->GetPathName() : FString());
    }
    AddClassConstraint(Nested, Property);
    if (UEnum* Enum = ResolvePropertyEnum(Property))
    {
        Nested->SetStringField(TEXT("enum_type"), Enum->GetPathName());
        Nested->SetArrayField(TEXT("enum_values"), BuildEnumValues(Enum));
    }
    Json->SetObjectField(FieldName, Nested);
}

static void AddContainerSchema(TSharedPtr<FJsonObject> Json, FProperty* Property)
{
    if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        AddNestedPropertySchema(Json, TEXT("array_inner"), ArrayProperty->Inner);
        return;
    }
    if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
    {
        AddNestedPropertySchema(Json, TEXT("set_inner"), SetProperty->ElementProp);
        return;
    }
    if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
    {
        AddNestedPropertySchema(Json, TEXT("map_key"), MapProperty->KeyProp);
        AddNestedPropertySchema(Json, TEXT("map_value"), MapProperty->ValueProp);
    }
}

static void AddPropertyMetadata(TSharedPtr<FJsonObject> Json, FProperty* Property)
{
    static const TCHAR* MetadataKeys[] =
    {
        TEXT("Category"),
        TEXT("DisplayName"),
        TEXT("ToolTip"),
        TEXT("ClampMin"),
        TEXT("ClampMax"),
        TEXT("UIMin"),
        TEXT("UIMax"),
        TEXT("AllowedClasses"),
        TEXT("DisallowedClasses"),
        TEXT("ExactClass"),
        TEXT("NoClear"),
    };

    TSharedPtr<FJsonObject> Metadata = MakeShared<FJsonObject>();
    for (const TCHAR* Key : MetadataKeys)
    {
        if (Property->HasMetaData(Key))
        {
            Metadata->SetStringField(Key, Property->GetMetaData(Key));
        }
    }
    Json->SetObjectField(TEXT("metadata"), Metadata);
}

static void AddCurrentValue(TSharedPtr<FJsonObject> Json, UMaterialExpression* Expression, FProperty* Property)
{
    if (Expression == nullptr)
    {
        return;
    }

    FString Value;
    Property->ExportTextItem_InContainer(Value, Expression, nullptr, Expression, PPF_None);
    Json->SetStringField(TEXT("value"), Value);
}

static void AddDefaultValue(TSharedPtr<FJsonObject> Json, UClass* Class, FProperty* Property)
{
    UObject* Cdo = Class ? Class->GetDefaultObject(false) : nullptr;
    if (Cdo == nullptr)
    {
        return;
    }

    FString Value;
    Property->ExportTextItem_InContainer(Value, Cdo, nullptr, Cdo, PPF_None);
    Json->SetStringField(TEXT("default_value"), Value);
}

static TSharedPtr<FJsonObject> BuildPropertyJson(FProperty* Property, int32 Index, UClass* OwnerClass, UMaterialExpression* Expression)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("index"), Index);
    Json->SetStringField(TEXT("name"), Property->GetName());
    Json->SetStringField(TEXT("type"), Property->GetCPPType());
    Json->SetStringField(TEXT("cpp_type"), Property->GetCPPType(nullptr, CPPF_None));
    Json->SetStringField(TEXT("kind"), PropertyKind(Property));
    Json->SetBoolField(TEXT("editable"), true);
    Json->SetStringField(TEXT("property_flags"), FString::Printf(TEXT("%llu"), static_cast<uint64>(Property->GetPropertyFlags())));

    if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        Json->SetStringField(TEXT("struct_type"), StructProperty->Struct ? StructProperty->Struct->GetPathName() : FString());
    }
    if (UEnum* Enum = ResolvePropertyEnum(Property))
    {
        Json->SetStringField(TEXT("enum_type"), Enum->GetPathName());
        Json->SetArrayField(TEXT("enum_values"), BuildEnumValues(Enum));
    }

    AddClassConstraint(Json, Property);
    AddContainerSchema(Json, Property);
    AddPropertyMetadata(Json, Property);
    AddDefaultValue(Json, OwnerClass, Property);
    AddCurrentValue(Json, Expression, Property);
    return Json;
}

TArray<TSharedPtr<FJsonValue>> BuildMaterialExpressionClassParams(UClass* Class)
{
    TArray<TSharedPtr<FJsonValue>> Params;
    if (Class == nullptr)
    {
        return Params;
    }

    int32 Index = 0;
    for (TFieldIterator<FProperty> It(Class); It; ++It)
    {
        FProperty* Property = *It;
        if (IsEditableMaterialExpressionProperty(Property))
        {
            Params.Add(MakeShared<FJsonValueObject>(BuildPropertyJson(Property, Index++, Class, nullptr)));
        }
    }
    return Params;
}

TArray<TSharedPtr<FJsonValue>> BuildMaterialExpressionParams(UMaterialExpression* Expression)
{
    TArray<TSharedPtr<FJsonValue>> Params;
    if (Expression == nullptr)
    {
        return Params;
    }

    int32 Index = 0;
    for (TFieldIterator<FProperty> It(Expression->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        if (IsEditableMaterialExpressionProperty(Property))
        {
            Params.Add(MakeShared<FJsonValueObject>(BuildPropertyJson(Property, Index++, Expression->GetClass(), Expression)));
        }
    }
    if (UMaterialExpressionNamedRerouteUsage* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Expression))
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetNumberField(TEXT("index"), Params.Num());
        Json->SetStringField(TEXT("name"), TEXT("DeclarationName"));
        Json->SetStringField(TEXT("type"), TEXT("FName"));
        Json->SetStringField(TEXT("cpp_type"), TEXT("FName"));
        Json->SetStringField(TEXT("kind"), TEXT("synthetic"));
        Json->SetStringField(TEXT("value"), Usage->Declaration ? Usage->Declaration->Name.ToString() : FString());
        Json->SetBoolField(TEXT("editable"), true);
        Params.Add(MakeShared<FJsonValueObject>(Json));
    }
    return Params;
}

}
