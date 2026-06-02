#include "UeNodeNexusBridgeAnimReflectionUtils.h"

#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
bool ExportAnimFieldValue(UObject* Owner, const void* Container, FProperty* Property, FString& OutValue)
{
    if (Property == nullptr || Container == nullptr)
    {
        return false;
    }

    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
        UObject* ValueObject = ObjectProperty->GetObjectPropertyValue(ValuePtr);
        OutValue = ValueObject ? ValueObject->GetPathName() : FString();
        return !OutValue.IsEmpty();
    }

    FString Value;
    const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
    Property->ExportTextItem_Direct(Value, ValuePtr, nullptr, Owner, PPF_None);
    OutValue = CleanExportedPropertyText(Value, 160, /*bStripQuotes*/ true);
    return !OutValue.IsEmpty() && !OutValue.Equals(TEXT("None"), ESearchCase::IgnoreCase);
}

FProperty* FindAnimPropertyCaseInsensitive(UStruct* Struct, const FString& Name)
{
    if (Struct == nullptr)
    {
        return nullptr;
    }

    for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FProperty* Property = *It;
        if (Property != nullptr && Property->GetName().Equals(Name, ESearchCase::IgnoreCase))
        {
            return Property;
        }
    }
    return nullptr;
}

void AddAnimField(TArray<FString>& Parts, UObject* Owner, const void* Container, UStruct* Struct, const FString& FieldName, const FString& Alias)
{
    FString Value;
    if (ExportAnimFieldValue(Owner, Container, FindAnimPropertyCaseInsensitive(Struct, FieldName), Value))
    {
        Parts.Add(FString::Printf(TEXT("%s=%s"), *Alias, *Value));
    }
}

void AddAnimStructField(TArray<FString>& Parts, UObject* Owner, const void* Container, UStruct* Struct, const FString& StructFieldName, const FString& InnerFieldName, const FString& Alias)
{
    FStructProperty* StructProperty = CastField<FStructProperty>(FindAnimPropertyCaseInsensitive(Struct, StructFieldName));
    if (StructProperty == nullptr)
    {
        return;
    }

    const void* StructPtr = StructProperty->ContainerPtrToValuePtr<void>(Container);
    AddAnimField(Parts, Owner, StructPtr, StructProperty->Struct, InnerFieldName, Alias);
}
}
