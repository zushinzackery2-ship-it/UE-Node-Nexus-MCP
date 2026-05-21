#include "UeNodeNexusBridgeGraphIndexedInfoOps.h"

#include "Dom/JsonObject.h"

namespace UeNodeNexusBridge
{
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

bool WantsRealIds(const TSharedPtr<FJsonObject>& Payload)
{
    FString IdMode = TEXT("alias");
    Payload->TryGetStringField(TEXT("id_mode"), IdMode);
    return IdMode.Equals(TEXT("real"), ESearchCase::IgnoreCase) || IdMode.Equals(TEXT("both"), ESearchCase::IgnoreCase);
}

int32 ReadIndexedMaxNodes(const TSharedPtr<FJsonObject>& Payload)
{
    double MaxNodes = 0.0;
    return Payload->TryGetNumberField(TEXT("max_nodes"), MaxNodes) ? FMath::Max(0, static_cast<int32>(MaxNodes)) : 0;
}
}
