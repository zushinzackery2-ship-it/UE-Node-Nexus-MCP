#include "NexusCommitInternal.h"

#include "ObjectTools.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge::Collaboration
{
FJson DeleteAsset(const FString& Operation, const FString& RequestId, const FJson& Payload)
{
    if (Text(Payload, TEXT("apply_id")).IsEmpty())
    {
        return CommitError(Operation, RequestId, TEXT("protocol_required"), TEXT("snapshot-backed deletion requires a collaboration apply"));
    }
    UObject* Asset = LoadObject<UObject>(nullptr, *Text(Payload, TEXT("asset_path")));
    if (!Asset)
    {
        return CommitError(Operation, RequestId, TEXT("asset_not_found"), TEXT("deletion target is unavailable"));
    }
    const bool bDryRun = Flag(Payload, TEXT("dry_run"), true);
    TArray<UObject*> Assets;
    Assets.Add(Asset);
    if (!bDryRun && ObjectTools::DeleteObjectsUnchecked(Assets) != 1)
    {
        return CommitError(Operation, RequestId, TEXT("delete_failed"), TEXT("the target could not be deleted"));
    }
    FJson Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("deleted"), !bDryRun);
    Data->SetBoolField(TEXT("changed"), !bDryRun);
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetNumberField(TEXT("applied"), bDryRun ? 0 : 1);
    FJson Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
