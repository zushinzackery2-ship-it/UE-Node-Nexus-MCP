#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Core/NexusWarningCapture.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UeNodeNexusBridgeOperations.h"

namespace
{
void Emit(UeNodeNexusBridge::Warnings::FJournal& Journal, const TCHAR* Message,
          ELogVerbosity::Type Verbosity = ELogVerbosity::Warning, const TCHAR* Category = TEXT("LogNexusLogProbe"))
{
    Journal.Serialize(Message, Verbosity, FName(Category));
}
}

/**
 * Emits through the device interface rather than UE_LOG: a recorded warning is
 * still an engine warning, and the automation report must stay warning-free.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusLogJournalTest, "Nexus.Log.Journal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLogJournalTest::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::Warnings;
    TArray<FName> Watched;
    Watched.Add(FName(TEXT("LogNexusLogProbe")));
    FJournal Journal(Watched);

    Emit(Journal, TEXT("display verbosity is not a message"), ELogVerbosity::Display);
    Emit(Journal, TEXT("bone S_actor_body_01_lod0 is not in the skeleton"));
    Emit(Journal, TEXT("bone S_actor_body_01_lod0 is not in the skeleton"));
    Emit(Journal, TEXT("animation import failed"), ELogVerbosity::Error);

    bool bDropped = true;
    const TArray<TSharedPtr<FJsonValue>> Items = Journal.Window(0, bDropped);
    TestEqual(TEXT("a warning and an error are recorded once each"), Items.Num(), 2);
    TestFalse(TEXT("a complete window is not reported as dropped"), bDropped);
    if (Items.Num() == 2)
    {
        TestEqual(TEXT("warning severity is recorded"), Items[0]->AsObject()->GetStringField(TEXT("severity")), FString(TEXT("warning")));
        TestEqual(TEXT("error severity is recorded"), Items[1]->AsObject()->GetStringField(TEXT("severity")), FString(TEXT("error")));
        TestEqual(TEXT("category is preserved"), Items[1]->AsObject()->GetStringField(TEXT("category")), FString(TEXT("LogNexusLogProbe")));
    }
    const int64 Cursor = Journal.Cursor();
    TestEqual(TEXT("a window after the cursor is empty"), Journal.Window(Cursor, bDropped).Num(), 0);

    const int64 BeforeRepeat = Journal.Cursor();
    Emit(Journal, TEXT("animation import failed"), ELogVerbosity::Error);
    const TArray<TSharedPtr<FJsonValue>> Repeat = Journal.Window(BeforeRepeat, bDropped);
    TestEqual(TEXT("a repeated message reaches the next window"), Repeat.Num(), 1);
    TestEqual(TEXT("a repeated message is not duplicated in the ring"), Journal.Window(0, bDropped).Num(), 2);

    TArray<FName> Only;
    Only.Add(FName(TEXT("LogNexusLogProbe")));
    TestEqual(TEXT("severity filter keeps errors only"), Journal.Window(0, bDropped, FJournal::Capacity, TEXT("error")).Num(), 1);
    TestEqual(TEXT("category filter keeps the whole stream"), Journal.Window(0, bDropped, FJournal::Capacity, FString(), Only).Num(), 2);
    TestEqual(TEXT("text filter matches one message"), Journal.Window(0, bDropped, FJournal::Capacity, FString(), TArray<FName>(), TEXT("failed")).Num(), 1);
    Only[0] = FName(TEXT("LogNexusUnknown"));
    TestEqual(TEXT("category filter excludes what it does not name"), Journal.Window(0, bDropped, FJournal::Capacity, FString(), Only).Num(), 0);

    for (int32 Index = 0; Index < FJournal::Capacity + 4; ++Index)
    {
        Emit(Journal, *FString::Printf(TEXT("distinct message %d"), Index));
    }
    const TArray<TSharedPtr<FJsonValue>> All = Journal.Window(0, bDropped);
    TestEqual(TEXT("journal keeps its bounded window"), All.Num(), FJournal::Capacity);
    TestTrue(TEXT("a lost window is reported"), bDropped);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusLogEveryCategoryTest, "Nexus.Log.EveryCategory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLogEveryCategoryTest::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::Warnings;
    FJournal Journal;
    const int64 Start = Journal.Cursor();
    Emit(Journal, TEXT("nexus-log-probe-unlisted-category"), ELogVerbosity::Warning, TEXT("LogNexusUnlisted"));
    Emit(Journal, TEXT("nexus-log-probe-error-category"), ELogVerbosity::Error, TEXT("LogNexusUnlistedError"));

    bool bDropped = false;
    const TArray<TSharedPtr<FJsonValue>> Items =
        Journal.Window(Start, bDropped, FJournal::Capacity, FString(), TArray<FName>(), TEXT("nexus-log-probe-"));
    TestEqual(TEXT("categories the bridge never named are part of the stream"), Items.Num(), 2);
    if (Items.Num() == 2)
    {
        TestEqual(TEXT("unlisted warning keeps its category"), Items[0]->AsObject()->GetStringField(TEXT("category")), FString(TEXT("LogNexusUnlisted")));
        TestEqual(TEXT("unlisted error keeps its category"), Items[1]->AsObject()->GetStringField(TEXT("category")), FString(TEXT("LogNexusUnlistedError")));
        TestEqual(TEXT("unlisted error keeps its severity"), Items[1]->AsObject()->GetStringField(TEXT("severity")), FString(TEXT("error")));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusLogOperationTest, "Nexus.Log.Operation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLogOperationTest::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    auto payload = MakeShared<FJsonObject>();
    payload->SetStringField(TEXT("severity"), TEXT("error"));
    const TSharedPtr<FJsonObject> Response = HandleLogMessagesGet(TEXT("log_messages_get"), TEXT("log-test"), payload);
    TestTrue(TEXT("operation succeeds"), Response->GetBoolField(TEXT("ok")));
    const TSharedPtr<FJsonObject> Data = Response->GetObjectField(TEXT("data"));
    TestTrue(TEXT("cursor is reported"), Data->HasField(TEXT("cursor")));
    TestTrue(TEXT("items are reported"), Data->HasTypedField<EJson::Array>(TEXT("items")));
    TestTrue(TEXT("dropped is reported"), Data->HasField(TEXT("dropped")));

    auto invalid = MakeShared<FJsonObject>();
    invalid->SetStringField(TEXT("severity"), TEXT("fatal"));
    const TSharedPtr<FJsonObject> Rejected = HandleLogMessagesGet(TEXT("log_messages_get"), TEXT("log-test"), invalid);
    TestFalse(TEXT("an unknown severity is refused"), Rejected->GetBoolField(TEXT("ok")));
    TestEqual(TEXT("refusal names the request"),
              Rejected->GetObjectField(TEXT("error"))->GetStringField(TEXT("code")), FString(TEXT("invalid_request")));
    return true;
}
#endif
