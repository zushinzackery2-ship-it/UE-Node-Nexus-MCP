#include "UeNodeNexusBridgeNiagaraIndexedFormat.h"

#include "Dom/JsonObject.h"
#include "Containers/StringConv.h"

namespace UeNodeNexusBridge::NiagaraIndexedFormat
{
static int32 CountTextLines(const FString& Text)
{
    if (Text.IsEmpty())
    {
        return 0;
    }

    int32 Lines = 1;
    for (int32 Index = 0; Index < Text.Len(); ++Index)
    {
        if (Text[Index] == TEXT('\n'))
        {
            ++Lines;
        }
    }
    if (Text.EndsWith(TEXT("\n")))
    {
        --Lines;
    }
    return Lines;
}

FString EscapeToken(const FString& Input)
{
    FString Output = Input;
    Output.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Output.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Output.ReplaceInline(TEXT(";"), TEXT("\\;"));
    Output.ReplaceInline(TEXT("|"), TEXT("\\|"));
    Output.ReplaceInline(TEXT(":"), TEXT("\\:"));
    Output.ReplaceInline(TEXT("="), TEXT("\\="));
    return Output;
}

int32 DictIndex(TMap<FString, int32>& Dict, TArray<FString>& Items, const FString& Value)
{
    if (const int32* Existing = Dict.Find(Value))
    {
        return *Existing;
    }

    const int32 Index = Items.Num();
    Dict.Add(Value, Index);
    Items.Add(Value);
    return Index;
}

FString JoinDictionaryLine(const FString& Prefix, const TArray<FString>& Items)
{
    TArray<FString> Parts;
    for (int32 Index = 0; Index < Items.Num(); ++Index)
    {
        Parts.Add(FString::Printf(TEXT("%d=%s"), Index, *EscapeToken(Items[Index])));
    }
    return Prefix + FString::Join(Parts, TEXT(";")) + TEXT("\n");
}

void SetTextPayload(const TSharedPtr<FJsonObject>& Data, const FString& Text)
{
    if (!Data.IsValid())
    {
        return;
    }

    const FTCHARToUTF8 Utf8Text(*Text);
    const int32 TextBytes = Utf8Text.Length();
    Data->SetStringField(TEXT("text"), Text);
    Data->SetNumberField(TEXT("text_bytes"), TextBytes);
    Data->SetNumberField(TEXT("text_kib"), static_cast<double>(TextBytes) / 1024.0);
    Data->SetNumberField(TEXT("text_lines"), CountTextLines(Text));
}
}
