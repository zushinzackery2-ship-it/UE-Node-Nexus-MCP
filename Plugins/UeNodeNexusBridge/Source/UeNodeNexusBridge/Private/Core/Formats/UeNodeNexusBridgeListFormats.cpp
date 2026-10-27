#include "UeNodeNexusBridgeListFormats.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UeNodeNexusBridge
{
void SetColumns(const TSharedPtr<FJsonObject>& Data, std::initializer_list<const TCHAR*> Columns)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    Values.Reserve(Columns.size());
    for (const TCHAR* Column : Columns)
    {
        Values.Add(MakeShared<FJsonValueString>(Column));
    }
    Data->SetArrayField(TEXT("columns"), Values);
}

FString EscapeIndexedToken(const FString& Input)
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
        Parts.Add(FString::Printf(TEXT("%d=%s"), Index, *EscapeIndexedToken(Items[Index])));
    }
    return Prefix + FString::Join(Parts, TEXT(";")) + TEXT("\n");
}
}
