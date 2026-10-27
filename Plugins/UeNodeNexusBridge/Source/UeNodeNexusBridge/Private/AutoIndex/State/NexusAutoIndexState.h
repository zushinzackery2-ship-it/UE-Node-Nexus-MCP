#pragma once

#include "CoreMinimal.h"

struct FAssetData;
class FJsonObject;

namespace UeNodeNexusBridge::AutoIndex
{
inline constexpr int32 Version = 1;

struct FAssetRecord
{
    FString ObjectPath;
    FString PackageName;
    FString PackagePath;
    FString AssetName;
    FString ClassPath;
    FString ClassName;
    FString DiskFilename;
    FString ModifiedUtc;
    bool bLoaded = false;
    bool bRedirector = false;
};

struct FFolderRecord
{
    FString FolderPath;
    FString ParentPath;
    FString DiskDirectory;
    int32 DirectAssetCount = 0;
    int32 RecursiveAssetCount = 0;
    TMap<FString, int32> DirectClassCounts;
    TMap<FString, int32> RecursiveClassCounts;
    TArray<FString> Samples;
    FString LatestModifiedUtc;
    bool bDiskFolder = false;
};

// Assets and folders beneath RootPath. While enabled the records follow the asset
// registry; they persist to Saved/UeNodeNexusBridge/AutoIndex.json.
class FState
{
public:
    // The editor-lifetime index every auto_index operation reads and updates.
    static FState& Get();

    bool bEnabled = false;
    bool bLoadedFromDisk = false;
    bool bDirty = false;
    int32 ChangeSerial = 0;
    FString LastBuildUtc;
    FString LastFlushUtc;
    FString LastEventUtc;
    FString RootPath = TEXT("/Game");
    TMap<FString, FAssetRecord> AssetsByObjectPath;
    TMap<FString, FFolderRecord> FoldersByPath;

    // NexusAutoIndexRegistry.cpp: registry listeners and rebuilds.
    FString IndexPath() const;
    bool IsListening() const;
    void MarkDirty();
    void RegisterListeners();
    void UnregisterListeners();
    bool IsIndexedAsset(const FAssetData& AssetData) const;
    void RebuildFromRegistry();

    // NexusAutoIndexPersistence.cpp: the JSON file and its records.
    TSharedPtr<FJsonObject> AssetRecordToJson(const FAssetRecord& Record) const;
    bool LoadFromDisk();
    bool FlushToDisk();
    void Clear(bool bDeleteFile);

private:
    FDelegateHandle AssetAddedHandle;
    FDelegateHandle AssetRemovedHandle;
    FDelegateHandle AssetRenamedHandle;
    FDelegateHandle AssetUpdatedHandle;
    FDelegateHandle FilesLoadedHandle;

    void OnAssetAdded(const FAssetData& AssetData);
    void OnAssetRemoved(const FAssetData& AssetData);
    void OnAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath);
    void OnAssetUpdated(const FAssetData& AssetData);
    void OnFilesLoaded();

    // NexusAutoIndexFolders.cpp: asset records and the folder tree derived from them.
    FAssetRecord MakeRecord(const FAssetData& AssetData) const;
    void RebuildFolders();
    FFolderRecord& EnsureFolder(const FString& FolderPath);
    FString PackageNameToDiskFilename(const FString& PackageName) const;
    FString PackageFolderToDirectory(const FString& FolderPath) const;
    FString DirectoryToPackageFolder(const FString& Directory) const;
    void AddAssetToFolderTree(const FAssetRecord& Record);
    void AddDiskFolders();

    TSharedPtr<FJsonObject> FolderRecordToJson(const FFolderRecord& Record) const;
};
}
