#include "State.h"
#include "Json.h"
#include "../Identity/Identity.h"
#include "Misc/ScopeLock.h"

namespace NexusLifecycle
{
bool Admit(const TSharedPtr<FJsonObject>& Request, uint32 Peer, FString& Error)
{
    FScopeLock Lock(&State().Mutex);
    auto& S = State();
    FString RequestId, Instance, ScopeId, Client, Operation;
    double Epoch = 0, Context = 0;
    const TSharedPtr<FJsonObject>* Meta = nullptr;
    if (!ReadString(Request, TEXT("request_id"), RequestId) || RequestId.IsEmpty()
        || RequestId.Len() > 128 || !ReadString(Request, TEXT("operation"), Operation)
        || !Request->TryGetObjectField(TEXT("lifecycle"), Meta)
        || !ReadString(*Meta, TEXT("scope_id"), ScopeId)
        || !ReadString(*Meta, TEXT("instance_id"), Instance)
        || !ReadString(*Meta, TEXT("client_session_id"), Client)
        || !ReadNumber(*Meta, TEXT("manager_epoch"), Epoch))
    {
        Error = TEXT("lease_required");
        return false;
    }
    const auto* Grant = S.Grants.Find(ScopeId);
    const TSharedPtr<FJsonObject>* Owner = nullptr;
    double OwnerPid = 0;
    if (!Grant || !S.Identity.IsValid() || Instance != S.Identity->GetStringField(TEXT("instance_id"))
        || Epoch != S.ManagerEpoch || (*Grant)->GetNumberField(TEXT("manager_epoch")) != Epoch
        || (*Grant)->GetStringField(TEXT("client_session_id")) != Client
        || !(*Grant)->TryGetObjectField(TEXT("identity"), Owner)
        || !(*Owner)->TryGetNumberField(TEXT("pid"), OwnerPid) || OwnerPid != Peer || !SameProcess(*Owner))
    {
        Error = TEXT("stale_scope");
        return false;
    }
    bool bReleasing = false;
    (*Grant)->TryGetBoolField(TEXT("releasing"), bReleasing);
    if (S.bDraining || S.bStopping || bReleasing)
    {
        Error = TEXT("instance_draining");
        return false;
    }
    if ((Operation == TEXT("level_open") || Operation == TEXT("editor_save_all"))
        && !(*Grant)->GetBoolField(TEXT("exclusive")))
    {
        Error = TEXT("exclusive_scope_required");
        return false;
    }
    if (Operation != TEXT("project_context_get")
        && (!ReadNumber(*Meta, TEXT("context_epoch"), Context) || Context != S.ContextEpoch))
    {
        Error = TEXT("stale_context");
        return false;
    }
    if (S.Requests.Contains(RequestId))
    {
        Error = TEXT("operation_outcome_unknown");
        return false;
    }
    if (S.Requests.Num() - S.Finished.Num() >= 256)
    {
        Error = TEXT("admission_queue_full");
        return false;
    }
    FRequest Entry;
    Entry.ScopeId = ScopeId;
    S.Requests.Add(RequestId, MoveTemp(Entry));
    return true;
}

void Executing(const FString& RequestId)
{
    FScopeLock Lock(&State().Mutex);
    if (auto* Request = State().Requests.Find(RequestId))
    {
        Request->bRunning = true;
    }
}

void Complete(const FString& RequestId, bool bOk, const FString& Code, bool bContextChanged)
{
    FScopeLock Lock(&State().Mutex);
    auto& S = State();
    auto* Request = S.Requests.Find(RequestId);
    if (!Request)
    {
        return;
    }
    Request->bFinished = true;
    Request->bRunning = false;
    Request->Outcome = bOk ? TEXT("completed") : Code;
    S.Finished.Add(RequestId);
    while (S.Finished.Num() > 256)
    {
        S.Requests.Remove(S.Finished[0]);
        S.Finished.RemoveAt(0, 1, EAllowShrinking::No);
    }
    if (bOk && bContextChanged)
    {
        ++S.ContextEpoch;
    }
}
}
