#include "UeNodeNexusBridgeObjectPropertyJsonValidation.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonValue> NormalizeValue(const FProperty* Property, const TSharedPtr<FJsonValue>& Value)
{
    if (CastField<FObjectPropertyBase>(Property) && Value->Type == EJson::Null)
    {
        // JsonObjectConverter skips JSON null fields; ImportText None clears them.
        return MakeShared<FJsonValueString>(TEXT("None"));
    }
    if (const auto* Struct = CastField<FStructProperty>(Property))
    {
        return MakeShared<FJsonValueObject>(NormalizeStructJson(Struct->Struct, Value->AsObject()));
    }
    const FProperty* Element = nullptr;
    if (const auto* Array = CastField<FArrayProperty>(Property))
    {
        Element = Array->Inner;
    }
    else if (const auto* Set = CastField<FSetProperty>(Property))
    {
        Element = Set->ElementProp;
    }
    if (Element)
    {
        TArray<TSharedPtr<FJsonValue>> Items;
        for (const auto& Item : Value->AsArray())
        {
            Items.Add(NormalizeValue(Element, Item));
        }
        return MakeShared<FJsonValueArray>(Items);
    }
    if (const auto* Map = CastField<FMapProperty>(Property))
    {
        auto Items = MakeShared<FJsonObject>();
        for (const auto& Pair : Value->AsObject()->Values)
        {
            Items->SetField(Pair.Key, NormalizeValue(Map->ValueProp, Pair.Value));
        }
        return MakeShared<FJsonValueObject>(Items);
    }
    return Value;
}

TSharedPtr<FJsonObject> NormalizeStructJson(const UStruct* Struct, const TSharedPtr<FJsonObject>& Json)
{
    auto Result = MakeShared<FJsonObject>();
    for (const auto& Pair : Json->Values)
    {
        // Called only after recursive validation has resolved every field.
        const FProperty* Property = ResolveStructJsonField(Struct, Pair.Key);
        Result->SetField(Property->GetAuthoredName(), NormalizeValue(Property, Pair.Value));
    }
    return Result;
}
}
