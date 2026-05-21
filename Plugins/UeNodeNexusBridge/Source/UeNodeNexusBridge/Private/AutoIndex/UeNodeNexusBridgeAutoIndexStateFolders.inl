    FAutoIndexAssetRecord MakeRecord(const FAssetData& AssetData) const
    {
        FAutoIndexAssetRecord Record;
        Record.ObjectPath = AssetData.GetObjectPathString();
        Record.PackageName = AssetData.PackageName.ToString();
        Record.PackagePath = AssetData.PackagePath.ToString();
        Record.AssetName = AssetData.AssetName.ToString();
        Record.ClassPath = AssetData.AssetClassPath.ToString();
        Record.ClassName = AssetData.AssetClassPath.GetAssetName().ToString();
        Record.bLoaded = AssetData.IsAssetLoaded();
        Record.bRedirector = AssetData.IsRedirector();
        Record.DiskFilename = PackageNameToDiskFilename(Record.PackageName);
        Record.ModifiedUtc = FileModifiedUtc(Record.DiskFilename);
        return Record;
    }

    FString PackageNameToDiskFilename(const FString& PackageName) const
    {
        FString Filename;
        if (FPackageName::DoesPackageExist(PackageName, &Filename))
        {
            return Filename;
        }

        if (PackageName.StartsWith(TEXT("/Game/")))
        {
            const FString Relative = PackageName.RightChop(6);
            const FString UAssetPath = FPaths::ProjectContentDir() / Relative + TEXT(".uasset");
            const FString UMapPath = FPaths::ProjectContentDir() / Relative + TEXT(".umap");
            if (FPaths::FileExists(UAssetPath))
            {
                return UAssetPath;
            }
            if (FPaths::FileExists(UMapPath))
            {
                return UMapPath;
            }
        }
        return FString();
    }

    static FString FileModifiedUtc(const FString& Filename)
    {
        if (Filename.IsEmpty() || !FPaths::FileExists(Filename))
        {
            return FString();
        }
        return IFileManager::Get().GetTimeStamp(*Filename).ToIso8601();
    }

    FAutoIndexFolderRecord& EnsureFolder(const FString& FolderPath)
    {
        FAutoIndexFolderRecord* Existing = FoldersByPath.Find(FolderPath);
        if (Existing != nullptr)
        {
            return *Existing;
        }

        FAutoIndexFolderRecord Record;
        Record.FolderPath = FolderPath;
        Record.ParentPath = ParentFolder(FolderPath);
        Record.DiskDirectory = PackageFolderToDirectory(FolderPath);
        Record.bDiskFolder = !Record.DiskDirectory.IsEmpty() && IFileManager::Get().DirectoryExists(*Record.DiskDirectory);
        FoldersByPath.Add(FolderPath, Record);
        if (!Record.ParentPath.IsEmpty() && !Record.ParentPath.Equals(FolderPath))
        {
            EnsureFolder(Record.ParentPath);
        }
        return FoldersByPath.FindChecked(FolderPath);
    }

    FString PackageFolderToDirectory(const FString& FolderPath) const
    {
        if (FolderPath.Equals(TEXT("/Game"), ESearchCase::IgnoreCase))
        {
            return FPaths::ProjectContentDir();
        }
        if (!FolderPath.StartsWith(TEXT("/Game/"), ESearchCase::IgnoreCase))
        {
            return FString();
        }
        const FString Relative = FolderPath.RightChop(6);
        return FPaths::ProjectContentDir() / Relative;
    }

    FString DirectoryToPackageFolder(const FString& Directory) const
    {
        FString NormalizedDirectory = FPaths::ConvertRelativePathToFull(Directory);
        FString ContentDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        FPaths::NormalizeDirectoryName(NormalizedDirectory);
        FPaths::NormalizeDirectoryName(ContentDirectory);
        NormalizedDirectory.RemoveFromEnd(TEXT("/"));
        ContentDirectory.RemoveFromEnd(TEXT("/"));

        if (NormalizedDirectory.Equals(ContentDirectory, ESearchCase::IgnoreCase))
        {
            return TEXT("/Game");
        }
        if (!NormalizedDirectory.StartsWith(ContentDirectory + TEXT("/"), ESearchCase::IgnoreCase))
        {
            return FString();
        }

        FString Relative = NormalizedDirectory.RightChop(ContentDirectory.Len() + 1);
        Relative.ReplaceInline(TEXT("\\"), TEXT("/"));
        Relative.RemoveFromEnd(TEXT("/"));
        if (Relative.IsEmpty())
        {
            return TEXT("/Game");
        }
        return TEXT("/Game/") + Relative;
    }

    static FString ParentFolder(const FString& FolderPath)
    {
        if (FolderPath.IsEmpty() || FolderPath.Equals(TEXT("/Game")))
        {
            return FString();
        }

        int32 SlashIndex = INDEX_NONE;
        if (!FolderPath.FindLastChar(TEXT('/'), SlashIndex) || SlashIndex <= 0)
        {
            return FString();
        }
        return FolderPath.Left(SlashIndex);
    }

    void AddAssetToFolderTree(const FAutoIndexAssetRecord& Record)
    {
        FAutoIndexFolderRecord& DirectFolder = EnsureFolder(Record.PackagePath);
        ++DirectFolder.DirectAssetCount;
        ++DirectFolder.DirectClassCounts.FindOrAdd(Record.ClassName);
        AddFolderSample(DirectFolder, Record.AssetName);
        UpdateFolderLatest(DirectFolder, Record.ModifiedUtc);

        FString Current = Record.PackagePath;
        while (!Current.IsEmpty())
        {
            FAutoIndexFolderRecord& Folder = EnsureFolder(Current);
            ++Folder.RecursiveAssetCount;
            ++Folder.RecursiveClassCounts.FindOrAdd(Record.ClassName);
            AddFolderSample(Folder, Record.AssetName);
            UpdateFolderLatest(Folder, Record.ModifiedUtc);
            if (Current.Equals(RootPath, ESearchCase::IgnoreCase))
            {
                break;
            }
            Current = ParentFolder(Current);
        }
    }

    void AddDiskFolders()
    {
        const FString RootDirectory = FPaths::ConvertRelativePathToFull(PackageFolderToDirectory(RootPath));
        if (RootDirectory.IsEmpty() || !IFileManager::Get().DirectoryExists(*RootDirectory))
        {
            return;
        }

        FAutoIndexFolderRecord& RootFolder = EnsureFolder(RootPath);
        RootFolder.DiskDirectory = RootDirectory;
        RootFolder.bDiskFolder = true;

        struct FDirectoryCollector : public IPlatformFile::FDirectoryVisitor
        {
            TArray<FString>& Directories;

            explicit FDirectoryCollector(TArray<FString>& InDirectories)
                : Directories(InDirectories)
            {
            }

            virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
            {
                if (bIsDirectory)
                {
                    Directories.Add(FString(FilenameOrDirectory));
                }
                return true;
            }
        };

        TArray<FString> Directories;
        FDirectoryCollector Collector(Directories);
        FPlatformFileManager::Get().GetPlatformFile().IterateDirectoryRecursively(*RootDirectory, Collector);
        for (const FString& Directory : Directories)
        {
            const FString FolderPath = DirectoryToPackageFolder(Directory);
            if (FolderPath.IsEmpty())
            {
                continue;
            }
            if (!FolderPath.Equals(RootPath, ESearchCase::IgnoreCase) && !FolderPath.StartsWith(RootPath + TEXT("/"), ESearchCase::IgnoreCase))
            {
                continue;
            }
            FAutoIndexFolderRecord& Folder = EnsureFolder(FolderPath);
            Folder.DiskDirectory = Directory;
            Folder.bDiskFolder = true;
        }
    }

    static void AddFolderSample(FAutoIndexFolderRecord& Folder, const FString& AssetName)
    {
        if (Folder.Samples.Num() >= 4 || AssetName.IsEmpty())
        {
            return;
        }
        Folder.Samples.AddUnique(AssetName);
    }

    static void UpdateFolderLatest(FAutoIndexFolderRecord& Folder, const FString& ModifiedUtc)
    {
        if (!ModifiedUtc.IsEmpty() && (Folder.LatestModifiedUtc.IsEmpty() || ModifiedUtc > Folder.LatestModifiedUtc))
        {
            Folder.LatestModifiedUtc = ModifiedUtc;
        }
    }

    void OnAssetAdded(const FAssetData& AssetData)
    {
        if (!bEnabled || !IsIndexedAsset(AssetData))
        {
            return;
        }
        const FAutoIndexAssetRecord Record = MakeRecord(AssetData);
        AssetsByObjectPath.Add(Record.ObjectPath, Record);
        RebuildFolders();
        MarkDirty();
    }

    void OnAssetRemoved(const FAssetData& AssetData)
    {
        if (!bEnabled)
        {
            return;
        }
        AssetsByObjectPath.Remove(AssetData.GetObjectPathString());
        RebuildFolders();
        MarkDirty();
    }

    void OnAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath)
    {
        if (!bEnabled)
        {
            return;
        }
        AssetsByObjectPath.Remove(OldObjectPath);
        if (IsIndexedAsset(AssetData))
        {
            const FAutoIndexAssetRecord Record = MakeRecord(AssetData);
            AssetsByObjectPath.Add(Record.ObjectPath, Record);
        }
        RebuildFolders();
        MarkDirty();
    }

    void OnAssetUpdated(const FAssetData& AssetData)
    {
        if (!bEnabled)
        {
            return;
        }
        if (IsIndexedAsset(AssetData))
        {
            const FAutoIndexAssetRecord Record = MakeRecord(AssetData);
            AssetsByObjectPath.Add(Record.ObjectPath, Record);
        }
        RebuildFolders();
        MarkDirty();
    }

    void OnFilesLoaded()
    {
        if (!bEnabled)
        {
            return;
        }
        RebuildFromRegistry();
    }
