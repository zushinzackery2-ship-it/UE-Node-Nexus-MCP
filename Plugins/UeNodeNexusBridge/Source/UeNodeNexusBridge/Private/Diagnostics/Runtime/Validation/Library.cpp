#include "Diagnostics/NexusRuntimeDiagnosticsLibrary.h"

#include "../NexusRuntimeDiagnostics.h"
#include "Verdict.h"
#include "UeNodeNexusBridgeJson.h"

namespace
{
TSharedPtr<FJsonObject> ReadSession(const FString& Id)
{
    auto Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("session_id"), Id);
    Payload->SetStringField(TEXT("severity"), TEXT("error"));
    Payload->SetNumberField(TEXT("limit"), 3);
    return UeNodeNexusBridge::RuntimeDiagnostics::Read(Payload);
}
}

FString UNexusRuntimeDiagnosticsLibrary::RuntimeDiagnosticsJson(const FString& SessionId)
{
    return UeNodeNexusBridge::SerializeJsonObjectToString(ReadSession(SessionId));
}

FString UNexusRuntimeDiagnosticsLibrary::RuntimeVerificationJson(const FString& SessionId, const FString& InstanceId, bool bFunctionalPassed)
{
    auto Runtime = ReadSession(SessionId.IsEmpty() ? TEXT("unobserved") : SessionId);
    auto Verdict = UeNodeNexusBridge::RuntimeSmoke::Evaluate(Runtime, SessionId);
    FString ObservedInstance;
    Runtime->TryGetStringField(TEXT("instance_id"), ObservedInstance);
    if (InstanceId.IsEmpty() || InstanceId != ObservedInstance)
    {
        Verdict->SetBoolField(TEXT("passed"), false);
        Verdict->SetStringField(TEXT("status"), TEXT("inconclusive"));
        Verdict->SetStringField(TEXT("code"), TEXT("runtime_session_mismatch"));
    }
    else if (!bFunctionalPassed && Verdict->GetStringField(TEXT("status")) != TEXT("failed"))
    {
        Verdict->SetBoolField(TEXT("passed"), false);
        Verdict->SetStringField(TEXT("status"), TEXT("failed"));
        Verdict->SetStringField(TEXT("code"), TEXT("functional_checks_failed"));
    }
    auto Result = MakeShared<FJsonObject>();
    Result->SetObjectField(TEXT("runtime"), Runtime);
    Result->SetObjectField(TEXT("runtime_verification"), Verdict);
    Result->SetBoolField(TEXT("passed"), Verdict->GetBoolField(TEXT("passed")));
    Result->SetBoolField(TEXT("functional_passed"), bFunctionalPassed);
    return UeNodeNexusBridge::SerializeJsonObjectToString(Result);
}
