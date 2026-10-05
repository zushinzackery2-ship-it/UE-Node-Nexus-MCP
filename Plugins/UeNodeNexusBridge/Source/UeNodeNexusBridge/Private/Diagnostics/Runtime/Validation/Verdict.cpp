#include "Verdict.h"

namespace UeNodeNexusBridge::RuntimeSmoke
{
TSharedPtr<FJsonObject> Evaluate(const TSharedPtr<FJsonObject>& Runtime, const FString& ExpectedSession)
{
    auto Result = MakeShared<FJsonObject>();
    FString Session;
    Runtime->TryGetStringField(TEXT("session_id"), Session);
    Result->SetStringField(TEXT("session_id"), Session);
    FString Status = TEXT("inconclusive"), Code = TEXT("runtime_session_mismatch");
    double Errors = 0, Dropped = -1;
    const bool bKnownErrors = Runtime->TryGetNumberField(TEXT("error_count"), Errors) && FMath::IsFinite(Errors) && Errors >= 0;
    Runtime->TryGetNumberField(TEXT("dropped_count"), Dropped);
    bool bAvailable = false, bPie = false, bActive = true, bSources = false, bCounters = false, bGap = false;
    Runtime->TryGetBoolField(TEXT("available"), bAvailable);
    Runtime->TryGetBoolField(TEXT("pie"), bPie);
    Runtime->TryGetBoolField(TEXT("active"), bActive);
    Runtime->TryGetBoolField(TEXT("sources_complete"), bSources);
    Runtime->TryGetBoolField(TEXT("asset_counts_complete"), bCounters);
    Runtime->TryGetBoolField(TEXT("cursor_gap"), bGap);
    if (!ExpectedSession.IsEmpty() && Session == ExpectedSession)
    {
        Code = TEXT("runtime_observation_incomplete");
        if (Errors > 0)
        {
            Status = TEXT("failed");
            Code = TEXT("runtime_diagnostics_failed");
        }
        else if (bKnownErrors && bAvailable && bPie && !bActive && bSources && bCounters && Dropped == 0 && !bGap)
        {
            Status = TEXT("passed");
            Code = TEXT("runtime_diagnostics_clean");
        }
    }
    Result->SetStringField(TEXT("status"), Status);
    Result->SetStringField(TEXT("code"), Code);
    Result->SetBoolField(TEXT("passed"), Status == TEXT("passed"));
    Result->SetNumberField(TEXT("error_count"), Errors);
    return Result;
}
}
