#include "State.h"
#include "Json.h"

#include "Async/Async.h"
#include "Misc/ScopeLock.h"

namespace NexusLifecycle
{
static TSharedPtr<FJsonObject> Pending(uint64 Revision)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("close_pending"), true);
    Result->SetNumberField(TEXT("close_revision"), Revision);
    return Success(Result);
}

static TArray<TSharedPtr<FJsonValue>> Blockers(const TSharedPtr<FJsonObject>& Editor)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (Editor->TryGetArrayField(TEXT("blockers"), Values))
    {
        Result = *Values;
    }
    auto Work = ScopeStatus(FString());
    if (Work->GetNumberField(TEXT("pending")) + Work->GetNumberField(TEXT("inflight")) > 0 || !State().Grants.IsEmpty())
    {
        Result.Add(MakeShared<FJsonValueString>(TEXT("work_in_progress")));
    }
    return Result;
}

static void Evaluate(const FString& CloseId, uint64 Revision, TArray<FString> SavePackages, bool bAutomatic, bool bCommit)
{
    FInspector Inspector;
    {
        FScopeLock Lock(&State().Mutex);
        auto& S = State();
        if (!S.bDraining || S.Revision != Revision || S.CloseId != CloseId || !S.Inspector)
        {
            return;
        }
        Inspector = S.Inspector;
    }
    // UObject inspection and package saves are confined to this game-thread callback.
    const auto Editor = Inspector(SavePackages, bAutomatic, [CloseId, Revision]()
    {
        FScopeLock Lock(&State().Mutex);
        return State().bDraining && !State().bStopping && State().Revision == Revision && State().CloseId == CloseId;
    });
    bool bExit = false;
    {
        FScopeLock Lock(&State().Mutex);
        auto& S = State();
        if (!S.bDraining || S.Revision != Revision || S.CloseId != CloseId)
        {
            return;
        }
        const auto Reasons = Blockers(Editor);
        S.Editor = Editor;
        S.Prepared = MakeShared<FJsonObject>();
        S.Prepared->SetArrayField(TEXT("blockers"), Reasons);
        S.Prepared->SetNumberField(TEXT("close_revision"), Revision);
        S.Prepared->SetBoolField(TEXT("close_pending"), false);
        S.Prepared->SetBoolField(TEXT("exit_requested"), bCommit && Reasons.IsEmpty());
        S.Prepared->SetObjectField(TEXT("editor"), Editor);
        S.bCommitQueued = false;
        if (!Reasons.IsEmpty())
        {
            S.bDraining = false;
        }
        else if (bCommit)
        {
            S.bStopping = true;
            bExit = true;
        }
    }
    if (bExit)
    {
        UE_LOG(LogTemp, Display, TEXT("Nexus Guard close_committed close_id=%s revision=%llu"), *CloseId, Revision);
        FPlatformMisc::RequestExit(false);
    }
}

TSharedPtr<FJsonObject> Close(const FString& Operation, const TSharedPtr<FJsonObject>& Payload)
{
    FString CloseId;
    ReadString(Payload, TEXT("close_id"), CloseId);
    FScopeLock Lock(&State().Mutex);
    auto& S = State();
    if (Operation == TEXT("cancel_close"))
    {
        if (S.bStopping)
        {
            return Failure(TEXT("instance_stopping"), TEXT("normal exit has already been committed"));
        }
        if (!CloseId.IsEmpty() && CloseId != S.CloseId)
        {
            return Failure(TEXT("stale_plan"), TEXT("close id changed"));
        }
        S.bDraining = false;
        S.bCommitQueued = false;
        S.Prepared.Reset();
        ++S.Revision;
        return Success(StatusLocked());
    }
    if (CloseId.IsEmpty() || !S.bReady)
    {
        return Failure(TEXT("instance_unavailable"), TEXT("ready instance and close_id required"));
    }
    bool bAutomatic = false;
    Payload->TryGetBoolField(TEXT("automatic"), bAutomatic);
    if (Operation == TEXT("prepare_close"))
    {
        if (S.CloseId == CloseId)
        {
            return S.Prepared.IsValid() ? Success(S.Prepared) : Pending(S.Revision);
        }
        if (S.bDraining || S.bStopping)
        {
            return Failure(TEXT("instance_draining"), TEXT("another close is in progress"));
        }
        TArray<FString> Packages;
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (Payload->TryGetArrayField(TEXT("save_packages"), Values))
        {
            for (const auto& Value : *Values)
            {
                if (!Value.IsValid() || Value->Type != EJson::String)
                {
                    return Failure(TEXT("invalid_request"), TEXT("save_packages must contain package name strings"));
                }
                Packages.Add(Value->AsString());
            }
        }
        if (bAutomatic && !Packages.IsEmpty())
        {
            return Failure(TEXT("invalid_request"), TEXT("automatic close cannot save packages"));
        }
        S.bDraining = true;
        S.CloseId = CloseId;
        S.Prepared.Reset();
        const uint64 Revision = ++S.Revision;
        AsyncTask(ENamedThreads::GameThread, [CloseId, Revision, Packages, bAutomatic]()
        {
            Evaluate(CloseId, Revision, Packages, bAutomatic, false);
        });
        return Pending(Revision);
    }
    double Revision = 0;
    if (Operation != TEXT("commit_close") || !ReadNumber(Payload, TEXT("close_revision"), Revision)
        || S.CloseId != CloseId || Revision != S.Revision || !S.Prepared.IsValid())
    {
        return Failure(TEXT("stale_plan"), TEXT("close revision no longer matches"));
    }
    if (S.bStopping || !S.bDraining)
    {
        return Success(S.Prepared);
    }
    if (!S.bCommitQueued)
    {
        S.bCommitQueued = true;
        AsyncTask(ENamedThreads::GameThread, [CloseId, Revision, bAutomatic]()
        {
            Evaluate(CloseId, static_cast<uint64>(Revision), TArray<FString>(), bAutomatic, true);
        });
    }
    return Pending(S.Revision);
}
}
