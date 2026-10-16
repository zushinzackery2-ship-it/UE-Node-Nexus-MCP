#include "NexusCustomEventPins.h"

#include "NexusBlueprintSignaturePins.h"
#include "K2Node_CustomEvent.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"

namespace UeNodeNexusBridge::Transcode
{
static bool LegacyScalarParams(const TSharedPtr<FJsonObject>& Operation,
    TArray<TSharedPtr<FJsonValue>>& Params, FString& Error)
{
    const TArray<FString> Positional = ReadOpStrings(Operation, TEXT("positional"));
    const TSet<FString> Categories = {TEXT("bool"), TEXT("byte"), TEXT("int"), TEXT("int64"),
        TEXT("name"), TEXT("string"), TEXT("text")};
    for (int32 Index = 1; Index < Positional.Num(); ++Index)
    {
        FString Name;
        FString Category;
        if (!Positional[Index].Split(TEXT(":"), &Name, &Category)
            || !Categories.Contains(Category.TrimStartAndEnd().ToLower()))
        {
            Error = TEXT("custom event parameters require a structured native signature: ") + Positional[Index];
            return false;
        }
        auto Type = MakeShared<FJsonObject>();
        Type->SetStringField(TEXT("category"), Category.TrimStartAndEnd().ToLower());
        auto Param = MakeShared<FJsonObject>();
        Param->SetStringField(TEXT("name"), Name.TrimStartAndEnd());
        Param->SetObjectField(TEXT("type"), Type);
        Params.Add(MakeShared<FJsonValueObject>(Param));
    }
    return true;
}

bool ConfigureCustomEventPins(UK2Node_CustomEvent* Event, const TSharedPtr<FJsonObject>& Operation,
    FString& Error)
{
    const TSharedPtr<FJsonObject>* Signature = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
    TArray<TSharedPtr<FJsonValue>> Legacy;
    if (Operation->TryGetObjectField(TEXT("custom_event_signature"), Signature))
    {
        if (!(*Signature)->TryGetArrayField(TEXT("inputs"), Inputs))
        {
            Error = TEXT("custom event signature requires its input parameter array");
            return false;
        }
    }
    else
    {
        // Existing primitive-only requests remain valid; reflected type text is
        // rejected before it can become an unsupported native pin category.
        if (!LegacyScalarParams(Operation, Legacy, Error))
        {
            return false;
        }
        Inputs = &Legacy;
    }
    return SetSignaturePins(Event, Inputs, EGPD_Output, false, Error);
}
}
