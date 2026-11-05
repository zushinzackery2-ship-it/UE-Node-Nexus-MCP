#include "NexusInstanceValues.h"
#include <cstdlib>
#include "Misc/DefaultValueHelper.h"

namespace UeNodeNexusBridge::Transcode
{
bool InstanceParameterInfo(const TSharedPtr<FJsonObject>& Op, FMaterialParameterInfo& Out, FString& Error)
{
    FString Name;
    FString Association = TEXT("global");
    double Index = INDEX_NONE;
    const bool bValid = Op->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty()
        && (!Op->HasField(TEXT("association")) || Op->TryGetStringField(TEXT("association"), Association))
        && (!Op->HasField(TEXT("index")) || Op->TryGetNumberField(TEXT("index"), Index));
    if (!bValid || !FMath::IsFinite(Index) || FMath::FloorToDouble(Index) != Index || Index > MAX_int32
        || (Association == TEXT("global") ? Index != double(INDEX_NONE)
            : (Association != TEXT("layer") && Association != TEXT("blend")) || Index < 0))
    {
        Error = TEXT("parameter identity requires a name and global/-1 or layer|blend/nonnegative index");
        return false;
    }
    Out = FMaterialParameterInfo(FName(*Name), Association == TEXT("global") ? GlobalParameter
        : Association == TEXT("layer") ? LayerParameter : BlendParameter, int32(Index));
    return true;
}

bool InstanceFloat(const FString& Text, float& Out)
{
    const FString Trimmed = Text.TrimStartAndEnd();
    const FTCHARToUTF8 Utf8(*Trimmed);
    char* End = nullptr;
    const double Value = std::strtod(Utf8.Get(), &End);
    Out = float(Value);
    return End && End != Utf8.Get() && *End == 0 && FMath::IsFinite(Value) && FMath::IsFinite(Out)
        && FDefaultValueHelper::IsStringValidFloat(Trimmed);
}

bool InstanceBool(const FString& Text, bool& Out)
{
    const FString Value = Text.TrimStartAndEnd().ToLower();
    Out = Value == TEXT("true") || Value == TEXT("1");
    return Out || Value == TEXT("false") || Value == TEXT("0");
}

bool InstanceChannels(const FString& Text, TArray<FString>& Out)
{
    FString Body = Text.TrimStartAndEnd();
    if (!Body.RemoveFromStart(TEXT("(")) || !Body.RemoveFromEnd(TEXT(")")))
    {
        return false;
    }
    TArray<FString> Parts;
    Body.ParseIntoArray(Parts, TEXT(","), false);
    if (Parts.Num() != 4)
    {
        return false;
    }
    Out.SetNum(4);
    TSet<int32> Seen;
    for (const FString& Part : Parts)
    {
        FString Key, Value;
        if (!Part.Split(TEXT("="), &Key, &Value))
        {
            return false;
        }
        Key = Key.TrimStartAndEnd().ToUpper();
        const int32 Index = Key.Len() == 1 ? FString(TEXT("RGBA")).Find(Key) : INDEX_NONE;
        if (Index == INDEX_NONE || Seen.Contains(Index))
        {
            return false;
        }
        Seen.Add(Index);
        Out[Index] = Value.TrimStartAndEnd();
    }
    return true;
}
}
