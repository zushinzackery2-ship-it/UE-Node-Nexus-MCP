#include "State.h"

#include "Misc/ScopeLock.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge::RuntimeDiagnostics
{
TSharedPtr<FJsonObject> Read(const TSharedPtr<FJsonObject>& Payload)
{
    auto& Data = Store::State();
    FScopeLock Lock(&Data.Mutex);
    FString Id, Asset, Severity = TEXT("all");
    Payload->TryGetStringField(TEXT("session_id"), Id);
    Payload->TryGetStringField(TEXT("asset_path"), Asset);
    Payload->TryGetStringField(TEXT("severity"), Severity);
    double Cursor = 0;
    Payload->TryGetNumberField(TEXT("cursor"), Cursor);
    const int32 Limit = ReadLimit(Payload, 200, Store::MaxEntries);
    if (Id.IsEmpty() && Store::DefaultSession(Data))
    {
        Id = Store::DefaultSession(Data)->Id;
    }
    const auto* Session = Store::FindSession(Data, Id);
    auto Result = Session ? Store::SessionJson(Data, *Session) : MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("available"), Session != nullptr);
    Result->SetBoolField(TEXT("asset_counts_complete"), Session && !Session->bCounterOverflow);
    Result->SetNumberField(TEXT("unattributed_error_count"), Session ? Session->Unattributed.Errors : 0);
    Store::FCounts Counts;
    if (Session)
    {
        if (Asset.IsEmpty())
        {
            Counts.Errors = Session->Errors;
            Counts.Warnings = Session->Warnings;
        }
        else if (const auto* Matched = Session->Assets.Find(Store::Digest(Asset.ToLower())))
        {
            Counts = *Matched;
        }
    }
    Result->SetNumberField(TEXT("matched_error_count"), Counts.Errors);
    Result->SetNumberField(TEXT("matched_warning_count"), Counts.Warnings);
    Result->SetBoolField(TEXT("cursor_gap"), Session && Session->Dropped > 0 && Cursor < Session->DroppedThrough);
    Result->SetNumberField(TEXT("retained_bytes"), Data.Bytes);
    TArray<TSharedPtr<FJsonObject>> Matches;
    for (const auto& Entry : Data.Entries)
    {
        const auto Value = Entry.Value;
        FString Path;
        Value->TryGetStringField(TEXT("asset_path"), Path);
        const FString Level = Value->GetStringField(TEXT("severity"));
        if (Value->GetStringField(TEXT("session_id")) == Id && Value->GetNumberField(TEXT("sequence")) > Cursor
            && (Asset.IsEmpty() || Path.Equals(Asset, ESearchCase::IgnoreCase))
            && (Severity == TEXT("all") || Severity == Level || (Severity == TEXT("error") && Level == TEXT("fatal"))))
        {
            Matches.Add(Value);
        }
    }
    Matches.Sort([](const auto& Left, const auto& Right)
    {
        return Left->GetNumberField(TEXT("sequence")) < Right->GetNumberField(TEXT("sequence"));
    });
    TArray<TSharedPtr<FJsonValue>> Items;
    for (int32 Index = 0; Index < FMath::Min(Limit, Matches.Num()); ++Index)
    {
        Items.Add(MakeShared<FJsonValueObject>(MakeShared<FJsonObject>(*Matches[Index])));
    }
    if (Matches.Num() > Limit)
    {
        Result->SetNumberField(TEXT("next_cursor"), Matches[Limit - 1]->GetNumberField(TEXT("sequence")));
    }
    Result->SetBoolField(TEXT("truncated"), Matches.Num() > Limit);
    Result->SetArrayField(TEXT("items"), Items);
    return Result;
}
}
