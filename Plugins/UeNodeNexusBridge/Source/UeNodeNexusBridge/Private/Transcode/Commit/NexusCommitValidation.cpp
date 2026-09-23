#include "NexusCommitInternal.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "Misc/Paths.h"

namespace UeNodeNexusBridge::Collaboration
{
FJson CommitError(const FString& Operation, const FString& RequestId, const FString& Code, const FString& Error, const FJson& Receipt)
{
    FJson Response = MakeOperationError(Operation, RequestId, Code, Error);
    FJson Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("applied"), 0);
    if (Receipt.IsValid())
    {
        Data->SetObjectField(TEXT("receipt"), Receipt);
        // A rolled back apply answers with a fresh error envelope, so the body's
        // own data - including every compiler message that explains the failure -
        // is gone by the time the caller sees anything. Those messages are the
        // only thing that says which node was wrong, so they travel with the error.
        const TArray<TSharedPtr<FJsonValue>> Diagnostics = Rows(Object(Receipt, TEXT("response_data")), TEXT("diagnostics"));
        if (Diagnostics.Num() > 0)
        {
            Data->SetArrayField(TEXT("diagnostics"), Diagnostics);
        }
    }
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

static bool CheckOne(const FJson& Expected, FJson& Current, FString& Error)
{
    Current = Observe(Expected);
    if (!Current.IsValid())
    {
        Error = TEXT("cannot observe target: ") + Text(Expected, TEXT("asset_path"));
        return false;
    }
    const bool bAbsent = Current->HasField(TEXT("exists")) && !Flag(Current, TEXT("exists"));
    if (Flag(Expected, TEXT("expected_absent")))
    {
        if (!bAbsent)
        {
            Error = TEXT("add-add: target was created since observation");
        }
        return bAbsent;
    }
    const FString Revision = Text(Expected, TEXT("expected_revision"));
    if (bAbsent || Revision.IsEmpty() || Revision != Text(Current, TEXT("live_revision")))
    {
        Error = TEXT("revision changed: ") + Text(Expected, TEXT("asset_path"));
        return false;
    }
    return true;
}

// Which guarded asset failed, and what it holds now. Without it every failure
// reads as the target's, and the caller cannot tell its own publication's side
// effect on a dependency from a concurrent edit of the asset it is writing.
static FJson StaleReport(const FJson& Expected, const FJson& Current, const TCHAR* Role)
{
    FJson Stale = MakeShared<FJsonObject>();
    Stale->SetStringField(TEXT("role"), Role);
    Stale->SetStringField(TEXT("asset_path"), Text(Expected, TEXT("asset_path")));
    Stale->SetStringField(TEXT("expected_revision"), Text(Expected, TEXT("expected_revision")));
    Stale->SetBoolField(TEXT("expected_absent"), Flag(Expected, TEXT("expected_absent")));
    if (Current.IsValid())
    {
        Stale->SetStringField(TEXT("live_revision"), Text(Current, TEXT("live_revision")));
        Stale->SetStringField(TEXT("content_revision"), Text(Current, TEXT("content_revision")));
        Stale->SetBoolField(TEXT("exists"), !Current->HasField(TEXT("exists")) || Flag(Current, TEXT("exists")));
        Stale->SetBoolField(TEXT("dirty"), Flag(Current, TEXT("dirty")));
    }
    return Stale;
}

bool CheckRevisions(const FJson& Request, FJson& Current, FJson& Stale, FString& Error)
{
    if (!CheckOne(Request, Current, Error))
    {
        Stale = StaleReport(Request, Current, TEXT("target"));
        return false;
    }
    for (const auto& Value : Rows(Request, TEXT("read_set")))
    {
        if (!Value.IsValid() || Value->Type != EJson::Object)
        {
            Error = TEXT("read_set rows must be objects");
            return false;
        }
        FJson Dependency;
        if (!CheckOne(Value->AsObject(), Dependency, Error))
        {
            Stale = StaleReport(Value->AsObject(), Dependency, TEXT("dependency"));
            return false;
        }
    }
    return true;
}

FJson ResultSnapshot(const FJson& Request, const FJson& Response)
{
    FJson Observation = MakeShared<FJsonObject>(*Request);
    // A scene's members may change in the operation. Use its actual returned
    // selector instead of the selector that described the precondition.
    const FJson Data = Object(Response, TEXT("data"));
    if (Data->HasField(TEXT("result_selector")))
    {
        Observation->SetObjectField(TEXT("selector"), Object(Data, TEXT("result_selector")));
    }
    return Observe(Observation);
}

bool ExportResult(const FJson& Request, const FJson& Snapshot, const FJson& Data, FString& Error)
{
    FString File = Text(Request, TEXT("out_file"));
    if (Text(Request, TEXT("kind")) != TEXT("scene"))
    {
        if (!Transcode::ResolveRawFile(Text(Request, TEXT("out_dir")), Text(Request, TEXT("asset_path")), File, Error))
        {
            return false;
        }
    }
    if (File.IsEmpty() || !Transcode::IsInsideMirrorRoot(File) || !Transcode::WriteJsonFile(File, Snapshot, Error))
    {
        return false;
    }
    Data->SetStringField(TEXT("file"), File);
    Data->SetStringField(TEXT("live_revision"), Text(Snapshot, TEXT("live_revision")));
    Data->SetStringField(TEXT("saved_hash"), Text(Snapshot, TEXT("saved_hash")));
    Data->SetBoolField(TEXT("dirty"), Flag(Snapshot, TEXT("dirty")));
    return true;
}
}
