#include "Transcode/Properties/NexusPropertyText.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge::Transcode
{
static FString DecimalNumber(FString Text)
{
    FString Mantissa;
    FString Exponent;
    if (!Text.Split(TEXT("e"), &Mantissa, &Exponent, ESearchCase::IgnoreCase))
    {
        return Text;
    }
    // FNumericProperty::ImportText converts scientific notation, but stops its
    // cursor before the exponent. Container import then rejects the delimiter.
    const bool bNegative = Mantissa.RemoveFromStart(TEXT("-"));
    int32 Point = Mantissa.Find(TEXT("."));
    if (Point == INDEX_NONE)
    {
        Point = Mantissa.Len();
    }
    Mantissa.ReplaceInline(TEXT("."), TEXT(""));
    Point += FCString::Atoi(*Exponent);
    if (Point <= 0)
    {
        Text = TEXT("0.") + FString::ChrN(-Point, TEXT('0')) + Mantissa;
    }
    else if (Point >= Mantissa.Len())
    {
        Text = Mantissa + FString::ChrN(Point - Mantissa.Len(), TEXT('0'));
    }
    else
    {
        Text = Mantissa.Left(Point) + TEXT(".") + Mantissa.Mid(Point);
    }
    return bNegative ? TEXT("-") + Text : Text;
}

FString ExportFloat(float Value)
{
    return DecimalNumber(FString::Printf(TEXT("%.9g"), static_cast<double>(Value)));
}

FString ExportDouble(double Value)
{
    return DecimalNumber(FString::Printf(TEXT("%.17g"), Value));
}

static FString NativeText(const FProperty* Property, const void* Value, UObject* Owner, int32 Flags)
{
    FString Text;
    Property->ExportTextItem_Direct(Text, Value, Value, Owner, Flags);
    return Text;
}

static FString StructMember(const FString& Text, const FStructProperty* Struct, const void* Value, UObject* Owner, int32 Flags)
{
    FString Name;
    FString Original;
    if (!Text.Split(TEXT("="), &Name, &Original))
    {
        return Text;
    }
    const FString Key = Name.TrimStartAndEnd();
    for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
    {
        const FProperty* Member = *It;
        if (Member->GetName() == Key || Member->GetAuthoredName() == Key)
        {
            return Name + TEXT("=") + ExportPrecisePropertyText(Member, Member->ContainerPtrToValuePtr<void>(Value), Owner, Flags | PPF_Delimited);
        }
    }
    return Text;
}

static FString StructText(const FStructProperty* Property, const void* Value, UObject* Owner, int32 Flags)
{
    // Keep native ImportText grammar, omitted fields and quoted string contents.
    // Only reflected member values are re-exported, recursively, at full precision.
    const FString Original = NativeText(Property, Value, Owner, Flags);
    if (!Original.StartsWith(TEXT("(")) || !Original.EndsWith(TEXT(")")))
    {
        return Original;
    }
    TArray<FString> Members;
    int32 Start = 1;
    int32 Depth = 0;
    bool bQuoted = false;
    for (int32 Index = Start; Index < Original.Len() - 1; ++Index)
    {
        const TCHAR Character = Original[Index];
        if (bQuoted && Character == TEXT('\\'))
        {
            ++Index;
            continue;
        }
        if (Character == TEXT('"'))
        {
            bQuoted = !bQuoted;
        }
        if (bQuoted)
        {
            continue;
        }
        Depth += Character == TEXT('(') ? 1 : Character == TEXT(')') ? -1 : 0;
        if (Depth == 0 && Character == TEXT(','))
        {
            Members.Add(StructMember(Original.Mid(Start, Index - Start), Property, Value, Owner, Flags));
            Start = Index + 1;
        }
    }
    Members.Add(StructMember(Original.Mid(Start, Original.Len() - Start - 1), Property, Value, Owner, Flags));
    return TEXT("(") + FString::Join(Members, TEXT(",")) + TEXT(")");
}

FString ExportPrecisePropertyText(const FProperty* Property, const void* Value, UObject* Owner, int32 Flags)
{
    if (const FFloatProperty* Number = CastField<FFloatProperty>(Property))
    {
        return ExportFloat(Number->GetPropertyValue(Value));
    }
    if (const FDoubleProperty* Number = CastField<FDoubleProperty>(Property))
    {
        return ExportDouble(Number->GetPropertyValue(Value));
    }
    if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        return StructText(Struct, Value, Owner, Flags);
    }
    TArray<FString> Items;
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Helper(Array, Value);
        Items.Reserve(Helper.Num());
        for (int32 Index = 0; Index < Helper.Num(); ++Index)
        {
            Items.Add(ExportPrecisePropertyText(Array->Inner, Helper.GetRawPtr(Index), Owner, Flags | PPF_Delimited));
        }
    }
    else if (const FSetProperty* Set = CastField<FSetProperty>(Property))
    {
        FScriptSetHelper Helper(Set, Value);
        Items.Reserve(Helper.Num());
        for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
        {
            if (Helper.IsValidIndex(Index))
            {
                Items.Add(ExportPrecisePropertyText(Set->ElementProp, Helper.GetElementPtr(Index), Owner, Flags | PPF_Delimited));
            }
        }
    }
    else if (const FMapProperty* Map = CastField<FMapProperty>(Property))
    {
        FScriptMapHelper Helper(Map, Value);
        Items.Reserve(Helper.Num());
        for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
        {
            if (Helper.IsValidIndex(Index))
            {
                const FString Key = ExportPrecisePropertyText(Map->KeyProp, Helper.GetKeyPtr(Index), Owner, Flags | PPF_Delimited);
                const FString Item = ExportPrecisePropertyText(Map->ValueProp, Helper.GetValuePtr(Index), Owner, Flags | PPF_Delimited);
                Items.Add(TEXT("(") + Key + TEXT(",") + Item + TEXT(")"));
            }
        }
    }
    else
    {
        return NativeText(Property, Value, Owner, Flags);
    }
    return TEXT("(") + FString::Join(Items, TEXT(",")) + TEXT(")");
}
}
