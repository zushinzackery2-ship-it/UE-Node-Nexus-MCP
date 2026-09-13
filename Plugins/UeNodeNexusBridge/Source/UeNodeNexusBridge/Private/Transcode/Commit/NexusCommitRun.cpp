#include "NexusCommitInternal.h"

#include "Misc/Paths.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge::Collaboration
{
static FJson WithReceipt(const FJson& Response, const FJson& Receipt)
{
    FJson Result = MakeShared<FJsonObject>(*Response);
    FJson Data = MakeShared<FJsonObject>(*Object(Response, TEXT("data")));
    Data->SetObjectField(TEXT("receipt"), Receipt);
    Result->SetObjectField(TEXT("data"), Data);
    return Result;
}

static FJson Rollback(const FString& Operation, const FString& RequestId, const FJson& Receipt, const FString& Failure)
{
    Receipt->SetStringField(TEXT("failure"), Failure);
    FString RecoveryError;
    const bool bRestored = RestoreCheckpoint(Receipt, RecoveryError);
    if (!bRestored)
    {
        Receipt->SetStringField(TEXT("recovery_error"), RecoveryError);
        FString JournalError;
        SaveReceipt(Receipt, TEXT("recovery_required"), JournalError);
    }
    return CommitError(Operation, RequestId, bRestored ? TEXT("apply_rolled_back") : TEXT("recovery_required"),
        Failure + (bRestored ? TEXT("") : TEXT("; ") + RecoveryError), Receipt);
}

FJson RunCommit(const FString& Operation, const FString& RequestId, const FJson& Payload, FCommitBody Body)
{
    const FString ApplyId = Text(Payload, TEXT("apply_id"));
    if (ApplyId.IsEmpty())
    {
        return Body(Payload);
    }
    const double Started = FPlatformTime::Seconds();
    const FString Directory = TransactionDirectory(ApplyId);
    double Protocol = 0;
    Payload->TryGetNumberField(TEXT("collaboration_version"), Protocol);
    const FString Repository = Text(Payload, TEXT("repository"));
    if (Directory.IsEmpty() || Protocol != 1 || Repository.IsEmpty() || !FPaths::IsSamePath(Repository, BoundRepository()))
    {
        return CommitError(Operation, RequestId, TEXT("protocol_mismatch"), TEXT("collaboration_version=1, a valid apply_id and the bound repository are required"));
    }
    if (!Flag(Payload, TEXT("save"), true))
    {
        return CommitError(Operation, RequestId, TEXT("save_required"), TEXT("collaboration publication requires saving all touched packages"));
    }
    FJson Request = MakeShared<FJsonObject>(*Payload);
    Request->SetStringField(TEXT("operation"), Operation);
    const FString Digest = ContentDigest(Request);
    FJson Receipt;
    if (ReadJournal(Directory / TEXT("receipt.json"), Receipt))
    {
        if (Text(Receipt, TEXT("request_digest")) != Digest)
        {
            return CommitError(Operation, RequestId, TEXT("idempotency_mismatch"), TEXT("apply_id is already bound to a different request"));
        }
        if (Text(Receipt, TEXT("phase")) == TEXT("ue_committed"))
        {
            return WithReceipt(Object(Receipt, TEXT("response")), Receipt);
        }
        return CommitError(Operation, RequestId, TEXT("recovery_required"), TEXT("query transcode_recover before retrying this apply"), Receipt);
    }
    FString Error;
    if (HasPending(Request, ApplyId, Error))
    {
        return CommitError(Operation, RequestId, TEXT("recovery_required"), Error);
    }
    FJson Before;
    if (!CheckRevisions(Request, Before, Error))
    {
        FJson Evidence = MakeShared<FJsonObject>();
        if (Before.IsValid())
        {
            Evidence->SetObjectField(TEXT("current"), Before);
        }
        return CommitError(Operation, RequestId, TEXT("stale_target"), Error, Evidence);
    }
    if (Flag(Request, TEXT("dry_run"), true))
    {
        return Body(Payload);
    }
    Receipt = MakeShared<FJsonObject>();
    Receipt->SetStringField(TEXT("apply_id"), ApplyId);
    Receipt->SetStringField(TEXT("request_id"), RequestId);
    Receipt->SetStringField(TEXT("request_digest"), Digest);
    Receipt->SetStringField(TEXT("editor_epoch"), EditorEpoch());
    Receipt->SetObjectField(TEXT("request"), Request);
    Receipt->SetObjectField(TEXT("before"), Before);
    if (!Checkpoint(Request, Before, Receipt, Error))
    {
        FString JournalError;
        Receipt->SetStringField(TEXT("failure"), Error);
        SaveReceipt(Receipt, TEXT("rejected"), JournalError);
        return CommitError(Operation, RequestId, TEXT("checkpoint_failed"), Error, Receipt);
    }
    if (!SaveReceipt(Receipt, TEXT("applying"), Error))
    {
        return CommitError(Operation, RequestId, TEXT("journal_failed"), Error, Receipt);
    }
    FJson Unsaved = MakeShared<FJsonObject>(*Payload);
    Unsaved->SetBoolField(TEXT("save"), false);
    const FJson Response = Body(Unsaved);
    const FJson Data = Object(Response, TEXT("data"));
    Receipt->SetObjectField(TEXT("response_data"), Data);
    const FJson Applied = ResultSnapshot(Request, Response);
    if (Applied.IsValid())
    {
        Receipt->SetObjectField(TEXT("applied"), Applied);
    }
    if (!Flag(Response, TEXT("ok")) || !Applied.IsValid())
    {
        return Rollback(Operation, RequestId, Receipt, Text(Object(Response, TEXT("error")), TEXT("message")));
    }
    if (!SaveReceipt(Receipt, TEXT("applied"), Error) || !SavePackages(Request, Receipt, Error))
    {
        return Rollback(Operation, RequestId, Receipt, Error);
    }
    const FJson After = ResultSnapshot(Request, Response);
    if (!After.IsValid())
    {
        return Rollback(Operation, RequestId, Receipt, TEXT("cannot read saved result"));
    }
    Receipt->SetObjectField(TEXT("after"), After);
    Data->SetBoolField(TEXT("saved"), true);
    Data->SetBoolField(TEXT("save_required"), true);
    Data->SetStringField(TEXT("apply_id"), ApplyId);
    Data->SetNumberField(TEXT("duration_ms"), (FPlatformTime::Seconds() - Started) * 1000.0);
    if (!ExportResult(Request, After, Data, Error))
    {
        Data->SetStringField(TEXT("export_error"), Error);
    }
    Receipt->SetObjectField(TEXT("response"), Response);
    if (!SaveReceipt(Receipt, TEXT("ue_committed"), Error))
    {
        return CommitError(Operation, RequestId, TEXT("recovery_required"), Error, Receipt);
    }
    return WithReceipt(Response, Receipt);
}
}
