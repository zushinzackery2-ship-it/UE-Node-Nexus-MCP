#include "NexusMovePreflight.h"

#include "Assets/Management/NexusPackageLoadAudit.h"
#include "Assets/Management/UeNodeNexusBridgeAssetManagementShared.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge::AssetMove
{
bool Prepare(const TArray<TSharedPtr<FJsonValue>>& Items, TArray<FEntry>& Entries,
    TArray<UPackage*>& Packages, TMap<FSoftObjectPath, FSoftObjectPath>& Redirects,
    TSharedPtr<FJsonObject>& Details, FString& Error)
{
    Details = MakeShared<FJsonObject>();
    FAssetRegistryModule::GetRegistry().WaitForCompletion();
    TSet<FString> Sources;
    TSet<FString> Destinations;
    TArray<FString> Names;
    for (int32 Index = 0; Index < Items.Num(); ++Index)
    {
        const auto Item = Items[Index].IsValid() ? Items[Index]->AsObject() : nullptr;
        FEntry Entry;
        FString Package;
        FText Reason;
        if (!Item.IsValid() || !Item->TryGetStringField(TEXT("source_asset_path"), Entry.Source)
            || !Item->TryGetStringField(TEXT("destination_asset_path"), Entry.Destination)
            || !SplitAssetObjectPath(Entry.Destination, Package, Entry.Folder, Entry.Name, Reason)
            || Entry.Source.IsEmpty() || Sources.Contains(FPackageName::ObjectPathToPackageName(Entry.Source)) || Destinations.Contains(Package))
        {
            Error = TEXT("invalid_move_plan");
            Details->SetNumberField(TEXT("index"), Index);
            Details->SetStringField(TEXT("reason"), Reason.ToString());
            return false;
        }
        if (FPackageName::DoesPackageExist(Package) || FindPackage(nullptr, *Package))
        {
            Error = TEXT("destination_asset_exists");
            Details->SetStringField(TEXT("package"), Package);
            return false;
        }
        Entry.Destination = Package + TEXT(".") + Entry.Name;
        Sources.Add(FPackageName::ObjectPathToPackageName(Entry.Source));
        Destinations.Add(Package);
        Names.AddUnique(FPackageName::ObjectPathToPackageName(Entry.Source));
        Entries.Add(MoveTemp(Entry));
    }
    // Load the complete referencer closure before RenameAssets can save anything.
    for (int32 Index = 0; Index < Names.Num(); ++Index)
    {
        TArray<FName> Referencers;
        FAssetRegistryModule::GetRegistry().GetReferencers(FName(*Names[Index]), Referencers);
        for (FName Referencer : Referencers)
        {
            Names.AddUnique(Referencer.ToString());
        }
    }
    TArray<TSharedPtr<FJsonValue>> Loads;
    for (const FPackageLoadAudit& Audit : LoadPackagesAudited(Names))
    {
        Loads.Add(MakeShared<FJsonValueObject>(Audit.ToJson()));
        FString File;
        if (!Audit.IsComplete() || (FPackageName::DoesPackageExist(Audit.PackageName, &File)
            && IFileManager::Get().IsReadOnly(*File)))
        {
            Error = TEXT("move_package_unwritable");
            Details->SetArrayField(TEXT("package_loads"), Loads);
            return false;
        }
        Packages.AddUnique(Audit.Package);
    }
    Details->SetArrayField(TEXT("package_loads"), Loads);
    for (FEntry& Entry : Entries)
    {
        Entry.Asset = LoadAssetForManagement(Entry.Source);
        if (!Entry.Asset)
        {
            Error = TEXT("asset_not_found");
            Details->SetStringField(TEXT("asset"), Entry.Source);
            return false;
        }
        Entry.Source = Entry.Asset->GetPathName();
        Redirects.Add(FSoftObjectPath(Entry.Source), FSoftObjectPath(Entry.Destination));
        if (Cast<UBlueprint>(Entry.Asset))
        {
            const FString TargetPackage = FPackageName::ObjectPathToPackageName(Entry.Destination);
            const FString SourcePackage = FPackageName::ObjectPathToPackageName(Entry.Source);
            Redirects.Add(FSoftObjectPath(Entry.Source + TEXT("_C")), FSoftObjectPath(Entry.Destination + TEXT("_C")));
            Redirects.Add(FSoftObjectPath(SourcePackage + TEXT(".Default__") + FPackageName::ObjectPathToObjectName(Entry.Source) + TEXT("_C")),
                FSoftObjectPath(TargetPackage + TEXT(".Default__") + Entry.Name + TEXT("_C")));
        }
    }
    const auto Cdos = CdoReferences(Redirects);
    Details->SetArrayField(TEXT("cdo_references"), Cdos);
    if (!Cdos.IsEmpty())
    {
        Error = TEXT("native_cdo_reference");
        return false;
    }
    return true;
}
}
