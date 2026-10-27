#include "AutoIndex/UeNodeNexusBridgeAutoIndex.h"

#include "AutoIndex/Query/NexusAutoIndexQuery.h"
#include "AutoIndex/State/NexusAutoIndexState.h"
#include "Dom/JsonObject.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeListFormats.h"

namespace UeNodeNexusBridge
{
namespace
{
TSharedPtr<FJsonObject> MakeAutoIndexEnvelope(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Data)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

FString ReadRootPath(const TSharedPtr<FJsonObject>& Payload, const FString& DefaultValue)
{
    FString RootPath = DefaultValue;
    Payload->TryGetStringField(TEXT("root_path"), RootPath);
    if (RootPath.IsEmpty())
    {
        RootPath = TEXT("/Game");
    }
    RootPath.RemoveFromEnd(TEXT("/"));
    return RootPath;
}
}

void StartupAutoIndex()
{
    AutoIndex::FState& State = AutoIndex::FState::Get();
    if (State.LoadFromDisk() && State.bEnabled)
    {
        State.RegisterListeners();
        State.RebuildFromRegistry();
        State.FlushToDisk();
    }
}

void ShutdownAutoIndex()
{
    AutoIndex::FState& State = AutoIndex::FState::Get();
    if (State.bDirty)
    {
        State.FlushToDisk();
    }
    State.UnregisterListeners();
}

TSharedPtr<FJsonObject> HandleAutoIndexEnable(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    AutoIndex::FState& State = AutoIndex::FState::Get();
    State.RootPath = ReadRootPath(Payload, State.RootPath);
    bool bRebuild = true;
    Payload->TryGetBoolField(TEXT("rebuild"), bRebuild);
    State.bEnabled = true;
    State.RegisterListeners();
    if (bRebuild || State.AssetsByObjectPath.Num() == 0)
    {
        State.RebuildFromRegistry();
    }
    State.FlushToDisk();
    return MakeAutoIndexEnvelope(Operation, RequestId, AutoIndex::BuildStatusData(TEXT("auto_index_enable_status")));
}

TSharedPtr<FJsonObject> HandleAutoIndexDisable(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    AutoIndex::FState& State = AutoIndex::FState::Get();
    State.bEnabled = false;
    State.UnregisterListeners();
    State.MarkDirty();
    State.FlushToDisk();
    return MakeAutoIndexEnvelope(Operation, RequestId, AutoIndex::BuildStatusData(TEXT("auto_index_disable_status")));
}

TSharedPtr<FJsonObject> HandleAutoIndexStatus(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return MakeAutoIndexEnvelope(Operation, RequestId, AutoIndex::BuildStatusData(TEXT("auto_index_status_indexed")));
}

TSharedPtr<FJsonObject> HandleAutoIndexRebuild(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    AutoIndex::FState& State = AutoIndex::FState::Get();
    State.RootPath = ReadRootPath(Payload, State.RootPath);
    State.RebuildFromRegistry();
    State.FlushToDisk();
    return MakeAutoIndexEnvelope(Operation, RequestId, AutoIndex::BuildStatusData(TEXT("auto_index_rebuild_status")));
}

TSharedPtr<FJsonObject> HandleAutoIndexFlush(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    AutoIndex::FState& State = AutoIndex::FState::Get();
    if (!State.FlushToDisk())
    {
        return MakeOperationError(Operation, RequestId, TEXT("auto_index_flush_failed"), State.IndexPath());
    }
    return MakeAutoIndexEnvelope(Operation, RequestId, AutoIndex::BuildStatusData(TEXT("auto_index_flush_status")));
}

TSharedPtr<FJsonObject> HandleAutoIndexClear(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    bool bDeleteFile = false;
    Payload->TryGetBoolField(TEXT("delete_file"), bDeleteFile);
    AutoIndex::FState::Get().Clear(bDeleteFile);
    return MakeAutoIndexEnvelope(Operation, RequestId, AutoIndex::BuildStatusData(TEXT("auto_index_clear_status")));
}

TSharedPtr<FJsonObject> HandleAutoIndexOverview(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return MakeAutoIndexEnvelope(Operation, RequestId, AutoIndex::BuildOverviewData(Payload));
}

TSharedPtr<FJsonObject> HandleAutoIndexTreeGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return MakeAutoIndexEnvelope(Operation, RequestId, AutoIndex::BuildTreeData(Payload));
}

TSharedPtr<FJsonObject> HandleAutoIndexQuery(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return MakeAutoIndexEnvelope(Operation, RequestId, AutoIndex::BuildQueryData(Payload));
}

TSharedPtr<FJsonObject> HandleAutoIndexGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("asset_path is required"));
    }

    const AutoIndex::FState& State = AutoIndex::FState::Get();
    const AutoIndex::FAssetRecord* Record = State.AssetsByObjectPath.Find(AssetPath);
    if (Record == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("asset_not_found"), AssetPath);
    }

    TSharedPtr<FJsonObject> Data = State.AssetRecordToJson(*Record);
    Data->SetStringField(TEXT("format"), TEXT("auto_index_asset_full"));
    FString Text = FString::Printf(TEXT("A:%s|c=%s|f=%s|m=%s|r=%d\n"), *EscapeIndexedToken(Record->ObjectPath), *EscapeIndexedToken(Record->ClassName), *EscapeIndexedToken(Record->PackagePath), *EscapeIndexedToken(Record->ModifiedUtc), Record->bRedirector ? 1 : 0);
    SetTextPayload(Data, Text);
    return MakeAutoIndexEnvelope(Operation, RequestId, Data);
}

TSharedPtr<FJsonObject> HandleAutoIndexResolvePath(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return MakeAutoIndexEnvelope(Operation, RequestId, AutoIndex::BuildResolveData(Payload));
}

TSharedPtr<FJsonObject> HandleAutoIndexDiffRegistry(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return MakeAutoIndexEnvelope(Operation, RequestId, AutoIndex::BuildDiffData());
}
}
