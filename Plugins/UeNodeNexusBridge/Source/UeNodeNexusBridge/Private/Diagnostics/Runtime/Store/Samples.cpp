#include "State.h"

namespace UeNodeNexusBridge::RuntimeDiagnostics::Store
{
void UpdateSamples(FSession& Session, const TSharedPtr<FJsonObject>& Event)
{
    const FString Severity = Event->GetStringField(TEXT("severity"));
    if (Severity != TEXT("error") && Severity != TEXT("fatal"))
    {
        return;
    }
    Session.Samples.AddUnique(Event);
    Session.Samples.Sort([](const auto& Left, const auto& Right)
    {
        const bool bLeftFatal = Left->GetStringField(TEXT("severity")) == TEXT("fatal");
        const bool bRightFatal = Right->GetStringField(TEXT("severity")) == TEXT("fatal");
        if (bLeftFatal != bRightFatal)
        {
            return bLeftFatal;
        }
        return Left->GetNumberField(TEXT("sequence")) > Right->GetNumberField(TEXT("sequence"));
    });
    if (Session.Samples.Num() > MaxSamples)
    {
        Session.Samples.SetNum(MaxSamples, EAllowShrinking::No);
    }
}

void RebuildSamples(const FState& Data, FSession& Session)
{
    Session.Samples.Reset();
    for (const auto& Entry : Data.Entries)
    {
        if (Entry.Value->GetStringField(TEXT("session_id")) == Session.Id)
        {
            UpdateSamples(Session, Entry.Value);
        }
    }
}

TArray<TSharedPtr<FJsonValue>> SampleJson(const FSession& Session)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const auto& Event : Session.Samples)
    {
        auto Sample = MakeShared<FJsonObject>();
        const TArray<FString> Names
        {
            TEXT("id"), TEXT("session_id"), TEXT("severity"), TEXT("source"),
            TEXT("code"), TEXT("asset_path"), TEXT("message"), TEXT("graph"), TEXT("function"),
            TEXT("node_guid"), TEXT("node_title"), TEXT("first_at"), TEXT("last_at")
        };
        bool bTruncated = false;
        for (const FString& Name : Names)
        {
            FString Text;
            if (Event->TryGetStringField(Name, Text))
            {
                const int32 Limit = Name == TEXT("message") ? 512 : 160;
                Sample->SetStringField(Name, Text.Left(Limit));
                bTruncated |= Text.Len() > Limit;
            }
        }
        Sample->SetNumberField(TEXT("occurrence_count"), Event->GetNumberField(TEXT("occurrence_count")));
        Sample->SetNumberField(TEXT("sequence"), Event->GetNumberField(TEXT("sequence")));
        Sample->SetBoolField(TEXT("sample_truncated"), bTruncated);
        Result.Add(MakeShared<FJsonValueObject>(Sample));
    }
    return Result;
}
}
