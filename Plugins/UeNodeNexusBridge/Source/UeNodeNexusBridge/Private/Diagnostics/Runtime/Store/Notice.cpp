#include "State.h"

#include "Misc/ScopeLock.h"

namespace UeNodeNexusBridge::RuntimeDiagnostics::Store
{
TSharedPtr<FJsonObject> SessionJson(const FState& Data, const FSession& Session)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("session_id"), Session.Id);
    Result->SetStringField(TEXT("scope"), Session.bPie ? TEXT("pie_session") : TEXT("runtime_session"));
    Result->SetStringField(TEXT("sampled_at"), FDateTime::UtcNow().ToIso8601());
    Result->SetBoolField(TEXT("pie"), Session.bPie);
    Result->SetBoolField(TEXT("active"), Session.bActive);
    Result->SetBoolField(TEXT("available"), true);
    Result->SetBoolField(TEXT("live_state"), Session.bActive);
    Result->SetBoolField(TEXT("runtime_observed"), true);
    Result->SetBoolField(TEXT("sources_complete"), Session.bSourcesComplete);
    Result->SetNumberField(TEXT("error_count"), Session.Errors);
    Result->SetNumberField(TEXT("warning_count"), Session.Warnings);
    Result->SetNumberField(TEXT("dropped_count"), Session.Dropped);
    Result->SetNumberField(TEXT("next_cursor"), Data.Sequence);
    Result->SetNumberField(TEXT("unattributed_error_count"), Session.Unattributed.Errors);
    Result->SetBoolField(TEXT("asset_counts_complete"), !Session.bCounterOverflow);
    Result->SetStringField(TEXT("instance_id"), Data.Instance);
    Result->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
    const bool bClean = Session.bPie && !Session.bActive && Session.bSourcesComplete
        && Session.Dropped == 0 && !Session.bCounterOverflow;
    Result->SetStringField(TEXT("status"), Session.Errors > 0 ? TEXT("failed") : bClean ? TEXT("clean") : TEXT("unknown"));
    Result->SetArrayField(TEXT("samples"), SampleJson(Session));
    return Result;
}
}

namespace UeNodeNexusBridge::RuntimeDiagnostics
{
TSharedPtr<FJsonObject> Snapshot()
{
    auto& Data = Store::State();
    FScopeLock Lock(&Data.Mutex);
    const auto* Session = Store::DefaultSession(Data);
    auto Result = Session ? Store::SessionJson(Data, *Session) : MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("available"), Session != nullptr);
    if (!Data.Sessions.IsEmpty())
    {
        const auto& Current = Data.Sessions.Last();
        Result->SetStringField(TEXT("current_session_id"), Current.Id);
        if (!Current.bPie)
        {
            Result->SetNumberField(TEXT("editor_error_count"), Current.Errors);
            Result->SetArrayField(TEXT("editor_samples"), Store::SampleJson(Current));
        }
    }
    return Result;
}
}
