#include "UeNodeNexusBridgeTranscode.h"
#include "Schema/NexusSchema.h"

#include "UObject/Class.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

namespace UeNodeNexusBridge::Transcode
{
bool IsEditableProperty(const FProperty* Property)
{
    return Property != nullptr
        && Property->HasAnyPropertyFlags(CPF_Edit)
        && !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Deprecated);
}

static UEnum* PropertyEnum(FProperty* Property)
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

static FString PropertyKind(FProperty* Property)
{
    if (PropertyEnum(Property) != nullptr)
    {
        return TEXT("enum");
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
    if (CastField<FClassProperty>(Property) || CastField<FSoftClassProperty>(Property))
    {
        return TEXT("class");
    }
    if (CastField<FObjectPropertyBase>(Property))
    {
        return TEXT("object");
    }
    if (CastField<FStructProperty>(Property))
    {
        return TEXT("struct");
    }
    if (CastField<FArrayProperty>(Property) || CastField<FSetProperty>(Property) || CastField<FMapProperty>(Property))
    {
        return TEXT("array");
    }
    return TEXT("value");
}

FString ExportPropertyValue(const UObject* Object, FProperty* Property)
{
    if (Object == nullptr || Property == nullptr)
    {
        return FString();
    }
    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        if (!CastField<FSoftObjectProperty>(Property))
        {
            const UObject* Value = ObjectProperty->GetObjectPropertyValue_InContainer(Object);
            return Value ? Value->GetPathName() : TEXT("None");
        }
    }
    // ExportTextItem_* always exports the property itself, but it forwards the
    // delta pointer to the members inside it: UScriptStruct::ExportText hands a
    // null Defaults to every member, and FProperty::Identical compares against
    // zero when its other side is null, so any member whose value is zero is
    // dropped. TickGroup=TG_PrePhysics is zero, which is how a written value
    // came back missing and then read as a conflict against the mirror text.
    // Pointing the delta at the value itself makes every member compare equal
    // by address and export unconditionally, so the text round-trips.
    const void* Delta = Property->ContainerPtrToValuePtr<void>(Object);
    FString Value;
    Property->ExportTextItem_InContainer(Value, Object, Delta, const_cast<UObject*>(Object), PPF_None);
    return Value;
}

TArray<TSharedPtr<FJsonValue>> ExportEditableProps(UObject* Object, UObject* Defaults)
{
    TArray<TSharedPtr<FJsonValue>> Props;
    if (Object == nullptr)
    {
        return Props;
    }
    if (Defaults == nullptr)
    {
        Defaults = Object->GetClass()->GetDefaultObject();
    }
    for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        if (!IsEditableProperty(Property))
        {
            continue;
        }
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("name"), Property->GetName());
        Json->SetStringField(TEXT("type"), Property->GetCPPType());
        Json->SetStringField(TEXT("value"), ExportPropertyValue(Object, Property));
        if (Defaults != nullptr && Defaults->GetClass()->IsChildOf(Property->GetOwnerClass()))
        {
            Json->SetStringField(TEXT("default"), ExportPropertyValue(Defaults, Property));
        }
        Props.Add(MakeShared<FJsonValueObject>(Json));
    }
    return Props;
}

bool ImportPropertyValue(UObject* Object, const FString& Name, const FString& Value, FString& OutError, bool bNotify)
{
    if (Object == nullptr)
    {
        OutError = TEXT("object is null");
        return false;
    }
    FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*Name));
    if (Property == nullptr)
    {
        // Saying "no property" reads as a typo when the member exists in C++ but
        // carries no UPROPERTY (UActorComponent::bTickInEditor is one): nothing
        // in the reflection system can reach it, so no text form can express it.
        OutError = FString::Printf(
            TEXT("%s has no reflected property %s; a plain C++ member without UPROPERTY cannot be set from text"),
            *Object->GetClass()->GetName(), *Name);
        return false;
    }
    if (!IsEditableProperty(Property))
    {
        OutError = FString::Printf(TEXT("%s.%s is reflected but not editable (EditConst, Deprecated or no CPF_Edit)"),
            *Object->GetClass()->GetName(), *Name);
        return false;
    }
    Object->Modify();
    bool bOk = false;
    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property); ObjectProperty != nullptr && !CastField<FSoftObjectProperty>(Property))
    {
        FString Path = Value;
        Path.TrimStartAndEndInline();
        if (Path.Contains(TEXT("'")))
        {
            Path = Path.Mid(Path.Find(TEXT("'")) + 1);
            Path.RemoveFromEnd(TEXT("'"));
        }
        UObject* NewValue = Path.IsEmpty() || Path.Equals(TEXT("None"), ESearchCase::IgnoreCase) ? nullptr : ResolveObjectByPath(Path);
        if (NewValue == nullptr && !Path.IsEmpty() && !Path.Equals(TEXT("None"), ESearchCase::IgnoreCase))
        {
            OutError = FString::Printf(TEXT("%s: object not found: %s"), *Name, *Path);
            return false;
        }
        if (NewValue != nullptr && ObjectProperty->PropertyClass != nullptr && !NewValue->IsA(ObjectProperty->PropertyClass))
        {
            OutError = FString::Printf(TEXT("%s: %s is not a %s"), *Name, *Path, *ObjectProperty->PropertyClass->GetName());
            return false;
        }
        ObjectProperty->SetObjectPropertyValue_InContainer(Object, NewValue);
        bOk = true;
    }
    else
    {
        bOk = ApplyPropertyText(Object, Property, Value);
        if (!bOk)
        {
            OutError = FString::Printf(TEXT("%s: ImportText failed for %s"), *Name, *Value);
        }
    }
    if (bOk && bNotify)
    {
        FPropertyChangedEvent Event(Property);
        Object->PostEditChangeProperty(Event);
    }
    return bOk;
}

TSharedPtr<FJsonObject> PropertySchemaJson(FProperty* Property, UObject* Cdo)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("type"), Property->GetCPPType());
    Json->SetStringField(TEXT("kind"), PropertyKind(Property));
    if (Cdo != nullptr && Cdo->GetClass()->IsChildOf(Property->GetOwnerClass()))
    {
        Json->SetStringField(TEXT("default"), ExportPropertyValue(Cdo, Property));
    }
    if (UEnum* Enum = PropertyEnum(Property))
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
        {
            if (!Enum->HasMetaData(TEXT("Hidden"), Index))
            {
                Values.Add(MakeShared<FJsonValueString>(Enum->GetNameStringByIndex(Index)));
            }
        }
        Json->SetArrayField(TEXT("enum_values"), Values);
        Json->SetStringField(TEXT("enum_type"), Enum->GetPathName());
    }
    if (Property->HasMetaData(TEXT("ClampMin")))
    {
        Json->SetStringField(TEXT("clamp_min"), Property->GetMetaData(TEXT("ClampMin")));
    }
    if (Property->HasMetaData(TEXT("ClampMax")))
    {
        Json->SetStringField(TEXT("clamp_max"), Property->GetMetaData(TEXT("ClampMax")));
    }
    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        Json->SetStringField(TEXT("object_class"), ObjectProperty->PropertyClass ? ObjectProperty->PropertyClass->GetPathName() : FString());
    }
    if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        Json->SetStringField(TEXT("struct_type"), StructProperty->Struct ? StructProperty->Struct->GetPathName() : FString());
    }
    AddPropertyMetadata(Property, Json);
    return Json;
}
}
