    FString IndexPath() const
    {
        return FPaths::ProjectSavedDir() / TEXT("UeNodeNexusBridge") / TEXT("AutoIndex.json");
    }

    bool IsListening() const
    {
        return AssetAddedHandle.IsValid()
            || AssetRemovedHandle.IsValid()
            || AssetRenamedHandle.IsValid()
            || AssetUpdatedHandle.IsValid()
            || FilesLoadedHandle.IsValid();
    }

    void MarkDirty()
    {
        bDirty = true;
        ++ChangeSerial;
        LastEventUtc = FDateTime::UtcNow().ToIso8601();
    }

    void RegisterListeners()
    {
        if (IsListening())
        {
            return;
        }

        FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName);
        IAssetRegistry* Registry = AssetRegistryModule.TryGet();
        if (Registry == nullptr)
        {
            return;
        }

        AssetAddedHandle = Registry->OnAssetAdded().AddRaw(this, &FAutoIndexState::OnAssetAdded);
        AssetRemovedHandle = Registry->OnAssetRemoved().AddRaw(this, &FAutoIndexState::OnAssetRemoved);
        AssetRenamedHandle = Registry->OnAssetRenamed().AddRaw(this, &FAutoIndexState::OnAssetRenamed);
        AssetUpdatedHandle = Registry->OnAssetUpdated().AddRaw(this, &FAutoIndexState::OnAssetUpdated);
        FilesLoadedHandle = Registry->OnFilesLoaded().AddRaw(this, &FAutoIndexState::OnFilesLoaded);
    }

    void UnregisterListeners()
    {
        FAssetRegistryModule* AssetRegistryModule = FModuleManager::Get().GetModulePtr<FAssetRegistryModule>(AssetRegistryConstants::ModuleName);
        IAssetRegistry* Registry = AssetRegistryModule ? AssetRegistryModule->TryGet() : nullptr;
        if (Registry == nullptr)
        {
            AssetAddedHandle.Reset();
            AssetRemovedHandle.Reset();
            AssetRenamedHandle.Reset();
            AssetUpdatedHandle.Reset();
            FilesLoadedHandle.Reset();
            return;
        }

        if (AssetAddedHandle.IsValid())
        {
            Registry->OnAssetAdded().Remove(AssetAddedHandle);
            AssetAddedHandle.Reset();
        }
        if (AssetRemovedHandle.IsValid())
        {
            Registry->OnAssetRemoved().Remove(AssetRemovedHandle);
            AssetRemovedHandle.Reset();
        }
        if (AssetRenamedHandle.IsValid())
        {
            Registry->OnAssetRenamed().Remove(AssetRenamedHandle);
            AssetRenamedHandle.Reset();
        }
        if (AssetUpdatedHandle.IsValid())
        {
            Registry->OnAssetUpdated().Remove(AssetUpdatedHandle);
            AssetUpdatedHandle.Reset();
        }
        if (FilesLoadedHandle.IsValid())
        {
            Registry->OnFilesLoaded().Remove(FilesLoadedHandle);
            FilesLoadedHandle.Reset();
        }
    }

    bool IsIndexedAsset(const FAssetData& AssetData) const
    {
        const FString PackagePath = AssetData.PackagePath.ToString();
        return PackagePath.Equals(RootPath, ESearchCase::IgnoreCase)
            || PackagePath.StartsWith(RootPath + TEXT("/"), ESearchCase::IgnoreCase);
    }

    void RebuildFolders()
    {
        FoldersByPath.Reset();
        EnsureFolder(RootPath);
        for (const TPair<FString, FAutoIndexAssetRecord>& Pair : AssetsByObjectPath)
        {
            AddAssetToFolderTree(Pair.Value);
        }
        AddDiskFolders();
    }

    void RebuildFromRegistry()
    {
        AssetsByObjectPath.Reset();

        TArray<FAssetData> Assets;
        FAssetRegistryModule::GetRegistry().GetAllAssets(Assets, true);
        for (const FAssetData& AssetData : Assets)
        {
            if (!IsIndexedAsset(AssetData))
            {
                continue;
            }
            FAutoIndexAssetRecord Record = MakeRecord(AssetData);
            AssetsByObjectPath.Add(Record.ObjectPath, Record);
        }

        RebuildFolders();
        LastBuildUtc = FDateTime::UtcNow().ToIso8601();
        MarkDirty();
    }
