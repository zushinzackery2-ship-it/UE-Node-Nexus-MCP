#include "State.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"

namespace UeNodeNexusBridge::RuntimeDiagnostics::Store
{
FState::FState()
{
    FParse::Value(FCommandLine::Get(), TEXT("NexusInstance="), Instance);
}

FState& State()
{
    static FState Data;
    return Data;
}

FString Digest(const FString& Text)
{
    const FTCHARToUTF8 Encoded(*Text);
    return FMD5::HashBytes(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
}

FSession* FindSession(FState& Data, const FString& Id)
{
    return Data.Sessions.FindByPredicate([&](const FSession& Session)
    {
        return Session.Id == Id;
    });
}

const FSession* DefaultSession(const FState& Data)
{
    for (int32 Index = Data.Sessions.Num() - 1; Index >= 0; --Index)
    {
        if (Data.Sessions[Index].bPie)
        {
            return &Data.Sessions[Index];
        }
    }
    return Data.Sessions.IsEmpty() ? nullptr : &Data.Sessions.Last();
}

void Evict(FState& Data)
{
    const auto Entry = Data.Entries[0];
    const auto Value = Entry.Value;
    auto* Session = FindSession(Data, Value->GetStringField(TEXT("session_id")));
    if (Session)
    {
        Session->Dropped += Value->GetIntegerField(TEXT("occurrence_count"));
        Session->DroppedThrough = FMath::Max(Session->DroppedThrough,
            static_cast<int64>(Value->GetNumberField(TEXT("sequence"))));
    }
    Data.Aggregates.Remove(Entry.Key);
    Data.Bytes -= Entry.Bytes;
    Data.Entries.RemoveAt(0);
    if (Session && Session->Samples.Contains(Value))
    {
        RebuildSamples(Data, *Session);
    }
}
}
