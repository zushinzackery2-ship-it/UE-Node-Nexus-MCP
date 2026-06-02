FAutoIndexState& AutoIndex()
{
    static FAutoIndexState State;
    return State;
}

TSharedPtr<FJsonObject> MakeAutoIndexEnvelope(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Data)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> MakeAutoIndexError(const FString& Operation, const FString& RequestId, const FString& Code, const FString& Message)
{
    return MakeOperationError(Operation, RequestId, Code, Message);
}

bool IsFullFormat(const TSharedPtr<FJsonObject>& Payload)
{
    FString Format = TEXT("indexed");
    Payload->TryGetStringField(TEXT("format"), Format);
    return Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
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

TArray<FString> SortedAssetKeys()
{
    TArray<FString> Keys;
    AutoIndex().AssetsByObjectPath.GetKeys(Keys);
    Keys.Sort();
    return Keys;
}

TArray<FString> SortedFolderKeys()
{
    TArray<FString> Keys;
    AutoIndex().FoldersByPath.GetKeys(Keys);
    Keys.Sort();
    return Keys;
}

bool FolderMatchesRoot(const FString& FolderPath, const FString& RootPath, bool bRecursive)
{
    if (bRecursive)
    {
        return FolderPath.Equals(RootPath, ESearchCase::IgnoreCase)
            || FolderPath.StartsWith(RootPath + TEXT("/"), ESearchCase::IgnoreCase);
    }
    return FolderPath.Equals(RootPath, ESearchCase::IgnoreCase)
        || FAutoIndexState::ParentFolder(FolderPath).Equals(RootPath, ESearchCase::IgnoreCase);
}

FString ClassCountsToken(const TMap<FString, int32>& ClassCounts, TMap<FString, int32>& ClassDict, TArray<FString>& Classes)
{
    TArray<FString> Keys;
    ClassCounts.GetKeys(Keys);
    Keys.Sort();

    TArray<FString> Parts;
    for (const FString& Key : Keys)
    {
        const int32 ClassIndex = DictIndex(ClassDict, Classes, Key);
        Parts.Add(FString::Printf(TEXT("%d:%d"), ClassIndex, ClassCounts.FindChecked(Key)));
    }
    return FString::Join(Parts, TEXT(","));
}

FString MakeStatusText()
{
    const FAutoIndexState& State = AutoIndex();
    return FString::Printf(
        TEXT("S:enabled=%d|listening=%d|assets=%d|folders=%d|dirty=%d|serial=%d\nP:%s\nT:build=%s|flush=%s|event=%s\n"),
        State.bEnabled ? 1 : 0,
        State.IsListening() ? 1 : 0,
        State.AssetsByObjectPath.Num(),
        State.FoldersByPath.Num(),
        State.bDirty ? 1 : 0,
        State.ChangeSerial,
        *EscapeIndexedToken(State.IndexPath()),
        *EscapeIndexedToken(State.LastBuildUtc),
        *EscapeIndexedToken(State.LastFlushUtc),
        *EscapeIndexedToken(State.LastEventUtc));
}

TSharedPtr<FJsonObject> BuildStatusData(const FString& Format)
{
    const FAutoIndexState& State = AutoIndex();
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), Format);
    Data->SetBoolField(TEXT("enabled"), State.bEnabled);
    Data->SetBoolField(TEXT("listening"), State.IsListening());
    Data->SetBoolField(TEXT("dirty"), State.bDirty);
    Data->SetBoolField(TEXT("loaded_from_disk"), State.bLoadedFromDisk);
    Data->SetStringField(TEXT("index_path"), State.IndexPath());
    Data->SetStringField(TEXT("root_path"), State.RootPath);
    Data->SetStringField(TEXT("last_build_utc"), State.LastBuildUtc);
    Data->SetStringField(TEXT("last_flush_utc"), State.LastFlushUtc);
    Data->SetStringField(TEXT("last_event_utc"), State.LastEventUtc);
    Data->SetNumberField(TEXT("asset_count"), State.AssetsByObjectPath.Num());
    Data->SetNumberField(TEXT("folder_count"), State.FoldersByPath.Num());
    Data->SetNumberField(TEXT("change_serial"), State.ChangeSerial);
    SetTextPayload(Data, MakeStatusText());
    return Data;
}
