#include "Text/NexusPropertyTextImport.h"
#include "UeNodeNexusBridgeObjectPropertyJsonValidation.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

#include "Misc/ScopeExit.h"
#include "Misc/DefaultValueHelper.h"
#include "UObject/UnrealType.h"
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace UeNodeNexusBridge
{
namespace
{
bool Members(const FString& Text, TArray<FString>& Parts)
{
    if (!Text.StartsWith(TEXT("(")) || !Text.EndsWith(TEXT(")")))
    {
        return false;
    }
    int32 Start = 1, Depth = 0;
    bool Quoted = false;
    for (int32 Index = 1; Index < Text.Len() - 1; ++Index)
    {
        const TCHAR Character = Text[Index];
        if (Quoted && Character == TEXT('\\'))
        {
            ++Index;
            continue;
        }
        if (Character == TEXT('"')) Quoted = !Quoted;
        if (Quoted) continue;
        Depth += Character == TEXT('(') ? 1 : Character == TEXT(')') ? -1 : 0;
        if (Depth < 0) return false;
        if (Depth == 0 && Character == TEXT(','))
        {
            Parts.Add(Text.Mid(Start, Index - Start).TrimStartAndEnd());
            Start = Index + 1;
        }
    }
    const FString Last = Text.Mid(Start, Text.Len() - Start - 1).TrimStartAndEnd();
    if (!Last.IsEmpty() || Parts.Num()) Parts.Add(Last);
    return Depth == 0 && !Quoted;
}

bool Number(const FNumericProperty* Property, FString& Text)
{
    if (Property->IsFloatingPoint() && Text.EndsWith(TEXT("f"), ESearchCase::IgnoreCase)) Text.LeftChopInline(1);
    FTCHARToUTF8 Input(*Text);
    const char* Begin = Input.Get();
    const char* End = Begin + Input.Length();
    if (Begin == End) return false;
    if (Property->IsFloatingPoint())
    {
        char* ParsedEnd = nullptr;
        const double Value = std::strtod(Begin, &ParsedEnd);
        if (ParsedEnd != End || !std::isfinite(Value) || !FDefaultValueHelper::IsStringValidFloat(Text)) return false;
        if (Property->GetElementSize() == sizeof(float))
        {
            const float Narrow = static_cast<float>(Value);
            if (!std::isfinite(Narrow)) return false;
            Text = Transcode::ExportFloat(Narrow);
        }
        else Text = Transcode::ExportDouble(Value);
        return true;
    }
    const bool Negative = *Begin == '-';
    if (*Begin == '+' || Negative) ++Begin;
    int32 Base = 10;
    if (End - Begin >= 2 && Begin[0] == '0' && (Begin[1] == 'x' || Begin[1] == 'X'))
    {
        Begin += 2;
        Base = 16;
    }
    uint64 Value = 0;
    const auto Result = std::from_chars(Begin, End, Value, Base);
    const int32 Bits = Property->GetElementSize() * 8;
    if (Result.ec != std::errc() || Result.ptr != End) return false;
    const bool Unsigned = CastField<FByteProperty>(Property) || CastField<FUInt16Property>(Property)
        || CastField<FUInt32Property>(Property) || CastField<FUInt64Property>(Property);
    if (Unsigned && (Negative || (Bits < 64 && Value >= (uint64(1) << Bits)))) return false;
    if (!Unsigned && Value > (uint64(1) << (Bits - 1)) - (Negative ? 0 : 1)) return false;
    Text = (Negative ? TEXT("-") : TEXT("")) + LexToString(Value);
    return true;
}

bool Normalize(const FProperty* Property, UObject* Owner, FString& Text, int32 Flags, FString& Error);

bool Structure(const FStructProperty* Property, UObject* Owner, FString& Text, FString& Error)
{
    TArray<FString> Parts;
    if (!Members(Text, Parts)) return false;
    TSet<FString> Seen;
    for (FString& Part : Parts)
    {
        FString Name, Value;
        if (!Part.Split(TEXT("="), &Name, &Value)) return false;
        Name.TrimStartAndEndInline();
        FString Field = Name;
        int32 ArrayIndex = 0;
        int32 Bracket;
        if (Name.FindChar(TEXT('['), Bracket))
        {
            if (!Name.EndsWith(TEXT("]"))) return false;
            const FString Index = Name.Mid(Bracket + 1, Name.Len() - Bracket - 2);
            const FTCHARToUTF8 IndexBytes(*Index);
            const auto Parsed = std::from_chars(IndexBytes.Get(), IndexBytes.Get() + IndexBytes.Length(), ArrayIndex);
            if (Parsed.ec != std::errc() || Parsed.ptr != IndexBytes.Get() + IndexBytes.Length()) return false;
            Field = Name.Left(Bracket);
        }
        const FProperty* Member = ResolveStructJsonField(Property->Struct, Field);
        if (!Member || ArrayIndex < 0 || ArrayIndex >= Member->ArrayDim) return false;
        if (!Member->ShouldPort(PPF_None))
        {
            Error = TEXT("member does not support text import: ") + Member->GetName();
            return false;
        }
        const FString Identity = Member->GetName() + TEXT(":") + LexToString(ArrayIndex);
        if (Seen.Contains(Identity)) return false;
        Seen.Add(Identity);
        if (!Normalize(Member, Owner, Value, PPF_Delimited, Error)) return false;
        Part = Name + TEXT("=") + Value;
    }
    Text = TEXT("(") + FString::Join(Parts, TEXT(",")) + TEXT(")");
    return true;
}

bool Normalize(const FProperty* Property, UObject* Owner, FString& Text, int32 Flags, FString& Error)
{
    if (!CastField<FStrProperty>(Property) || (Flags & PPF_Delimited)) Text.TrimStartAndEndInline();
    if (const auto* Numeric = CastField<FNumericProperty>(Property); Numeric && !Numeric->IsEnum())
    {
        return Number(Numeric, Text);
    }
    const auto* Struct = CastField<FStructProperty>(Property);
    if (Struct && !(Struct->Struct->StructFlags & STRUCT_ImportTextItemNative))
    {
        return Structure(Struct, Owner, Text, Error);
    }
    const auto* Array = CastField<FArrayProperty>(Property);
    const auto* Set = CastField<FSetProperty>(Property);
    const auto* Map = CastField<FMapProperty>(Property);
    if (Array || Set || Map)
    {
        if (Text.IsEmpty()) Text = TEXT("()");
        TArray<FString> Parts;
        if (!Members(Text, Parts)) return false;
        for (FString& Part : Parts)
        {
            if (!Map)
            {
                if (!Normalize(Array ? Array->Inner : Set->ElementProp, Owner, Part, PPF_Delimited, Error)) return false;
                continue;
            }
            TArray<FString> Pair;
            if (!Members(Part, Pair) || Pair.Num() != 2) return false;
            if (!Normalize(Map->KeyProp, Owner, Pair[0], PPF_Delimited, Error)
                || !Normalize(Map->ValueProp, Owner, Pair[1], PPF_Delimited, Error)) return false;
            Part = TEXT("(") + Pair[0] + TEXT(",") + Pair[1] + TEXT(")");
        }
        Text = TEXT("(") + FString::Join(Parts, TEXT(",")) + TEXT(")");
        return true;
    }
    void* Scratch = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
    Property->InitializeValue(Scratch);
    ON_SCOPE_EXIT { Property->DestroyValue(Scratch); FMemory::Free(Scratch); };
    FStringOutputDevice Errors;
    const TCHAR* End = *Text;
    if (Struct)
    {
        // UScriptStruct falls back to reflected members when a native parser fails.
        // The custom grammar must succeed on its own, including its cursor boundary.
        if (!Struct->Struct->GetCppStructOps()->ImportTextItem(End, Scratch, Flags, Owner, &Errors)) End = nullptr;
    }
    else End = Property->ImportText_Direct(*Text, Scratch, Owner, Flags, &Errors);
    if (End) while (FChar::IsWhitespace(*End)) ++End;
    if (!End || *End || !Errors.IsEmpty())
    {
        Error = Errors.IsEmpty() ? TEXT("incomplete or invalid value: ") + Text : FString(Errors);
        return false;
    }
    return true;
}
}

bool ImportCompletePropertyText(const FProperty* Property, void* Storage, UObject* Owner,
    const FString& Text, FString& Error)
{
    Error.Reset();
    FString Canonical = Text;
    if (!Normalize(Property, Owner, Canonical, PPF_None, Error))
    {
        if (Error.IsEmpty()) Error = TEXT("invalid property text: ") + Property->GetName();
        return false;
    }
    FStringOutputDevice Errors;
    const TCHAR* End = Property->ImportText_Direct(*Canonical, Storage, Owner, PPF_None, &Errors);
    if (End) while (FChar::IsWhitespace(*End)) ++End;
    if (!End || *End || !Errors.IsEmpty())
    {
        Error = Errors.IsEmpty() ? TEXT("property text was not fully imported") : FString(Errors);
        return false;
    }
    return true;
}
}
