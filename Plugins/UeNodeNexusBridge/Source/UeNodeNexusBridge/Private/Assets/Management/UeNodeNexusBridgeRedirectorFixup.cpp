#include "UeNodeNexusBridgeAssetManagementShared.h"
#include "Redirectors/NexusRedirectorPackages.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/GCObjectScopeGuard.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
namespace
{
TSharedPtr<FJsonValue> PackageProgress(UPackage* Package, bool bWasDirty)
{
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("package"), Package->GetName());
    Row->SetBoolField(TEXT("was_dirty"), bWasDirty);
    Row->SetBoolField(TEXT("dirty"), Package->IsDirty());
    return MakeShared<FJsonValueObject>(Row);
}

// Every referencer is loaded while all redirector packages still exist. A
// referencer whose load dropped an export (a map actor whose class failed to
// resolve) is refused before anything is written: saving it would erase that
// export from disk.
bool FixReferencers(const TArray<RedirectorFixup::FRedirectorPackage>& Packages, const TArray<UObject*>& Redirectors,
    const TMap<FSoftObjectPath, FSoftObjectPath>& Redirects, const TArray<FPackageLoadAudit>& Audits,
    bool bDryRun, FRedirectorFixupProgress& Progress)
{
    TGCObjectsScopeGuard<UObject> KeepRedirectors(Redirectors);
    TArray<UPackage*> Loaded;
    for (int32 Index = Packages.Num(); Index < Audits.Num(); ++Index)
    {
        const FPackageLoadAudit& Audit = Audits[Index];
        Progress.Referencers.Add(MakeShared<FJsonValueObject>(Audit.ToJson()));
        if (!Audit.IsComplete() || Audit.Package->HasAnyPackageFlags(PKG_CompiledIn))
        {
            Progress.IncompleteReferencers.Add(MakeShared<FJsonValueObject>(Audit.ToJson()));
            continue;
        }
        Loaded.Add(Audit.Package);
    }
    if (!Progress.IncompleteReferencers.IsEmpty())
    {
        Progress.Error = TEXT("referencer_load_incomplete: ") + Progress.IncompleteReferencers[0]->AsObject()->GetStringField(TEXT("package"));
        return false;
    }
    if (bDryRun)
    {
        return true;
    }
    TGCObjectsScopeGuard<UPackage> KeepReferencers(Loaded);
    TMap<UPackage*, bool> WasDirty;
    for (UPackage* Package : Loaded)
    {
        WasDirty.Add(Package, Package->IsDirty());
    }
    if (!Loaded.IsEmpty())
    {
        FAssetToolsModule::GetModule().Get().RenameReferencingSoftObjectPaths(Loaded, Redirects);
    }
    for (UPackage* Package : Loaded)
    {
        const bool bWasDirty = WasDirty.FindRef(Package);
        Progress.ModifiedPackages.Add(PackageProgress(Package, bWasDirty));
        FString SaveError;
        if (!Transcode::SavePackageDirect(Package, nullptr, SaveError))
        {
            Progress.FailedPackages.Add(PackageProgress(Package, bWasDirty));
            Progress.bDirty = true;
            Progress.Error = FString::Printf(TEXT("referencer_save_failed: %s: %s"), *Package->GetName(), *SaveError);
            return false;
        }
        Progress.SavedPackages.Add(PackageProgress(Package, bWasDirty));
        Progress.bDirty |= Package->IsDirty();
    }

    TArray<FString> PathsToScan;
    for (const RedirectorFixup::FRedirectorPackage& Entry : Packages)
    {
        PathsToScan.AddUnique(FPackageName::GetLongPackagePath(Entry.PackageName.ToString()));
    }
    for (UPackage* Package : Loaded)
    {
        PathsToScan.AddUnique(FPackageName::GetLongPackagePath(Package->GetName()));
    }
    FAssetRegistryModule::GetRegistry().ScanPathsSynchronous(PathsToScan, true);
    return RedirectorFixup::RemainingReferencers(Packages, Progress);
}
}

FRedirectorFixupProgress FixRedirectorsUnderFolder(const FString& FolderPath, bool bDryRun)
{
    FRedirectorFixupProgress Progress;
    TArray<RedirectorFixup::FRedirectorPackage> Packages;
    TMap<FSoftObjectPath, FSoftObjectPath> Redirects;
    TArray<FString> ReferencerNames;
    if (!RedirectorFixup::CollectRedirectorPackages(FolderPath, Progress, Packages)
        || !RedirectorFixup::CollectReferencers(Packages, ReferencerNames, Progress.Error))
    {
        return Progress;
    }
    if (Packages.IsEmpty())
    {
        return Progress;
    }
    TArray<FString> AllPackageNames;
    for (const RedirectorFixup::FRedirectorPackage& Entry : Packages)
    {
        AllPackageNames.Add(Entry.PackageName.ToString());
    }
    AllPackageNames.Append(ReferencerNames);
    const TArray<FPackageLoadAudit> Audits = LoadPackagesAudited(AllPackageNames);
    for (const FPackageLoadAudit& Audit : Audits)
    {
        Progress.PackageLoads.Add(MakeShared<FJsonValueObject>(Audit.ToJson()));
    }
    if (!RedirectorFixup::ResolveRedirectorPackages(Packages, Audits, Progress, Redirects))
    {
        return Progress;
    }
    TArray<UObject*> Objects;
    for (const RedirectorFixup::FRedirectorPackage& Entry : Packages)
    {
        Objects.Append(Entry.Redirectors);
    }
    if (!FixReferencers(Packages, Objects, Redirects, Audits, bDryRun, Progress) || bDryRun)
    {
        return Progress;
    }

    TArray<FString> Paths;
    for (UObject* Object : Objects)
    {
        Paths.Add(Object->GetPathName());
    }
    // One deletion for the whole set: deleting a Blueprint's asset redirector
    // alone empties the package and invalidates its class and CDO redirectors.
    const int32 Deleted = ObjectTools::DeleteObjectsUnchecked(Objects);
    Progress.bDirty = true;
    TArray<FString> Leftover;
    for (const RedirectorFixup::FRedirectorPackage& Entry : Packages)
    {
        const FString Name = Entry.PackageName.ToString();
        if (FPackageName::DoesPackageExist(Name))
        {
            Leftover.Add(Name);
            continue;
        }
        Progress.DeletedPackages.Add(MakeShared<FJsonValueString>(Name));
    }
    if (Deleted >= Objects.Num())
    {
        for (const FString& Path : Paths)
        {
            Progress.DeletedRedirectors.Add(MakeShared<FJsonValueString>(Path));
        }
    }
    if (Deleted < Objects.Num() || !Leftover.IsEmpty())
    {
        Progress.Error = FString::Printf(TEXT("redirector_delete_failed: deleted %d of %d redirectors; packages left on disk: %s"),
            Deleted, Objects.Num(), *FString::Join(Leftover, TEXT(", ")));
    }
    return Progress;
}
}
