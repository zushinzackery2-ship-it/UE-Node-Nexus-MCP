#include "NexusPackageFiles.h"

#include "Misc/Paths.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge::Collaboration
{
static bool SavedFilesMatch(const FJson& Receipt)
{
    bool bSawSave = false;
    for (const auto& Value : Rows(Receipt, TEXT("packages")))
    {
        const FJson Package = Value->AsObject();
        if (Text(Package, TEXT("save_phase")).IsEmpty())
        {
            continue;
        }
        bSawSave = true;
        for (const auto& FileValue : Rows(Package, TEXT("files")))
        {
            const FJson File = FileValue->AsObject();
            const FString Expected = File->HasField(TEXT("planned_hash")) ? Text(File, TEXT("planned_hash")) : Text(File, TEXT("saved_hash"));
            if (Expected.IsEmpty() || FileHash(Text(File, TEXT("path"))) != Expected)
            {
                return false;
            }
        }
    }
    return bSawSave;
}

FJson Recover(const FString& Operation, const FString& RequestId, const FJson& Payload)
{
    const FString Directory = TransactionDirectory(Text(Payload, TEXT("apply_id")));
    FJson Receipt;
    if (Directory.IsEmpty() || !ReadJournal(Directory / TEXT("receipt.json"), Receipt))
    {
        return CommitError(Operation, RequestId, TEXT("apply_not_found"), TEXT("no durable receipt for this apply_id"));
    }
    if (!FPaths::IsSamePath(Text(Payload, TEXT("repository")), BoundRepository())
        || !FPaths::IsSamePath(Text(Object(Receipt, TEXT("request")), TEXT("repository")), BoundRepository()))
    {
        return CommitError(Operation, RequestId, TEXT("repository_mismatch"), TEXT("recovery requires the repository that owns this apply"));
    }
    FString Error;
    FString Phase = Text(Receipt, TEXT("phase"));
    if (Phase == TEXT("saving") && SavedFilesMatch(Receipt))
    {
        FJson SavedResponse = MakeEnvelope(Text(Object(Receipt, TEXT("request")), TEXT("operation")), Text(Receipt, TEXT("request_id")), true);
        FJson SavedData = MakeShared<FJsonObject>(*Object(Receipt, TEXT("response_data")));
        SavedResponse->SetObjectField(TEXT("data"), SavedData);
        const FJson Current = ResultSnapshot(Object(Receipt, TEXT("request")), SavedResponse);
        if (Current.IsValid() && Text(Current, TEXT("content_revision")) == Text(Object(Receipt, TEXT("applied")), TEXT("content_revision")))
        {
            Receipt->SetObjectField(TEXT("after"), Current);
            SavedData->SetBoolField(TEXT("saved"), true);
            SavedData->SetStringField(TEXT("apply_id"), Text(Receipt, TEXT("apply_id")));
            ExportResult(Object(Receipt, TEXT("request")), Current, SavedData, Error);
            Receipt->SetObjectField(TEXT("response"), SavedResponse);
            if (!SaveReceipt(Receipt, TEXT("ue_committed"), Error))
            {
                return CommitError(Operation, RequestId, TEXT("journal_failed"), Error, Receipt);
            }
            Phase = TEXT("ue_committed");
        }
    }
    if (Flag(Payload, TEXT("restore")) && Phase != TEXT("rolled_back") && Phase != TEXT("rejected"))
    {
        if (Phase == TEXT("ue_committed"))
        {
            return CommitError(Operation, RequestId, TEXT("published_history"), TEXT("recover the saved receipt or create a revert commit"), Receipt);
        }
        const FJson Current = ResultSnapshot(Object(Receipt, TEXT("request")), Object(Receipt, TEXT("response")));
        const FString Content = Text(Current, TEXT("content_revision"));
        const bool bSameEpoch = Text(Receipt, TEXT("editor_epoch")) == EditorEpoch();
        const bool bKnownMemory = Content == Text(Object(Receipt, TEXT("before")), TEXT("content_revision"))
            || Content == Text(Object(Receipt, TEXT("applied")), TEXT("content_revision"));
        if ((bSameEpoch && !bKnownMemory) || !CheckRecoveryFiles(Receipt, Error))
        {
            return CommitError(Operation, RequestId, TEXT("recovery_conflict"), Error.IsEmpty() ? TEXT("memory changed after the recorded apply") : Error, Receipt);
        }
        if (!RestoreCheckpoint(Receipt, Error))
        {
            Receipt->SetStringField(TEXT("recovery_error"), Error);
            FString JournalError;
            SaveReceipt(Receipt, TEXT("recovery_required"), JournalError);
            return CommitError(Operation, RequestId, TEXT("recovery_required"), Error, Receipt);
        }
    }
    FJson Data = MakeShared<FJsonObject>();
    Data->SetObjectField(TEXT("receipt"), Receipt);
    FJson Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
