#include "AutoIndex/State/NexusAutoIndexState.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace UeNodeNexusBridge::AutoIndex
{
FState& FState::Get()
{
    static FState Instance;
    return Instance;
}

FString FState::IndexPath() const
{
    return FPaths::ProjectSavedDir() / TEXT("UeNodeNexusBridge") / TEXT("AutoIndex.json");
}

bool FState::IsListening() const
{
    return AssetAddedHandle.IsValid()
        || AssetRemovedHandle.IsValid()
        || AssetRenamedHandle.IsValid()
        || AssetUpdatedHandle.IsValid()
        || FilesLoadedHandle.IsValid();
}

void FState::MarkDirty()
{
    bDirty = true;
    ++ChangeSerial;
    LastEventUtc = FDateTime::UtcNow().ToIso8601();
}

void FState::RegisterListeners()
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

    AssetAddedHandle = Registry->OnAssetAdded().AddRaw(this, &FState::OnAssetAdded);
    AssetRemovedHandle = Registry->OnAssetRemoved().AddRaw(this, &FState::OnAssetRemoved);
    AssetRenamedHandle = Registry->OnAssetRenamed().AddRaw(this, &FState::OnAssetRenamed);
    AssetUpdatedHandle = Registry->OnAssetUpdated().AddRaw(this, &FState::OnAssetUpdated);
    FilesLoadedHandle = Registry->OnFilesLoaded().AddRaw(this, &FState::OnFilesLoaded);
}

void FState::UnregisterListeners()
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

bool FState::IsIndexedAsset(const FAssetData& AssetData) const
{
    const FString PackagePath = AssetData.PackagePath.ToString();
    return PackagePath.Equals(RootPath, ESearchCase::IgnoreCase)
        || PackagePath.StartsWith(RootPath + TEXT("/"), ESearchCase::IgnoreCase);
}

void FState::RebuildFromRegistry()
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
        FAssetRecord Record = MakeRecord(AssetData);
        AssetsByObjectPath.Add(Record.ObjectPath, Record);
    }

    RebuildFolders();
    LastBuildUtc = FDateTime::UtcNow().ToIso8601();
    MarkDirty();
}

void FState::OnAssetAdded(const FAssetData& AssetData)
{
    if (!bEnabled || !IsIndexedAsset(AssetData))
    {
        return;
    }
    const FAssetRecord Record = MakeRecord(AssetData);
    AssetsByObjectPath.Add(Record.ObjectPath, Record);
    RebuildFolders();
    MarkDirty();
}

void FState::OnAssetRemoved(const FAssetData& AssetData)
{
    if (!bEnabled)
    {
        return;
    }
    AssetsByObjectPath.Remove(AssetData.GetObjectPathString());
    RebuildFolders();
    MarkDirty();
}

void FState::OnAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath)
{
    if (!bEnabled)
    {
        return;
    }
    AssetsByObjectPath.Remove(OldObjectPath);
    if (IsIndexedAsset(AssetData))
    {
        const FAssetRecord Record = MakeRecord(AssetData);
        AssetsByObjectPath.Add(Record.ObjectPath, Record);
    }
    RebuildFolders();
    MarkDirty();
}

void FState::OnAssetUpdated(const FAssetData& AssetData)
{
    if (!bEnabled)
    {
        return;
    }
    if (IsIndexedAsset(AssetData))
    {
        const FAssetRecord Record = MakeRecord(AssetData);
        AssetsByObjectPath.Add(Record.ObjectPath, Record);
    }
    RebuildFolders();
    MarkDirty();
}

void FState::OnFilesLoaded()
{
    if (!bEnabled)
    {
        return;
    }
    RebuildFromRegistry();
}
}
