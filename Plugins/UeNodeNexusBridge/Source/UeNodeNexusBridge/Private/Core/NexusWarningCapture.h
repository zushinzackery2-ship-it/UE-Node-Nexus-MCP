#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "HAL/CriticalSection.h"
#include "Misc/OutputDevice.h"

class FJsonObject;
class FJsonValue;

namespace UeNodeNexusBridge
{
namespace Warnings
{
/**
 * Bounded journal of the warnings and errors the editor writes into the UE log.
 *
 * This is the log stream itself, not a list of channels: every category is
 * recorded, including the ones MessageLog mirrors under a listing name (for
 * example ``FBXImport``), so a caller can read what the engine reported without
 * the bridge deciding which subsystems matter.
 *
 * The optional category list exists only to bind one journal to one probe in
 * tests; the module journal records every category.
 */
class FJournal final : public FOutputDevice
{
public:
    static constexpr int32 Capacity = 256;
    static constexpr int32 MessageChars = 512;

    FJournal();
    explicit FJournal(const TArray<FName>& InCategories);
    virtual ~FJournal() override;

    virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override;

    /** Sequence of the last recorded entry; zero before the first message. */
    int64 Cursor() const;

    /** Entries after ``Since``, optionally narrowed by severity, category or text. */
    TArray<TSharedPtr<FJsonValue>> Window(int64 Since, bool& bOutDropped, int32 Limit = Capacity,
                                          const FString& Severity = FString(),
                                          const TArray<FName>& Only = TArray<FName>(),
                                          const FString& Match = FString()) const;

private:
    struct FEntry
    {
        int64 Sequence = 0;
        FString Severity;
        FString Category;
        FString Message;
        FString Key;
    };

    mutable FCriticalSection Lock;
    TArray<FName> Categories;
    TArray<FEntry> Entries;
    TSet<FString> Seen;
    int64 NextSequence = 1;
    int64 DiscardedThrough = 0;
};

/** Install or remove the editor-wide journal. */
void Startup();
void Shutdown();

/** The editor-wide journal; valid between Startup and Shutdown. */
FJournal& Journal();

/** Attach the warnings and errors this request emitted to ``Response``. */
void AppendWindow(const TSharedPtr<FJsonObject>& Response, int64 Start);
}
}
