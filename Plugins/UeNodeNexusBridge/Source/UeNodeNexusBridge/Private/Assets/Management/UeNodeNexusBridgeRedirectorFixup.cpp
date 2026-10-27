#include "UeNodeNexusBridgeAssetManagementShared.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

namespace UeNodeNexusBridge
{
static bool CollectWritableReferencers(const FAssetData& Asset, TArray<UPackage*>& OutPackages, FString& OutError)
{
    TArray<FName> Referencers;
    FAssetRegistryModule::GetRegistry().GetReferencers(Asset.PackageName, Referencers);
    for (FName PackageName : Referencers)
    {
        if (PackageName == Asset.PackageName)
        {
            continue;
        }
        const FString Name = PackageName.ToString();
        FString Filename;
        if (!FPackageName::DoesPackageExist(Name, &Filename) || IFileManager::Get().IsReadOnly(*Filename))
        {
            OutError = FString::Printf(TEXT("referencer_missing_or_read_only: %s"), *Name);
            return false;
        }
        UPackage* Package = FindPackage(nullptr, *Name);
        if (Package == nullptr)
        {
            Package = LoadPackage(nullptr, *Name, LOAD_None);
        }
        if (Package == nullptr || Package->HasAnyPackageFlags(PKG_CompiledIn))
        {
            OutError = FString::Printf(TEXT("referencer_cannot_load: %s"), *Name);
            return false;
        }
        OutPackages.AddUnique(Package);
    }
    return true;
}

static TSharedPtr<FJsonValue> PackageProgress(UPackage* Package, bool bWasDirty)
{
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("package"), Package->GetName());
    Row->SetBoolField(TEXT("was_dirty"), bWasDirty);
    Row->SetBoolField(TEXT("dirty"), Package->IsDirty());
    return MakeShared<FJsonValueObject>(Row);
}

static bool FixOneRedirector(const FAssetData& Asset, FRedirectorFixupProgress& Progress)
{
    UObjectRedirector* Redirector = Cast<UObjectRedirector>(Asset.GetAsset());
    if (Redirector == nullptr || Redirector->DestinationObject == nullptr)
    {
        Progress.Error = TEXT("redirector_destination_unavailable: ") + Asset.GetObjectPathString();
        return false;
    }
    FString Filename;
    if (!FPackageName::DoesPackageExist(Asset.PackageName.ToString(), &Filename) || IFileManager::Get().IsReadOnly(*Filename))
    {
        Progress.Error = TEXT("redirector_missing_or_read_only: ") + Asset.GetObjectPathString();
        return false;
    }

    TArray<UPackage*> Packages;
    if (!CollectWritableReferencers(Asset, Packages, Progress.Error))
    {
        return false;
    }

    if (!Packages.IsEmpty())
    {
        TMap<UPackage*, bool> WasDirty;
        for (UPackage* Package : Packages)
        {
            WasDirty.Add(Package, Package->IsDirty());
        }
        const FSoftObjectPath OldPath(Redirector);
        const FSoftObjectPath NewPath(Redirector->DestinationObject);
        TMap<FSoftObjectPath, FSoftObjectPath> Redirects;
        Redirects.Add(OldPath, NewPath);
        if (Cast<UBlueprint>(Redirector->DestinationObject) != nullptr)
        {
            Redirects.Add(FSoftObjectPath(OldPath.ToString() + TEXT("_C")), FSoftObjectPath(NewPath.ToString() + TEXT("_C")));
            Redirects.Add(
                FSoftObjectPath(OldPath.GetLongPackageName() + TEXT(".Default__") + OldPath.GetAssetName() + TEXT("_C")),
                FSoftObjectPath(NewPath.GetLongPackageName() + TEXT(".Default__") + NewPath.GetAssetName() + TEXT("_C")));
        }
        FAssetToolsModule::GetModule().Get().RenameReferencingSoftObjectPaths(Packages, Redirects);
        for (UPackage* Package : Packages)
        {
            const bool bWasDirty = WasDirty.FindRef(Package);
            Progress.ModifiedPackages.Add(PackageProgress(Package, bWasDirty));
            Progress.bDirty |= Package->IsDirty();
            FString SaveError;
            if (!Transcode::SavePackageDirect(Package, nullptr, SaveError))
            {
                Progress.FailedPackages.Add(PackageProgress(Package, bWasDirty));
                Progress.bDirty |= Package->IsDirty();
                Progress.Error = FString::Printf(TEXT("referencer_save_failed: %s: %s"), *Package->GetName(), *SaveError);
                return false;
            }
            Progress.SavedPackages.Add(PackageProgress(Package, bWasDirty));
            Progress.bDirty |= Package->IsDirty();
        }
    }

    TArray<FString> PathsToScan = { FPackageName::GetLongPackagePath(Asset.PackageName.ToString()) };
    for (UPackage* Package : Packages)
    {
        PathsToScan.AddUnique(FPackageName::GetLongPackagePath(Package->GetName()));
    }
    FAssetRegistryModule::GetRegistry().ScanPathsSynchronous(PathsToScan, true);
    TArray<FName> Remaining;
    FAssetRegistryModule::GetRegistry().GetReferencers(Asset.PackageName, Remaining);
    Remaining.Remove(Asset.PackageName);
    if (!Remaining.IsEmpty())
    {
        for (const FName Name : Remaining)
        {
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("redirector"), Asset.GetObjectPathString());
            Row->SetStringField(TEXT("package"), Name.ToString());
            Progress.RemainingReferencers.Add(MakeShared<FJsonValueObject>(Row));
        }
        Progress.Error = FString::Printf(TEXT("redirector_still_referenced: %s by %s"), *Asset.GetObjectPathString(), *Remaining[0].ToString());
        return false;
    }
    if (ObjectTools::DeleteObjectsUnchecked({ Redirector }) != 1)
    {
        Progress.Error = TEXT("redirector_delete_failed: ") + Asset.GetObjectPathString();
        return false;
    }
    Progress.DeletedRedirectors.Add(MakeShared<FJsonValueString>(Asset.GetObjectPathString()));
    Progress.bDirty = true;
    return true;
}

FRedirectorFixupProgress FixRedirectorsUnderFolder(const FString& FolderPath, bool bDryRun)
{
    FRedirectorFixupProgress Progress;
    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*FolderPath));
    Filter.bRecursivePaths = true;
    Filter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());

    TArray<FAssetData> Redirectors;
    FAssetRegistryModule::GetRegistry().GetAssets(Filter, Redirectors);
    for (const FAssetData& Asset : Redirectors)
    {
        Progress.Redirectors.Add(MakeShared<FJsonValueString>(Asset.GetObjectPathString()));
        if (!bDryRun)
        {
            if (!FixOneRedirector(Asset, Progress))
            {
                break;
            }
        }
    }
    Progress.Count = Redirectors.Num();
    return Progress;
}
}
