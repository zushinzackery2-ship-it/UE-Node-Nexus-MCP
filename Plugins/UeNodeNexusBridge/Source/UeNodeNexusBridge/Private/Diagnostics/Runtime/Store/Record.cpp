#include "State.h"

#include "Misc/ScopeLock.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge::RuntimeDiagnostics
{
void Record(const TSharedPtr<FJsonObject>& Event)
{
    auto& Data = Store::State();
    FScopeLock Lock(&Data.Mutex);
    if (Data.Sessions.IsEmpty())
    {
        return;
    }
    auto& Session = Data.Sessions.Last();
    auto Item = MakeShared<FJsonObject>(*Event);
    bool bTruncated = false;
    for (auto& Pair : Item->Values)
    {
        FString Text;
        if (Pair.Value->TryGetString(Text) && Text.Len() > 4096)
        {
            Pair.Value = MakeShared<FJsonValueString>(Text.Left(4096));
            bTruncated = true;
        }
    }
    Item->SetBoolField(TEXT("text_truncated"), bTruncated);
    const FString Severity = Item->GetStringField(TEXT("severity"));
    const bool bError = Severity == TEXT("error") || Severity == TEXT("fatal");
    Session.Errors += bError ? 1 : 0;
    Session.Warnings += Severity == TEXT("warning") ? 1 : 0;
    FString Asset;
    Item->TryGetStringField(TEXT("asset_path"), Asset);
    const FString AssetKey = Store::Digest(Asset.ToLower());
    if (Asset.IsEmpty() || Session.Assets.Contains(AssetKey) || Session.Assets.Num() < 128)
    {
        auto& Counts = Asset.IsEmpty() ? Session.Unattributed : Session.Assets.FindOrAdd(AssetKey);
        Counts.Errors += bError ? 1 : 0;
        Counts.Warnings += Severity == TEXT("warning") ? 1 : 0;
    }
    else
    {
        Session.bCounterOverflow = true;
    }
    Item->SetStringField(TEXT("session_id"), Session.Id);
    const FString Key = Store::Digest(SerializeJsonObjectToString(Item));
    const FString Now = FDateTime::UtcNow().ToIso8601();
    ++Data.Sequence;
    if (auto* Existing = Data.Aggregates.Find(Key))
    {
        (*Existing)->SetNumberField(TEXT("occurrence_count"), (*Existing)->GetNumberField(TEXT("occurrence_count")) + 1);
        (*Existing)->SetNumberField(TEXT("sequence"), Data.Sequence);
        (*Existing)->SetStringField(TEXT("last_at"), Now);
        Store::UpdateSamples(Session, *Existing);
        return;
    }
    Item->SetStringField(TEXT("id"), Key);
    Item->SetStringField(TEXT("first_at"), Now);
    Item->SetStringField(TEXT("last_at"), Now);
    Item->SetNumberField(TEXT("occurrence_count"), 1);
    Item->SetNumberField(TEXT("sequence"), Data.Sequence);
    const int32 Size = SerializeJsonObjectToString(Item).Len() * sizeof(TCHAR) + 256;
    while (!Data.Entries.IsEmpty() && (Data.Entries.Num() >= Store::MaxEntries || Data.Bytes + Size > Store::MaxBytes))
    {
        Store::Evict(Data);
    }
    if (Size > Store::MaxBytes)
    {
        ++Session.Dropped;
        Session.DroppedThrough = Data.Sequence;
        return;
    }
    Store::FEntry Entry;
    Entry.Key = Key;
    Entry.Value = Item;
    Entry.Bytes = Size;
    Data.Entries.Add(MoveTemp(Entry));
    Data.Aggregates.Add(Key, Item);
    Data.Bytes += Size;
    Store::UpdateSamples(Session, Item);
}
}
