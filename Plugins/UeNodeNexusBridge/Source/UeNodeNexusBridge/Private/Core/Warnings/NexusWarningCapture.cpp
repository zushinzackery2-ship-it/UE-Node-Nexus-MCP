#include "NexusWarningCapture.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UeNodeNexusBridge
{
namespace Warnings
{
namespace
{
FString Truncated(FString Text)
{
    Text.TrimStartAndEndInline();
    if (Text.Len() > FJournal::MessageChars)
    {
        Text.LeftInline(FJournal::MessageChars);
    }
    return Text;
}

FString SeverityOf(ELogVerbosity::Type Verbosity)
{
    return (Verbosity & ELogVerbosity::VerbosityMask) == ELogVerbosity::Error ? TEXT("error") : TEXT("warning");
}

TUniquePtr<FJournal> GJournal;
}

FJournal::FJournal()
{
    if (GLog != nullptr)
    {
        GLog->AddOutputDevice(this);
    }
}

FJournal::FJournal(const TArray<FName>& InCategories) : Categories(InCategories)
{
    if (GLog != nullptr)
    {
        GLog->AddOutputDevice(this);
    }
}

FJournal::~FJournal()
{
    if (GLog != nullptr)
    {
        GLog->RemoveOutputDevice(this);
    }
}

void FJournal::Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category)
{
    const ELogVerbosity::Type Level = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
    if (Message == nullptr || (Level != ELogVerbosity::Warning && Level != ELogVerbosity::Error))
    {
        return;
    }
    if (!Categories.IsEmpty() && !Categories.Contains(Category))
    {
        return;
    }
    FString Text = Truncated(FString(Message));
    if (Text.IsEmpty())
    {
        return;
    }
    const FString Severity = SeverityOf(Verbosity);
    const FString Key = Severity + TEXT("|") + Category.ToString() + TEXT("|") + Text;
    FScopeLock Scope(&Lock);
    if (Seen.Contains(Key))
    {
        // One entry per distinct message, ordered by its last occurrence: a
        // second import that drops the same tracks must reach the next window.
        const int32 Index = Entries.IndexOfByPredicate([&Key](const FEntry& Entry) { return Entry.Key == Key; });
        if (Index != INDEX_NONE)
        {
            FEntry Moved = Entries[Index];
            Entries.RemoveAt(Index);
            Moved.Sequence = NextSequence++;
            Entries.Add(MoveTemp(Moved));
        }
        return;
    }
    if (Entries.Num() >= Capacity)
    {
        Seen.Remove(Entries[0].Key);
        DiscardedThrough = Entries[0].Sequence;
        Entries.RemoveAt(0);
    }
    Seen.Add(Key);
    Entries.Add(FEntry{NextSequence++, Severity, Category.ToString(), MoveTemp(Text), Key});
}

int64 FJournal::Cursor() const
{
    FScopeLock Scope(&Lock);
    return NextSequence - 1;
}

TArray<TSharedPtr<FJsonValue>> FJournal::Window(int64 Since, bool& bOutDropped, int32 Limit,
                                                const FString& Severity, const TArray<FName>& Only,
                                                const FString& Match) const
{
    TArray<TSharedPtr<FJsonValue>> Result;
    FScopeLock Scope(&Lock);
    bOutDropped = Since < DiscardedThrough;
    for (int32 Index = 0; Index < Entries.Num() && Result.Num() < Limit; ++Index)
    {
        const FEntry& Entry = Entries[Index];
        if (Entry.Sequence <= Since)
        {
            continue;
        }
        if (!Severity.IsEmpty() && !Entry.Severity.Equals(Severity, ESearchCase::IgnoreCase))
        {
            continue;
        }
        if (!Only.IsEmpty() && !Only.Contains(FName(Entry.Category)))
        {
            continue;
        }
        if (!Match.IsEmpty() && !Entry.Message.Contains(Match, ESearchCase::IgnoreCase))
        {
            continue;
        }
        auto Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("sequence"), static_cast<double>(Entry.Sequence));
        Row->SetStringField(TEXT("severity"), Entry.Severity);
        Row->SetStringField(TEXT("category"), Entry.Category);
        Row->SetStringField(TEXT("message"), Entry.Message);
        Result.Add(MakeShared<FJsonValueObject>(Row));
    }
    return Result;
}

void AppendWindow(const TSharedPtr<FJsonObject>& Response, int64 Start)
{
    if (!Response.IsValid())
    {
        return;
    }
    bool bDropped = false;
    const TArray<TSharedPtr<FJsonValue>> Items = Journal().Window(Start, bDropped);
    if (Items.IsEmpty() && !bDropped)
    {
        return;
    }
    const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
    TArray<TSharedPtr<FJsonValue>> Merged;
    if (Response->TryGetArrayField(TEXT("warnings"), Existing) && Existing != nullptr)
    {
        Merged = *Existing;
    }
    Merged.Append(Items);
    Response->SetArrayField(TEXT("warnings"), Merged);
}

void Startup()
{
    GJournal = MakeUnique<FJournal>();
}

void Shutdown()
{
    GJournal.Reset();
}

FJournal& Journal()
{
    check(GJournal.IsValid());
    return *GJournal;
}
}
}
