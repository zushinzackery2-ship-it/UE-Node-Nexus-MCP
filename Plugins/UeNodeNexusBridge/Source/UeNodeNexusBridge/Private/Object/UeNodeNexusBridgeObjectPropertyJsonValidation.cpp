#include "UeNodeNexusBridgeObjectPropertyJsonValidation.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
const FProperty* ResolveStructJsonField(const UStruct* Struct, const FString& Name)
{
    const FProperty* Result = nullptr;
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        if (It->GetName().Equals(Name, ESearchCase::IgnoreCase)
            || It->GetAuthoredName().Equals(Name, ESearchCase::IgnoreCase))
        {
            if (Result && Result != *It)
            {
                return nullptr;
            }
            Result = *It;
        }
    }
    return Result;
}

static bool ValidateJsonPropertyValue(
    const FProperty* Property,
    const TSharedPtr<FJsonValue>& Value,
    const FString& Path,
    FString& OutError);

static bool ValidateJsonArray(
    const FProperty* ElementProperty,
    const TSharedPtr<FJsonValue>& Value,
    const FString& Path,
    FString& OutError)
{
    if (Value->Type != EJson::Array)
    {
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
    for (int32 Index = 0; Index < Items.Num(); ++Index)
    {
        const FString ElementPath = FString::Printf(TEXT("%s[%d]"), *Path, Index);
        if (!ValidateJsonPropertyValue(ElementProperty, Items[Index], ElementPath, OutError))
        {
            return false;
        }
    }
    return true;
}

static bool ValidateJsonPropertyValue(
    const FProperty* Property,
    const TSharedPtr<FJsonValue>& Value,
    const FString& Path,
    FString& OutError)
{
    if (!Value.IsValid() || Property->ArrayDim != 1)
    {
        OutError = TEXT("struct_json_type_mismatch: ") + Path;
        return false;
    }

    const EJson Type = Value->Type;
    bool bMatches = true;
    if (CastField<FBoolProperty>(Property) != nullptr)
    {
        bMatches = Type == EJson::Boolean;
    }
    else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
    {
        bMatches = Type == EJson::Number || (ByteProperty->Enum != nullptr && Type == EJson::String);
    }
    else if (CastField<FNumericProperty>(Property) != nullptr)
    {
        bMatches = Type == EJson::Number;
    }
    else if (CastField<FEnumProperty>(Property) != nullptr)
    {
        bMatches = Type == EJson::Number || Type == EJson::String;
    }
    else if (CastField<FStrProperty>(Property) != nullptr
        || CastField<FNameProperty>(Property) != nullptr
        || CastField<FTextProperty>(Property) != nullptr)
    {
        bMatches = Type == EJson::String;
    }
    else if (CastField<FObjectPropertyBase>(Property) != nullptr)
    {
        bMatches = Type == EJson::String || Type == EJson::Null;
    }
    else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        bMatches = Type == EJson::Object && ValidateStructJson(
            StructProperty->Struct, Value->AsObject(), Path + TEXT("."), OutError);
    }
    else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        bMatches = ValidateJsonArray(ArrayProperty->Inner, Value, Path, OutError);
    }
    else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
    {
        bMatches = ValidateJsonArray(SetProperty->ElementProp, Value, Path, OutError);
    }
    else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
    {
        bMatches = Type == EJson::Object;
        if (bMatches)
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Item : Value->AsObject()->Values)
            {
                if (!ValidateJsonPropertyValue(MapProperty->ValueProp, Item.Value, Path + TEXT(".") + Item.Key, OutError))
                {
                    return false;
                }
            }
        }
    }

    if (!bMatches && OutError.IsEmpty())
    {
        OutError = TEXT("struct_json_type_mismatch: ") + Path;
    }
    return bMatches;
}

bool ValidateStructJson(
    const UStruct* Struct,
    const TSharedPtr<FJsonObject>& Json,
    const FString& Prefix,
    FString& OutError)
{
    TSet<const FProperty*> Seen;
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Json->Values)
    {
        const FProperty* FieldProperty = ResolveStructJsonField(Struct, Field.Key);
        const FString Path = Prefix + Field.Key;
        if (FieldProperty == nullptr)
        {
            OutError = TEXT("unknown_struct_field: ") + Path;
            return false;
        }
        if (Seen.Contains(FieldProperty))
        {
            OutError = TEXT("duplicate_struct_field: ") + Path;
            return false;
        }
        Seen.Add(FieldProperty);
        if (!ValidateJsonPropertyValue(FieldProperty, Field.Value, Path, OutError))
        {
            return false;
        }
    }
    return true;
}
}
