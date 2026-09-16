#include "State.h"
#include "Json.h"
#include "../Identity/Identity.h"

#include "Misc/ScopeLock.h"

namespace NexusLifecycle
{
FGuardState& State()
{
    static FGuardState Value;
    return Value;
}

TSharedPtr<FJsonObject> Success(const TSharedPtr<FJsonObject>& Data)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetObjectField(TEXT("data"), Data);
    return Result;
}

TSharedPtr<FJsonObject> Failure(const FString& Code, const FString& Message)
{
    auto Error = MakeShared<FJsonObject>();
    Error->SetStringField(TEXT("code"), Code);
    Error->SetStringField(TEXT("message"), Message);
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), false);
    Result->SetObjectField(TEXT("error"), Error);
    return Result;
}

TSharedPtr<FJsonObject> ScopeStatus(const FString& ScopeId)
{
    auto Result = MakeShared<FJsonObject>();
    int32 Pending = 0, Inflight = 0;
    for (const auto& Pair : State().Requests)
    {
        if ((ScopeId.IsEmpty() || Pair.Value.ScopeId == ScopeId) && !Pair.Value.bFinished)
        {
            Pair.Value.bRunning ? ++Inflight : ++Pending;
        }
    }
    Result->SetNumberField(TEXT("pending"), Pending);
    Result->SetNumberField(TEXT("inflight"), Inflight);
    for (int32 Index = State().Finished.Num() - 1; Index >= 0; --Index)
    {
        const FRequest& Request = State().Requests[State().Finished[Index]];
        if (Request.ScopeId == ScopeId)
        {
            Result->SetStringField(TEXT("outcome"), Request.Outcome);
            break;
        }
    }
    return Result;
}

TSharedPtr<FJsonObject> StatusLocked()
{
    auto& S = State();
    auto Result = ScopeStatus(FString());
    if (S.Identity.IsValid())
    {
        Result->Values.Append(S.Identity->Values);
    }
    Result->Values.Append(S.Editor->Values);
    Result->SetBoolField(TEXT("ready"), S.bReady);
    Result->SetBoolField(TEXT("draining"), S.bDraining);
    Result->SetBoolField(TEXT("stopping"), S.bStopping);
    Result->SetNumberField(TEXT("manager_epoch"), S.ManagerEpoch);
    Result->SetNumberField(TEXT("context_epoch"), S.ContextEpoch);
    Result->SetNumberField(TEXT("close_revision"), S.Revision);
    Result->SetNumberField(TEXT("scope_count"), S.Grants.Num());
    return Result;
}

TSharedPtr<FJsonObject> Snapshot()
{
    FScopeLock Lock(&State().Mutex);
    return StatusLocked();
}

void Attach(FInspector Inspector)
{
    FScopeLock Lock(&State().Mutex);
    State().Inspector = MoveTemp(Inspector);
    State().bReady = true;
}

void Detach()
{
    FScopeLock Lock(&State().Mutex);
    State().bReady = false;
    State().bStopping = true;
    State().Inspector = nullptr;
}

void Publish(const TSharedPtr<FJsonObject>& Editor)
{
    FScopeLock Lock(&State().Mutex);
    State().Editor = Editor;
}

TSharedPtr<FJsonObject> Dispatch(const TSharedPtr<FJsonObject>& Request, uint32 Peer)
{
    FString Operation;
    const TSharedPtr<FJsonObject>* Payload = nullptr;
    if (!ReadString(Request, TEXT("operation"), Operation)
        || !Request->TryGetObjectField(TEXT("payload"), Payload))
    {
        return Failure(TEXT("invalid_request"), TEXT("operation and payload are required"));
    }
    if (Operation == TEXT("status"))
    {
        return Success(Snapshot());
    }
    if (!Authenticate(*Payload, Peer))
    {
        return Failure(TEXT("stale_manager"), TEXT("manager identity, epoch or instance no longer matches"));
    }
    if (Operation.EndsWith(TEXT("close")))
    {
        return Close(Operation, *Payload);
    }
    FScopeLock Lock(&State().Mutex);
    auto& S = State();
    FString ScopeId;
    if (Operation == TEXT("adopt"))
    {
        S.bManaged = true;
        return Success(StatusLocked());
    }
    if (!ReadString(*Payload, TEXT("scope_id"), ScopeId) || ScopeId.IsEmpty())
    {
        return Failure(TEXT("invalid_request"), TEXT("scope_id is required"));
    }
    if (Operation == TEXT("scope_grant"))
    {
        FString Client, Lease;
        bool bExclusive = false;
        const TSharedPtr<FJsonObject>* Identity = nullptr;
        if (!ReadString(*Payload, TEXT("client_session_id"), Client) || Client.IsEmpty()
            || !ReadString(*Payload, TEXT("lease_id"), Lease) || Lease.IsEmpty()
            || !(*Payload)->TryGetObjectField(TEXT("identity"), Identity)
            || !ReadBool(*Payload, TEXT("exclusive"), bExclusive) || ScopeId.Len() > 128)
        {
            return Failure(TEXT("invalid_request"), TEXT("scope owner and mode are required"));
        }
        const auto* Previous = S.Grants.Find(ScopeId);
        if (Previous)
        {
            bool bReleasing = false;
            (*Previous)->TryGetBoolField(TEXT("releasing"), bReleasing);
            if (bReleasing || (*Previous)->GetStringField(TEXT("client_session_id")) != Client
                || (*Previous)->GetStringField(TEXT("lease_id")) != Lease
                || (*Previous)->GetBoolField(TEXT("exclusive")) != bExclusive)
            {
                return Failure(TEXT("stale_scope"), TEXT("a released scope or changed owner cannot be granted again"));
            }
        }
        if (S.bDraining || S.bStopping || !S.bReady)
        {
            return Failure(TEXT("instance_unavailable"), TEXT("editor is not accepting work"));
        }
        if (S.Grants.Num() >= 256 && !S.Grants.Contains(ScopeId))
        {
            return Failure(TEXT("admission_queue_full"), TEXT("native scope limit reached"));
        }
        S.Grants.Add(ScopeId, *Payload);
        return Success(ScopeStatus(ScopeId));
    }
    if (Operation == TEXT("scope_release"))
    {
        const auto Result = ScopeStatus(ScopeId);
        if (Result->GetNumberField(TEXT("pending")) + Result->GetNumberField(TEXT("inflight")) == 0)
        {
            S.Grants.Remove(ScopeId);
        }
        else if (S.Grants.Contains(ScopeId))
        {
            S.Grants[ScopeId]->SetBoolField(TEXT("releasing"), true);
        }
        return Success(Result);
    }
    return Failure(TEXT("unknown_lifecycle_action"), Operation);
}
}
