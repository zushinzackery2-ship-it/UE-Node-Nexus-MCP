#include "NexusCommitInternal.h"

#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"

namespace UeNodeNexusBridge::Collaboration
{
namespace
{
struct FPendingEntry
{
    FString Id;
    TSet<FString> Packages;
    bool bUnknown = false;
};
TMap<FString, FPendingEntry> Pending;
TSet<FString> FailedWrites;
TSet<FString> ChangedFiles;
FCriticalSection ChangeMutex;
FDelegateHandle WatchHandle;
FString WatchRoot;
bool bStarted = false;
bool bRescan = false;
bool bWatchFailed = false;

void IndexReceipt(const FString& File, const FJson& Receipt)
{
    Pending.Remove(File);
    const FString Phase = Text(Receipt, TEXT("phase"));
    if (Receipt.IsValid() && !Text(Receipt, TEXT("apply_id")).IsEmpty()
        && (Phase == TEXT("ue_committed") || Phase == TEXT("rolled_back") || Phase == TEXT("rejected")))
    {
        return;
    }
    FPendingEntry Entry;
    Entry.Id = Text(Receipt, TEXT("apply_id"));
    Entry.bUnknown = !Receipt.IsValid() || Entry.Id.IsEmpty() || Phase.IsEmpty();
    if (Entry.Id.IsEmpty())
    {
        Entry.Id = File;
    }
    const FJson Request = Object(Receipt, TEXT("request"));
    FString Asset = Text(Request, TEXT("asset_path"));
    FString Map, Group;
    if (Asset.Split(TEXT("#"), &Map, &Group))
    {
        Asset = Map;
    }
    if (!Asset.IsEmpty())
    {
        Entry.Packages.Add(FPackageName::ObjectPathToPackageName(Asset));
    }
    for (const auto& Row : Rows(Receipt, TEXT("packages")))
    {
        Entry.Packages.Add(Text(Row->AsObject(), TEXT("package")));
    }
    Entry.Packages.Remove(FString());
    Entry.bUnknown |= Entry.Packages.IsEmpty();
    Pending.Add(File, MoveTemp(Entry));
}

void Scan()
{
    Pending.Empty();
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *WatchRoot, TEXT("receipt.json"), true, false);
    for (const FString& File : Files)
    {
        FJson Receipt;
        ReadJournal(File, Receipt);
        IndexReceipt(File, Receipt);
    }
}

void EnsureIndex()
{
    check(IsInGameThread());
    if (!bStarted)
    {
        bStarted = true;
        WatchRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Nexus/Collaboration"));
        IFileManager::Get().MakeDirectory(*WatchRoot, true);
        IDirectoryWatcher* Watcher = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")).Get();
        bWatchFailed = !Watcher || !Watcher->RegisterDirectoryChangedCallback_Handle(WatchRoot,
            IDirectoryWatcher::FDirectoryChanged::CreateLambda([](const TArray<FFileChangeData>& Changes)
            {
                FScopeLock Lock(&ChangeMutex);
                for (const FFileChangeData& Change : Changes)
                {
                    if (FPaths::GetCleanFilename(Change.Filename) == TEXT("receipt.json"))
                    {
                        ChangedFiles.Add(FPaths::ConvertRelativePathToFull(Change.Filename));
                    }
                    else if (Change.Action == FFileChangeData::FCA_Removed && FPaths::GetExtension(Change.Filename).IsEmpty())
                    {
                        bRescan = true;
                    }
                }
            }), WatchHandle, IDirectoryWatcher::IncludeDirectoryChanges);
        Scan();
    }
    TSet<FString> Changes;
    bool bScan;
    {
        FScopeLock Lock(&ChangeMutex);
        Changes = MoveTemp(ChangedFiles);
        bScan = bRescan;
        bRescan = false;
    }
    if (bScan)
    {
        Scan();
    }
    else
    {
        for (const FString& File : Changes)
        {
            FJson Receipt;
            ReadJournal(File, Receipt);
            // A deleted or corrupt receipt remains an explicit blocker.
            IndexReceipt(File, Receipt);
        }
    }
}
}

void UpdatePendingReceipt(const FString& File, const FJson& Receipt, bool bDurable)
{
    EnsureIndex();
    if (bDurable)
    {
        FailedWrites.Remove(File);
        IndexReceipt(File, Receipt);
    }
    else
    {
        FailedWrites.Add(File);
    }
}

TArray<TSharedPtr<FJsonValue>> PendingRecovery()
{
    EnsureIndex();
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const auto& Pair : Pending)
    {
        Result.Add(MakeShared<FJsonValueString>(Pair.Value.Id));
    }
    for (const FString& File : FailedWrites)
    {
        Result.Add(MakeShared<FJsonValueString>(File));
    }
    if (bWatchFailed)
    {
        Result.Add(MakeShared<FJsonValueString>(TEXT("receipt_directory_watch_failed")));
    }
    return Result;
}

bool HasPending(const FJson& Request, const FString& ApplyId, FString& Error)
{
    EnsureIndex();
    if (bWatchFailed || !FailedWrites.IsEmpty())
    {
        Error = TEXT("receipt index has a directory watch or durable journal failure");
        return true;
    }
    FString Asset = Text(Request, TEXT("asset_path")), Map, Group;
    if (Asset.Split(TEXT("#"), &Map, &Group))
    {
        Asset = Map;
    }
    const FString Package = FPackageName::ObjectPathToPackageName(Asset);
    for (const auto& Pair : Pending)
    {
        const auto& Entry = Pair.Value;
        if (Entry.bUnknown || (Entry.Id != ApplyId && Entry.Packages.Contains(Package)))
        {
            Error = TEXT("recover pending apply_id ") + Entry.Id;
            return true;
        }
    }
    return false;
}

void StopPendingIndex()
{
    if (WatchHandle.IsValid())
    {
        if (auto* Module = FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")))
        {
            if (IDirectoryWatcher* Watcher = Module->Get())
            {
                Watcher->UnregisterDirectoryChangedCallback_Handle(WatchRoot, WatchHandle);
            }
        }
    }
    WatchHandle.Reset();
    bStarted = false;
}
}
