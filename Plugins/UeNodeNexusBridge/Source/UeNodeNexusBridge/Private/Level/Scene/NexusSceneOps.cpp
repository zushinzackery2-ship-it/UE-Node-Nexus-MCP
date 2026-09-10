#include "NexusSceneOps.h"

#include "NexusSceneApply.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace UeNodeNexusBridge
{
using namespace Scene;

TSharedPtr<FJsonObject> HandleSceneExport(const FString& Operation, const FString& RequestId, const FObject& Payload)
{
    FString Error;
    const FString File = String(Payload, TEXT("out_file"));
    UWorld* World = ResolveWorld(String(Payload, TEXT("map_path")), Error);
    FObject Snapshot = World ? ExportScene(World, Payload, Error) : nullptr;
    if (!Snapshot.IsValid() || !File.EndsWith(TEXT(".json")) || !Transcode::IsInsideMirrorRoot(File)
        || !Transcode::WriteJsonFile(File, Snapshot, Error))
    {
        return MakeOperationError(Operation, RequestId, TEXT("scene_export_failed"), Error);
    }
    FObject Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("file"), File);
    Data->SetStringField(TEXT("revision"), String(Snapshot, TEXT("revision")));
    Data->SetNumberField(TEXT("actor_count"), Rows(Snapshot, TEXT("actors")).Num());
    Data->SetArrayField(TEXT("unavailable"), Rows(Snapshot, TEXT("unavailable")));
    FObject Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleSceneStatus(const FString& Operation, const FString& RequestId, const FObject& Payload)
{
    FString Error;
    UWorld* World = ResolveWorld(String(Payload, TEXT("map_path")), Error);
    FObject Snapshot = World ? ExportScene(World, Payload, Error) : nullptr;
    if (!Snapshot.IsValid())
    {
        return MakeOperationError(Operation, RequestId, TEXT("scene_unavailable"), Error);
    }
    Snapshot->SetNumberField(TEXT("actor_count"), Rows(Snapshot, TEXT("actors")).Num());
    Snapshot->RemoveField(TEXT("actors"));
    FObject Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Snapshot);
    return Response;
}

TSharedPtr<FJsonObject> HandleSceneApply(const FString& Operation, const FString& RequestId, const FObject& Payload)
{
    FObject Plan;
    FString Error;
    const FString File = String(Payload, TEXT("out_file"));
    if (!ReadFile(String(Payload, TEXT("plan_file")), Plan, Error) || !File.EndsWith(TEXT(".json")) || !Transcode::IsInsideMirrorRoot(File))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_scene_plan"), Error);
    }
    UWorld* World = ResolveWorld(String(Plan, TEXT("map_path")), Error);
    bool bDryRun = true;
    bool bSave = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);
    FObject Data = World ? ApplyScene(World, Plan, bDryRun, bSave, Error) : nullptr;
    if (!Data.IsValid())
    {
        return MakeOperationError(Operation, RequestId, TEXT("scene_apply_failed"), Error);
    }
    const FObject Snapshot = Object(Data, TEXT("snapshot"));
    if (!bDryRun && Snapshot->Values.Num() > 0)
    {
        if (Transcode::WriteJsonFile(File, Snapshot, Error))
        {
            Data->SetStringField(TEXT("file"), File);
        }
        else
        {
            Data->SetBoolField(TEXT("ok"), false);
            Data->SetStringField(TEXT("export_error"), Error);
        }
    }
    Data->RemoveField(TEXT("snapshot"));
    bool bOk = false;
    Data->TryGetBoolField(TEXT("ok"), bOk);
    FObject Response = MakeEnvelope(Operation, RequestId, bOk);
    Response->SetObjectField(TEXT("data"), Data);
    if (!bOk)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(
            FString(TEXT("scene_incomplete")), String(Data, TEXT("error"), TEXT("scene write or save did not complete"))));
    }
    return Response;
}
}
