    TSharedPtr<FJsonObject> AssetRecordToJson(const FAutoIndexAssetRecord& Record) const
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("object_path"), Record.ObjectPath);
        Json->SetStringField(TEXT("package_name"), Record.PackageName);
        Json->SetStringField(TEXT("package_path"), Record.PackagePath);
        Json->SetStringField(TEXT("asset_name"), Record.AssetName);
        Json->SetStringField(TEXT("asset_class_path"), Record.ClassPath);
        Json->SetStringField(TEXT("asset_class"), Record.ClassName);
        Json->SetStringField(TEXT("disk_filename"), Record.DiskFilename);
        Json->SetStringField(TEXT("modified_utc"), Record.ModifiedUtc);
        Json->SetBoolField(TEXT("loaded"), Record.bLoaded);
        Json->SetBoolField(TEXT("redirector"), Record.bRedirector);
        return Json;
    }

    TSharedPtr<FJsonObject> FolderRecordToJson(const FAutoIndexFolderRecord& Record) const
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("folder_path"), Record.FolderPath);
        Json->SetStringField(TEXT("parent_path"), Record.ParentPath);
        Json->SetStringField(TEXT("disk_directory"), Record.DiskDirectory);
        Json->SetNumberField(TEXT("direct_asset_count"), Record.DirectAssetCount);
        Json->SetNumberField(TEXT("recursive_asset_count"), Record.RecursiveAssetCount);
        Json->SetStringField(TEXT("latest_modified_utc"), Record.LatestModifiedUtc);
        Json->SetBoolField(TEXT("disk_folder"), Record.bDiskFolder);
        Json->SetArrayField(TEXT("samples"), StringsToJson(Record.Samples));
        Json->SetObjectField(TEXT("direct_class_counts"), MapToJson(Record.DirectClassCounts));
        Json->SetObjectField(TEXT("recursive_class_counts"), MapToJson(Record.RecursiveClassCounts));
        return Json;
    }

    bool LoadFromDisk()
    {
        FString JsonText;
        if (!FFileHelper::LoadFileToString(JsonText, *IndexPath()))
        {
            return false;
        }

        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            return false;
        }

        bEnabled = Root->GetBoolField(TEXT("enabled"));
        Root->TryGetStringField(TEXT("root_path"), RootPath);
        Root->TryGetStringField(TEXT("last_build_utc"), LastBuildUtc);
        Root->TryGetStringField(TEXT("last_flush_utc"), LastFlushUtc);

        AssetsByObjectPath.Reset();
        const TArray<TSharedPtr<FJsonValue>>* AssetValues = nullptr;
        if (Root->TryGetArrayField(TEXT("assets"), AssetValues))
        {
            for (const TSharedPtr<FJsonValue>& Value : *AssetValues)
            {
                const TSharedPtr<FJsonObject> Json = Value->AsObject();
                if (!Json.IsValid())
                {
                    continue;
                }
                FAutoIndexAssetRecord Record;
                Json->TryGetStringField(TEXT("object_path"), Record.ObjectPath);
                Json->TryGetStringField(TEXT("package_name"), Record.PackageName);
                Json->TryGetStringField(TEXT("package_path"), Record.PackagePath);
                Json->TryGetStringField(TEXT("asset_name"), Record.AssetName);
                Json->TryGetStringField(TEXT("asset_class_path"), Record.ClassPath);
                Json->TryGetStringField(TEXT("asset_class"), Record.ClassName);
                Json->TryGetStringField(TEXT("disk_filename"), Record.DiskFilename);
                Json->TryGetStringField(TEXT("modified_utc"), Record.ModifiedUtc);
                Json->TryGetBoolField(TEXT("loaded"), Record.bLoaded);
                Json->TryGetBoolField(TEXT("redirector"), Record.bRedirector);
                if (!Record.ObjectPath.IsEmpty())
                {
                    AssetsByObjectPath.Add(Record.ObjectPath, Record);
                }
            }
        }

        RebuildFolders();
        bLoadedFromDisk = true;
        bDirty = false;
        return true;
    }

    bool FlushToDisk()
    {
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(IndexPath()), true);

        TArray<TSharedPtr<FJsonValue>> AssetValues;
        TArray<FAutoIndexAssetRecord> Records;
        AssetsByObjectPath.GenerateValueArray(Records);
        Records.Sort([](const FAutoIndexAssetRecord& Left, const FAutoIndexAssetRecord& Right)
        {
            return Left.ObjectPath < Right.ObjectPath;
        });
        for (const FAutoIndexAssetRecord& Record : Records)
        {
            AssetValues.Add(MakeShared<FJsonValueObject>(AssetRecordToJson(Record)));
        }

        TArray<TSharedPtr<FJsonValue>> FolderValues;
        TArray<FAutoIndexFolderRecord> FolderRecords;
        FoldersByPath.GenerateValueArray(FolderRecords);
        FolderRecords.Sort([](const FAutoIndexFolderRecord& Left, const FAutoIndexFolderRecord& Right)
        {
            return Left.FolderPath < Right.FolderPath;
        });
        for (const FAutoIndexFolderRecord& Record : FolderRecords)
        {
            FolderValues.Add(MakeShared<FJsonValueObject>(FolderRecordToJson(Record)));
        }

        LastFlushUtc = FDateTime::UtcNow().ToIso8601();

        TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetNumberField(TEXT("version"), AutoIndexVersion);
        Root->SetBoolField(TEXT("enabled"), bEnabled);
        Root->SetStringField(TEXT("root_path"), RootPath);
        Root->SetStringField(TEXT("last_build_utc"), LastBuildUtc);
        Root->SetStringField(TEXT("last_flush_utc"), LastFlushUtc);
        Root->SetArrayField(TEXT("assets"), AssetValues);
        Root->SetArrayField(TEXT("folders"), FolderValues);

        FString Output;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
        {
            return false;
        }
        if (!FFileHelper::SaveStringToFile(Output, *IndexPath()))
        {
            return false;
        }
        bDirty = false;
        return true;
    }

    void Clear(bool bDeleteFile)
    {
        AssetsByObjectPath.Reset();
        FoldersByPath.Reset();
        EnsureFolder(RootPath);
        bDirty = false;
        LastBuildUtc.Reset();
        LastFlushUtc.Reset();
        LastEventUtc.Reset();
        ++ChangeSerial;
        if (bDeleteFile)
        {
            IFileManager::Get().Delete(*IndexPath(), false, true);
        }
    }

    static TSharedPtr<FJsonObject> MapToJson(const TMap<FString, int32>& Map)
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        TArray<FString> Keys;
        Map.GetKeys(Keys);
        Keys.Sort();
        for (const FString& Key : Keys)
        {
            Json->SetNumberField(Key, Map.FindChecked(Key));
        }
        return Json;
    }

    static TArray<TSharedPtr<FJsonValue>> StringsToJson(const TArray<FString>& Strings)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        for (const FString& String : Strings)
        {
            Values.Add(MakeShared<FJsonValueString>(String));
        }
        return Values;
    }
