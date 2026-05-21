#include "UeNodeNexusBridgeAutoIndex.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusBridgeGraphIndexedInfoOps.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
namespace
{
constexpr int32 AutoIndexVersion = 1;

struct FAutoIndexAssetRecord
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

struct FAutoIndexFolderRecord
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

class FAutoIndexState
{
public:
    bool bEnabled = false;
    bool bLoadedFromDisk = false;
    bool bDirty = false;
    int32 ChangeSerial = 0;
    FString LastBuildUtc;
    FString LastFlushUtc;
    FString LastEventUtc;
    FString RootPath = TEXT("/Game");
    TMap<FString, FAutoIndexAssetRecord> AssetsByObjectPath;
    TMap<FString, FAutoIndexFolderRecord> FoldersByPath;

    FDelegateHandle AssetAddedHandle;
    FDelegateHandle AssetRemovedHandle;
    FDelegateHandle AssetRenamedHandle;
    FDelegateHandle AssetUpdatedHandle;
    FDelegateHandle FilesLoadedHandle;

#include "UeNodeNexusBridgeAutoIndexStateRegistry.inl"
#include "UeNodeNexusBridgeAutoIndexStateFolders.inl"
#include "UeNodeNexusBridgeAutoIndexStatePersistence.inl"
};

#include "UeNodeNexusBridgeAutoIndexQueryCommon.inl"
#include "UeNodeNexusBridgeAutoIndexOverviewTree.inl"
#include "UeNodeNexusBridgeAutoIndexQueryResolve.inl"
}

#include "UeNodeNexusBridgeAutoIndexOperations.inl"
}
