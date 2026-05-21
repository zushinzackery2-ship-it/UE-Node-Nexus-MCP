void StartupAutoIndex()
{
    FAutoIndexState& State = AutoIndex();
    if (State.LoadFromDisk() && State.bEnabled)
    {
        State.RegisterListeners();
        State.RebuildFromRegistry();
        State.FlushToDisk();
    }
}

void ShutdownAutoIndex()
{
    FAutoIndexState& State = AutoIndex();
    if (State.bDirty)
    {
        State.FlushToDisk();
    }
    State.UnregisterListeners();
}

TSharedPtr<FJsonObject> HandleAutoIndexEnable(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FAutoIndexState& State = AutoIndex();
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
    return MakeAutoIndexEnvelope(Operation, RequestId, BuildStatusData(TEXT("auto_index_enable_status")));
}

TSharedPtr<FJsonObject> HandleAutoIndexDisable(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FAutoIndexState& State = AutoIndex();
    State.bEnabled = false;
    State.UnregisterListeners();
    State.MarkDirty();
    State.FlushToDisk();
    return MakeAutoIndexEnvelope(Operation, RequestId, BuildStatusData(TEXT("auto_index_disable_status")));
}

TSharedPtr<FJsonObject> HandleAutoIndexStatus(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return MakeAutoIndexEnvelope(Operation, RequestId, BuildStatusData(TEXT("auto_index_status_indexed")));
}

TSharedPtr<FJsonObject> HandleAutoIndexRebuild(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FAutoIndexState& State = AutoIndex();
    State.RootPath = ReadRootPath(Payload, State.RootPath);
    State.RebuildFromRegistry();
    State.FlushToDisk();
    return MakeAutoIndexEnvelope(Operation, RequestId, BuildStatusData(TEXT("auto_index_rebuild_status")));
}

TSharedPtr<FJsonObject> HandleAutoIndexFlush(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FAutoIndexState& State = AutoIndex();
    if (!State.FlushToDisk())
    {
        return MakeAutoIndexError(Operation, RequestId, TEXT("auto_index_flush_failed"), State.IndexPath());
    }
    return MakeAutoIndexEnvelope(Operation, RequestId, BuildStatusData(TEXT("auto_index_flush_status")));
}

TSharedPtr<FJsonObject> HandleAutoIndexClear(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    bool bDeleteFile = false;
    Payload->TryGetBoolField(TEXT("delete_file"), bDeleteFile);
    AutoIndex().Clear(bDeleteFile);
    return MakeAutoIndexEnvelope(Operation, RequestId, BuildStatusData(TEXT("auto_index_clear_status")));
}

TSharedPtr<FJsonObject> HandleAutoIndexOverview(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return MakeAutoIndexEnvelope(Operation, RequestId, BuildOverviewData(Payload));
}

TSharedPtr<FJsonObject> HandleAutoIndexTreeGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return MakeAutoIndexEnvelope(Operation, RequestId, BuildTreeData(Payload));
}

TSharedPtr<FJsonObject> HandleAutoIndexQuery(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return MakeAutoIndexEnvelope(Operation, RequestId, BuildQueryData(Payload));
}

TSharedPtr<FJsonObject> HandleAutoIndexGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        return MakeAutoIndexError(Operation, RequestId, TEXT("invalid_request"), TEXT("asset_path is required"));
    }

    const FAutoIndexAssetRecord* Record = AutoIndex().AssetsByObjectPath.Find(AssetPath);
    if (Record == nullptr)
    {
        return MakeAutoIndexError(Operation, RequestId, TEXT("asset_not_found"), AssetPath);
    }

    TSharedPtr<FJsonObject> Data = AutoIndex().AssetRecordToJson(*Record);
    Data->SetStringField(TEXT("format"), TEXT("auto_index_asset_full"));
    FString Text = FString::Printf(TEXT("A:%s|c=%s|f=%s|m=%s|r=%d\n"), *EscapeIndexedToken(Record->ObjectPath), *EscapeIndexedToken(Record->ClassName), *EscapeIndexedToken(Record->PackagePath), *EscapeIndexedToken(Record->ModifiedUtc), Record->bRedirector ? 1 : 0);
    SetTextPayload(Data, Text);
    return MakeAutoIndexEnvelope(Operation, RequestId, Data);
}

TSharedPtr<FJsonObject> HandleAutoIndexResolvePath(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return MakeAutoIndexEnvelope(Operation, RequestId, BuildResolveData(Payload));
}

TSharedPtr<FJsonObject> HandleAutoIndexDiffRegistry(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return MakeAutoIndexEnvelope(Operation, RequestId, BuildDiffData());
}
