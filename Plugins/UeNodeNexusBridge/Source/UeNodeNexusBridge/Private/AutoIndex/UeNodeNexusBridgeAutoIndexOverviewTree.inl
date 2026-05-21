TSharedPtr<FJsonObject> BuildOverviewData(const TSharedPtr<FJsonObject>& Payload)
{
    FAutoIndexState& State = AutoIndex();
    const int32 Limit = ReadLimit(Payload, 30, 200);

    TMap<FString, int32> TotalClassCounts;
    for (const TPair<FString, FAutoIndexAssetRecord>& Pair : State.AssetsByObjectPath)
    {
        ++TotalClassCounts.FindOrAdd(Pair.Value.ClassName);
    }

    TArray<const FAutoIndexFolderRecord*> TopFolders;
    for (const TPair<FString, FAutoIndexFolderRecord>& Pair : State.FoldersByPath)
    {
        const FAutoIndexFolderRecord& Folder = Pair.Value;
        if (Folder.ParentPath.Equals(State.RootPath, ESearchCase::IgnoreCase) || Folder.FolderPath.Equals(State.RootPath, ESearchCase::IgnoreCase))
        {
            TopFolders.Add(&Folder);
        }
    }
    TopFolders.Sort([](const FAutoIndexFolderRecord& Left, const FAutoIndexFolderRecord& Right)
    {
        if (Left.RecursiveAssetCount != Right.RecursiveAssetCount)
        {
            return Left.RecursiveAssetCount > Right.RecursiveAssetCount;
        }
        return Left.FolderPath < Right.FolderPath;
    });

    TMap<FString, int32> ClassDict;
    TArray<FString> Classes;
    TArray<FString> ClassRows;
    TArray<FString> FolderRows;
    for (const TPair<FString, int32>& Pair : TotalClassCounts)
    {
        DictIndex(ClassDict, Classes, Pair.Key);
    }
    Classes.Sort();
    ClassDict.Reset();
    for (int32 Index = 0; Index < Classes.Num(); ++Index)
    {
        ClassDict.Add(Classes[Index], Index);
        ClassRows.Add(FString::Printf(TEXT("%d=%s:%d"), Index, *EscapeIndexedToken(Classes[Index]), TotalClassCounts.FindRef(Classes[Index])));
    }

    const int32 ReturnedFolders = FMath::Min(Limit, TopFolders.Num());
    for (int32 Index = 0; Index < ReturnedFolders; ++Index)
    {
        const FAutoIndexFolderRecord* Folder = TopFolders[Index];
        FolderRows.Add(FString::Printf(
            TEXT("%d:%s;a=%d;r=%d;c=%s;m=%s;s=%s"),
            Index,
            *EscapeIndexedToken(Folder->FolderPath),
            Folder->DirectAssetCount,
            Folder->RecursiveAssetCount,
            *ClassCountsToken(Folder->RecursiveClassCounts, ClassDict, Classes),
            *EscapeIndexedToken(Folder->LatestModifiedUtc),
            *EscapeIndexedToken(FString::Join(Folder->Samples, TEXT(",")))));
    }

    FString Text = FString::Printf(
        TEXT("I:%s|assets=%d|folders=%d|enabled=%d|dirty=%d|serial=%d\n"),
        *EscapeIndexedToken(State.RootPath),
        State.AssetsByObjectPath.Num(),
        State.FoldersByPath.Num(),
        State.bEnabled ? 1 : 0,
        State.bDirty ? 1 : 0,
        State.ChangeSerial);
    Text += TEXT("C:") + FString::Join(ClassRows, TEXT(";")) + TEXT("\n");
    Text += TEXT("F:") + FString::Join(FolderRows, TEXT("|")) + TEXT("\n");

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("auto_index_overview_indexed"));
    Data->SetStringField(TEXT("root_path"), State.RootPath);
    Data->SetNumberField(TEXT("total_assets"), State.AssetsByObjectPath.Num());
    Data->SetNumberField(TEXT("total_folders"), State.FoldersByPath.Num());
    Data->SetNumberField(TEXT("returned_folders"), ReturnedFolders);
    Data->SetBoolField(TEXT("truncated"), ReturnedFolders < TopFolders.Num());
    SetTextPayload(Data, Text);
    return Data;
}

TSharedPtr<FJsonObject> BuildTreeData(const TSharedPtr<FJsonObject>& Payload)
{
    const FAutoIndexState& State = AutoIndex();
    FString RootPath = State.RootPath;
    Payload->TryGetStringField(TEXT("root_path"), RootPath);
    RootPath.RemoveFromEnd(TEXT("/"));

    double DepthNumber = 2.0;
    Payload->TryGetNumberField(TEXT("depth"), DepthNumber);
    const int32 Depth = FMath::Clamp(static_cast<int32>(DepthNumber), 0, 8);
    const int32 Limit = ReadLimit(Payload, 120, 1000);

    TArray<const FAutoIndexFolderRecord*> Folders;
    for (const TPair<FString, FAutoIndexFolderRecord>& Pair : State.FoldersByPath)
    {
        const FAutoIndexFolderRecord& Folder = Pair.Value;
        if (!Folder.FolderPath.Equals(RootPath, ESearchCase::IgnoreCase) && !Folder.FolderPath.StartsWith(RootPath + TEXT("/"), ESearchCase::IgnoreCase))
        {
            continue;
        }
        TArray<FString> Parts;
        const FString Relative = Folder.FolderPath.Equals(RootPath) ? FString() : Folder.FolderPath.RightChop(RootPath.Len() + 1);
        Relative.ParseIntoArray(Parts, TEXT("/"), true);
        if (Parts.Num() <= Depth)
        {
            Folders.Add(&Folder);
        }
    }
    Folders.Sort([](const FAutoIndexFolderRecord& Left, const FAutoIndexFolderRecord& Right)
    {
        return Left.FolderPath < Right.FolderPath;
    });

    TMap<FString, int32> FolderDict;
    for (int32 Index = 0; Index < Folders.Num(); ++Index)
    {
        FolderDict.Add(Folders[Index]->FolderPath, Index);
    }

    TMap<FString, int32> ClassDict;
    TArray<FString> Classes;
    TArray<FString> FolderRows;
    TArray<FString> SampleRows;
    const int32 Returned = FMath::Min(Limit, Folders.Num());
    for (int32 Index = 0; Index < Returned; ++Index)
    {
        const FAutoIndexFolderRecord* Folder = Folders[Index];
        const int32 ParentIndex = FolderDict.FindRef(Folder->ParentPath);
        FolderRows.Add(FString::Printf(
            TEXT("%d:%s;p=%d;a=%d;r=%d;c=%s"),
            Index,
            *EscapeIndexedToken(Folder->FolderPath),
            Folder->ParentPath.IsEmpty() ? -1 : ParentIndex,
            Folder->DirectAssetCount,
            Folder->RecursiveAssetCount,
            *ClassCountsToken(Folder->RecursiveClassCounts, ClassDict, Classes)));
        if (Folder->Samples.Num() > 0)
        {
            SampleRows.Add(FString::Printf(TEXT("%d=%s"), Index, *EscapeIndexedToken(FString::Join(Folder->Samples, TEXT(",")))));
        }
    }

    FString Text = FString::Printf(TEXT("T:%s|depth=%d|folders=%d|assets=%d\n"), *EscapeIndexedToken(RootPath), Depth, State.FoldersByPath.Num(), State.AssetsByObjectPath.Num());
    Text += JoinDictionaryLine(TEXT("C:"), Classes);
    Text += TEXT("D:") + FString::Join(FolderRows, TEXT("|")) + TEXT("\n");
    Text += TEXT("S:") + FString::Join(SampleRows, TEXT(";")) + TEXT("\n");

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("auto_index_tree_indexed"));
    Data->SetStringField(TEXT("root_path"), RootPath);
    Data->SetNumberField(TEXT("depth"), Depth);
    Data->SetNumberField(TEXT("total_folders"), Folders.Num());
    Data->SetNumberField(TEXT("returned_folders"), Returned);
    Data->SetBoolField(TEXT("truncated"), Returned < Folders.Num());
    SetTextPayload(Data, Text);
    return Data;
}
