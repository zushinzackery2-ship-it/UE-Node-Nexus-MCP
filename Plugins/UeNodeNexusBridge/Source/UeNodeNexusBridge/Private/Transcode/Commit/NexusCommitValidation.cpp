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

bool CheckRevisions(const FJson& Request, FJson& Current, FString& Error)
{
    if (!CheckOne(Request, Current, Error))
    {
        return false;
    }
    for (const auto& Value : Rows(Request, TEXT("read_set")))
    {
        FJson Dependency;
        if (!Value.IsValid() || Value->Type != EJson::Object || !CheckOne(Value->AsObject(), Dependency, Error))
        {
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
