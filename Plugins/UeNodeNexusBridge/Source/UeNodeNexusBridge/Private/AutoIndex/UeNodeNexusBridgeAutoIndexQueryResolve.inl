bool AssetRecordMatchesText(const FAutoIndexAssetRecord& Record, const FString& Text)
{
    if (Text.IsEmpty())
    {
        return true;
    }
    return Record.ObjectPath.Contains(Text, ESearchCase::IgnoreCase)
        || Record.AssetName.Contains(Text, ESearchCase::IgnoreCase)
        || Record.PackagePath.Contains(Text, ESearchCase::IgnoreCase)
        || Record.ClassName.Contains(Text, ESearchCase::IgnoreCase);
}

bool AssetRecordMatchesClasses(const FAutoIndexAssetRecord& Record, const TArray<FString>& ClassNames)
{
    if (ClassNames.Num() == 0)
    {
        return true;
    }
    for (const FString& ClassName : ClassNames)
    {
        if (Record.ClassName.Equals(ClassName, ESearchCase::IgnoreCase) || Record.ClassPath.Equals(ClassName, ESearchCase::IgnoreCase) || Record.ClassPath.EndsWith(TEXT(".") + ClassName, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

bool AssetRecordMatchesPaths(const FAutoIndexAssetRecord& Record, const TArray<FString>& PackagePaths, bool bRecursive)
{
    if (PackagePaths.Num() == 0)
    {
        return true;
    }
    for (const FString& PackagePath : PackagePaths)
    {
        if (bRecursive)
        {
            if (Record.PackagePath.Equals(PackagePath, ESearchCase::IgnoreCase) || Record.PackagePath.StartsWith(PackagePath + TEXT("/"), ESearchCase::IgnoreCase))
            {
                return true;
            }
        }
        else if (Record.PackagePath.Equals(PackagePath, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

TSharedPtr<FJsonObject> BuildQueryData(const TSharedPtr<FJsonObject>& Payload)
{
    const FAutoIndexState& State = AutoIndex();
    FString TextQuery;
    Payload->TryGetStringField(TEXT("text"), TextQuery);
    TArray<FString> ClassNames;
    Payload->TryGetStringArrayField(TEXT("class_names"), ClassNames);
    TArray<FString> PackagePaths;
    Payload->TryGetStringArrayField(TEXT("package_paths"), PackagePaths);
    bool bRecursive = true;
    Payload->TryGetBoolField(TEXT("recursive"), bRecursive);
    bool bIncludeRedirectors = false;
    Payload->TryGetBoolField(TEXT("include_redirectors"), bIncludeRedirectors);

    const int32 Offset = ReadCursor(Payload);
    const int32 Limit = ReadLimit(Payload, 80, 500);

    TArray<const FAutoIndexAssetRecord*> Matches;
    TArray<FString> Keys = SortedAssetKeys();
    for (const FString& Key : Keys)
    {
        const FAutoIndexAssetRecord& Record = State.AssetsByObjectPath.FindChecked(Key);
        if (!bIncludeRedirectors && Record.bRedirector)
        {
            continue;
        }
        if (!AssetRecordMatchesText(Record, TextQuery) || !AssetRecordMatchesClasses(Record, ClassNames) || !AssetRecordMatchesPaths(Record, PackagePaths, bRecursive))
        {
            continue;
        }
        Matches.Add(&Record);
    }

    TMap<FString, int32> ClassDict;
    TArray<FString> Classes;
    TArray<FString> FolderDictItems;
    TMap<FString, int32> FolderDict;
    TArray<FString> AssetRows;
    const int32 Start = FMath::Clamp(Offset, 0, Matches.Num());
    const int32 End = FMath::Min(Start + Limit, Matches.Num());
    for (int32 Index = Start; Index < End; ++Index)
    {
        const FAutoIndexAssetRecord* Record = Matches[Index];
        const int32 ClassIndex = DictIndex(ClassDict, Classes, Record->ClassName);
        const int32 FolderIndex = DictIndex(FolderDict, FolderDictItems, Record->PackagePath);
        AssetRows.Add(FString::Printf(
            TEXT("%d:%s;c=%d;f=%d;n=%s;r=%d;m=%s"),
            Index,
            *EscapeIndexedToken(Record->ObjectPath),
            ClassIndex,
            FolderIndex,
            *EscapeIndexedToken(Record->AssetName),
            Record->bRedirector ? 1 : 0,
            *EscapeIndexedToken(Record->ModifiedUtc)));
    }

    FString ResultText = FString::Printf(TEXT("Q:text=%s|count=%d|total=%d|cursor=%d\n"), *EscapeIndexedToken(TextQuery), End - Start, Matches.Num(), Start);
    ResultText += JoinDictionaryLine(TEXT("C:"), Classes);
    ResultText += JoinDictionaryLine(TEXT("F:"), FolderDictItems);
    ResultText += TEXT("A:") + FString::Join(AssetRows, TEXT("|")) + TEXT("\n");

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("auto_index_query_indexed"));
    Data->SetNumberField(TEXT("total"), Matches.Num());
    Data->SetNumberField(TEXT("count"), End - Start);
    Data->SetStringField(TEXT("cursor"), FString::FromInt(Start));
    Data->SetStringField(TEXT("next_cursor"), End < Matches.Num() ? FString::FromInt(End) : FString());
    Data->SetBoolField(TEXT("truncated"), End < Matches.Num());
    SetTextPayload(Data, ResultText);
    return Data;
}

TSharedPtr<FJsonObject> BuildResolveData(const TSharedPtr<FJsonObject>& Payload)
{
    const FAutoIndexState& State = AutoIndex();
    FString Input;
    Payload->TryGetStringField(TEXT("path"), Input);
    if (Input.IsEmpty())
    {
        Payload->TryGetStringField(TEXT("text"), Input);
    }

    const int32 Limit = ReadLimit(Payload, 20, 100);
    TArray<const FAutoIndexAssetRecord*> Exact;
    TArray<const FAutoIndexAssetRecord*> Partial;
    TArray<FString> Keys = SortedAssetKeys();
    for (const FString& Key : Keys)
    {
        const FAutoIndexAssetRecord& Record = State.AssetsByObjectPath.FindChecked(Key);
        if (Record.ObjectPath.Equals(Input, ESearchCase::IgnoreCase) || Record.PackageName.Equals(Input, ESearchCase::IgnoreCase) || Record.AssetName.Equals(Input, ESearchCase::IgnoreCase))
        {
            Exact.Add(&Record);
        }
        else if (AssetRecordMatchesText(Record, Input))
        {
            Partial.Add(&Record);
        }
    }

    const TArray<const FAutoIndexAssetRecord*>& Source = Exact.Num() > 0 ? Exact : Partial;
    TMap<FString, int32> ClassDict;
    TArray<FString> Classes;
    TArray<FString> Rows;
    const int32 Returned = FMath::Min(Limit, Source.Num());
    for (int32 Index = 0; Index < Returned; ++Index)
    {
        const FAutoIndexAssetRecord* Record = Source[Index];
        const int32 ClassIndex = DictIndex(ClassDict, Classes, Record->ClassName);
        Rows.Add(FString::Printf(TEXT("%d:%s;c=%d;n=%s;f=%s"), Index, *EscapeIndexedToken(Record->ObjectPath), ClassIndex, *EscapeIndexedToken(Record->AssetName), *EscapeIndexedToken(Record->PackagePath)));
    }

    FString Text = FString::Printf(TEXT("R:input=%s|exact=%d|candidates=%d|returned=%d\n"), *EscapeIndexedToken(Input), Exact.Num(), Source.Num(), Returned);
    Text += JoinDictionaryLine(TEXT("C:"), Classes);
    Text += TEXT("A:") + FString::Join(Rows, TEXT("|")) + TEXT("\n");

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("auto_index_resolve_indexed"));
    Data->SetStringField(TEXT("input"), Input);
    Data->SetStringField(TEXT("resolved_asset_path"), Source.Num() == 1 ? Source[0]->ObjectPath : FString());
    Data->SetNumberField(TEXT("candidate_count"), Source.Num());
    Data->SetNumberField(TEXT("returned_count"), Returned);
    Data->SetBoolField(TEXT("unique"), Source.Num() == 1);
    Data->SetBoolField(TEXT("truncated"), Returned < Source.Num());
    SetTextPayload(Data, Text);
    return Data;
}

TSharedPtr<FJsonObject> BuildDiffData()
{
    const FAutoIndexState& State = AutoIndex();
    TSet<FString> RegistryObjectPaths;
    TArray<FAssetData> RegistryAssets;
    FAssetRegistryModule::GetRegistry().GetAllAssets(RegistryAssets, true);
    for (const FAssetData& AssetData : RegistryAssets)
    {
        if (State.IsIndexedAsset(AssetData))
        {
            RegistryObjectPaths.Add(AssetData.GetObjectPathString());
        }
    }

    TArray<FString> MissingInIndex;
    TArray<FString> MissingInRegistry;
    TArray<FString> MissingDisk;
    for (const FString& ObjectPath : RegistryObjectPaths)
    {
        if (!State.AssetsByObjectPath.Contains(ObjectPath))
        {
            MissingInIndex.Add(ObjectPath);
        }
    }
    for (const TPair<FString, FAutoIndexAssetRecord>& Pair : State.AssetsByObjectPath)
    {
        if (!RegistryObjectPaths.Contains(Pair.Key))
        {
            MissingInRegistry.Add(Pair.Key);
        }
        if (Pair.Value.DiskFilename.IsEmpty() || !FPaths::FileExists(Pair.Value.DiskFilename))
        {
            MissingDisk.Add(Pair.Key);
        }
    }
    MissingInIndex.Sort();
    MissingInRegistry.Sort();
    MissingDisk.Sort();

    auto LimitJoin = [](const TArray<FString>& Values)
    {
        TArray<FString> Limited;
        for (int32 Index = 0; Index < Values.Num() && Index < 20; ++Index)
        {
            Limited.Add(EscapeIndexedToken(Values[Index]));
        }
        return FString::Join(Limited, TEXT(";"));
    };

    FString Text = FString::Printf(
        TEXT("D:registry=%d|index=%d|missing_index=%d|missing_registry=%d|missing_disk=%d\nMI:%s\nMR:%s\nMD:%s\n"),
        RegistryObjectPaths.Num(),
        State.AssetsByObjectPath.Num(),
        MissingInIndex.Num(),
        MissingInRegistry.Num(),
        MissingDisk.Num(),
        *LimitJoin(MissingInIndex),
        *LimitJoin(MissingInRegistry),
        *LimitJoin(MissingDisk));

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("auto_index_diff_registry_indexed"));
    Data->SetNumberField(TEXT("registry_count"), RegistryObjectPaths.Num());
    Data->SetNumberField(TEXT("index_count"), State.AssetsByObjectPath.Num());
    Data->SetNumberField(TEXT("missing_in_index_count"), MissingInIndex.Num());
    Data->SetNumberField(TEXT("missing_in_registry_count"), MissingInRegistry.Num());
    Data->SetNumberField(TEXT("missing_disk_count"), MissingDisk.Num());
    Data->SetBoolField(TEXT("ok"), MissingInIndex.Num() == 0 && MissingInRegistry.Num() == 0 && MissingDisk.Num() == 0);
    SetTextPayload(Data, Text);
    return Data;
}
