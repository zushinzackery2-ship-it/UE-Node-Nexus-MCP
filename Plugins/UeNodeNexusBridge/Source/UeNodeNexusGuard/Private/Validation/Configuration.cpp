#include "Environment.h"
#include "ConfigurationLayers.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace NexusLifecycle::Validation
{
namespace
{
TMap<FString, FString> Files()
{
    return
    {
        {TEXT("Engine"), GEngineIni}, {TEXT("Game"), GGameIni},
        {TEXT("Input"), GInputIni}, {TEXT("Scalability"), GScalabilityIni},
        {TEXT("DeviceProfiles"), GDeviceProfilesIni}, {TEXT("Hardware"), GHardwareIni}
    };
}
}

FString Normalize(const FString& Value)
{
    FString Result = Value;
    const FString Full = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    Result.ReplaceInline(*Full, TEXT("{NEXUS_PROJECT}/"), ESearchCase::IgnoreCase);
    Result.ReplaceInline(*FPaths::ProjectDir(), TEXT("{NEXUS_PROJECT}/"), ESearchCase::IgnoreCase);
    FString Windows = Full;
    Windows.ReplaceInline(TEXT("/"), TEXT("\\"));
    Result.ReplaceInline(*Windows, TEXT("{NEXUS_PROJECT}/"), ESearchCase::IgnoreCase);
    return Result;
}

FString Expand(const FString& Value)
{
    return Value.Replace(TEXT("{NEXUS_PROJECT}/"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
}

TSharedPtr<FJsonObject> Configuration()
{
    auto Result = MakeShared<FJsonObject>();
    for (const auto& Pair : Files())
    {
        auto Sections = MakeShared<FJsonObject>();
        if (const FConfigFile* File = GConfig->FindConfigFile(Pair.Value))
        {
            const FConfigFile Effective = EffectiveConfigurationFile(*File);
            UE_LOG(LogTemp, Display, TEXT("Nexus effective configuration captured category=%s runtime_sections=%d effective_sections=%d"),
                *Pair.Key, File->Num(), Effective.Num());
            for (const auto& Section : Effective)
            {
                TArray<TSharedPtr<FJsonValue>> Values;
                for (const auto& Entry : Section.Value)
                {
                    auto Row = MakeShared<FJsonObject>();
                    Row->SetStringField(TEXT("key"), Entry.Key.ToString());
                    Row->SetStringField(TEXT("value"), Normalize(Entry.Value.GetValue()));
                    Values.Add(MakeShared<FJsonValueObject>(Row));
                }
                Sections->SetArrayField(Section.Key, Values);
            }
        }
        Result->SetObjectField(Pair.Key, Sections);
    }
    return Result;
}

bool ValidateConfiguration(const TSharedPtr<FJsonObject>& Input, FString& Error)
{
    if (!Input.IsValid() || Input->Values.Num() != Files().Num())
    {
        Error = TEXT("effective configuration requires all runtime ini categories");
        return false;
    }
    for (const auto& Pair : Files())
    {
        const TSharedPtr<FJsonObject>* Sections = nullptr;
        if (!Input->TryGetObjectField(Pair.Key, Sections) || !GConfig->FindConfigFile(Pair.Value))
        {
            Error = TEXT("runtime ini category is unavailable: ") + Pair.Key;
            return false;
        }
        for (const auto& Section : (*Sections)->Values)
        {
            const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
            if (!Section.Value->TryGetArray(Entries))
            {
                Error = TEXT("configuration section requires ordered entries");
                return false;
            }
            for (const auto& Entry : *Entries)
            {
                const TSharedPtr<FJsonObject>* Row = nullptr;
                FString Key, Value;
                if (!Entry->TryGetObject(Row) || !(*Row)->TryGetStringField(TEXT("key"), Key)
                    || Key.IsEmpty() || !(*Row)->TryGetStringField(TEXT("value"), Value))
                {
                    Error = TEXT("configuration entry requires key and value strings");
                    return false;
                }
            }
        }
    }
    return true;
}

void ApplyConfiguration(const TSharedPtr<FJsonObject>& Input)
{
    for (const auto& Pair : Files())
    {
        FConfigFile* File = GConfig->FindConfigFile(Pair.Value);
        File->Empty();
        for (const auto& Section : Input->GetObjectField(Pair.Key)->Values)
        {
            FConfigSection Output;
            for (const auto& Entry : Section.Value->AsArray())
            {
                const auto Row = Entry->AsObject();
                Output.Add(FName(*Row->GetStringField(TEXT("key"))), FConfigValue(Expand(Row->GetStringField(TEXT("value")))));
            }
            File->Add(Section.Key, MoveTemp(Output));
        }
    }
}
}
