#include "NexusRedirectorPackages.h"

#include "Assets/Management/UeNodeNexusBridgeAssetManagementShared.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace UeNodeNexusBridge::RedirectorFixup
{
namespace
{
bool IsWritablePackageFile(const FString& PackageName)
{
    FString Filename;
    return FPackageName::DoesPackageExist(PackageName, &Filename) && !IFileManager::Get().IsReadOnly(*Filename);
}

UObject* FinalDestination(UObjectRedirector* Redirector)
{
    TSet<UObjectRedirector*> Seen;
    UObject* Destination = Redirector->DestinationObject;
    while (auto* Next = Cast<UObjectRedirector>(Destination))
    {
        if (Seen.Contains(Next))
        {
            return nullptr;
        }
        Seen.Add(Next);
        Destination = Next->DestinationObject;
    }
    return Destination;
}

TSet<FName> PackageNames(const TArray<FRedirectorPackage>& Packages)
{
    TSet<FName> Names;
    for (const FRedirectorPackage& Entry : Packages)
    {
        Names.Add(Entry.PackageName);
    }
    return Names;
}

}

bool CollectRedirectorPackages(const FString& FolderPath, FRedirectorFixupProgress& Progress,
    TArray<FRedirectorPackage>& OutPackages)
{
    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*FolderPath));
    Filter.bRecursivePaths = true;
    Filter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());
    TArray<FAssetData> Assets;
    FAssetRegistryModule::GetRegistry().GetAssets(Filter, Assets);
    TArray<FName> Names;
    for (const FAssetData& Asset : Assets)
    {
        Names.AddUnique(Asset.PackageName);
    }
    Names.Sort(FNameLexicalLess());
    for (const FName Name : Names)
    {
        const FString PackageName = Name.ToString();
        Progress.RedirectorPackages.Add(MakeShared<FJsonValueString>(PackageName));
        if (!IsWritablePackageFile(PackageName))
        {
            Progress.Error = TEXT("redirector_missing_or_read_only: ") + PackageName;
            return false;
        }
        FRedirectorPackage& Entry = OutPackages.AddDefaulted_GetRef();
        Entry.PackageName = Name;
    }
    return true;
}

bool ResolveRedirectorPackages(TArray<FRedirectorPackage>& Packages, const TArray<FPackageLoadAudit>& Audits,
    FRedirectorFixupProgress& Progress, TMap<FSoftObjectPath, FSoftObjectPath>& OutRedirects)
{
    for (int32 Index = 0; Index < Packages.Num(); ++Index)
    {
        FRedirectorPackage& Entry = Packages[Index];
        const FPackageLoadAudit& Audit = Audits[Index];
        if (!Audit.IsComplete())
        {
            Progress.Error = TEXT("redirector_package_load_incomplete: ") + Audit.PackageName;
            return false;
        }
        // The registry rows are a snapshot; the loaded package is the complete set.
        ForEachObjectWithPackage(Audit.Package, [&Entry](UObject* Object)
        {
            if (auto* Redirector = Cast<UObjectRedirector>(Object))
            {
                Entry.Redirectors.Add(Redirector);
            }
            return true;
        }, false);
        if (Entry.Redirectors.IsEmpty())
        {
            Progress.Error = TEXT("redirector_package_unavailable: ") + Audit.PackageName;
            return false;
        }
        for (UObjectRedirector* Redirector : Entry.Redirectors)
        {
            Progress.Redirectors.Add(MakeShared<FJsonValueString>(Redirector->GetPathName()));
            UObject* Destination = FinalDestination(Redirector);
            if (Destination == nullptr)
            {
                Progress.Error = TEXT("redirector_destination_unavailable: ") + Redirector->GetPathName();
                return false;
            }
            OutRedirects.Add(FSoftObjectPath(Redirector), FSoftObjectPath(Destination));
        }
    }
    Progress.Count = Progress.Redirectors.Num();
    return true;
}

bool CollectReferencers(const TArray<FRedirectorPackage>& Packages, TArray<FString>& OutReferencers, FString& OutError)
{
    const TSet<FName> Owned = PackageNames(Packages);
    for (const FRedirectorPackage& Entry : Packages)
    {
        TArray<FName> Referencers;
        FAssetRegistryModule::GetRegistry().GetReferencers(Entry.PackageName, Referencers);
        for (const FName Referencer : Referencers)
        {
            if (Owned.Contains(Referencer))
            {
                continue;
            }
            const FString Name = Referencer.ToString();
            if (!IsWritablePackageFile(Name))
            {
                OutError = TEXT("referencer_missing_or_read_only: ") + Name;
                return false;
            }
            OutReferencers.AddUnique(Name);
        }
    }
    OutReferencers.Sort();
    return true;
}

bool RemainingReferencers(const TArray<FRedirectorPackage>& Packages, FRedirectorFixupProgress& Progress)
{
    const TSet<FName> Owned = PackageNames(Packages);
    for (const FRedirectorPackage& Entry : Packages)
    {
        TArray<FName> Remaining;
        FAssetRegistryModule::GetRegistry().GetReferencers(Entry.PackageName, Remaining);
        for (const FName Name : Remaining)
        {
            if (Owned.Contains(Name))
            {
                continue;
            }
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("redirector"), Entry.PackageName.ToString());
            Row->SetStringField(TEXT("package"), Name.ToString());
            Progress.RemainingReferencers.Add(MakeShared<FJsonValueObject>(Row));
            if (Progress.Error.IsEmpty())
            {
                Progress.Error = FString::Printf(TEXT("redirector_still_referenced: %s by %s"), *Entry.PackageName.ToString(), *Name.ToString());
            }
        }
    }
    return Progress.RemainingReferencers.IsEmpty();
}

}
