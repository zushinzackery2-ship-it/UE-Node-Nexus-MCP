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
};

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
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(Code, Message));
    return Response;
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
}

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
}
