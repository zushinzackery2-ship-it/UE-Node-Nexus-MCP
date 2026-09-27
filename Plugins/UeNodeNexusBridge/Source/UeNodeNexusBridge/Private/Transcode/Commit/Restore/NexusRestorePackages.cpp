#include "NexusRestore.h"

#include "Editor.h"
#include "PackageTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge::Collaboration
{
static void ClosePackageEditors(UPackage* Package)
{
    UAssetEditorSubsystem* Editors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
    if (!Editors)
    {
        return;
    }
    const TArray<UObject*> EditedAssets = Editors->GetAllEditedAssets();
    for (UObject* Asset : EditedAssets)
    {
        if (Asset && Asset->GetOutermost() == Package)
        {
            Editors->CloseAllEditorsForAsset(Asset);
        }
    }
}

bool PrepareCheckpointPackages(const FJson& Receipt, FString& Error)
{
    TArray<UPackage*> LoadedPackages;
    for (const auto& Value : Rows(Receipt, TEXT("packages")))
    {
        const FJson Package = Value->AsObject();
        UPackage* Loaded = FindPackage(nullptr, *Text(Package, TEXT("package")));
        if (!Loaded)
        {
            continue;
        }
        if (Flag(Package, TEXT("existed")) && Loaded->HasAnyPackageFlags(PKG_InMemoryOnly))
        {
            Error = TEXT("checkpoint target cannot be reloaded because it is memory-only: ") + Loaded->GetName();
            return false;
        }
        LoadedPackages.Add(Loaded);
    }
    for (UPackage* Loaded : LoadedPackages)
    {
        ClosePackageEditors(Loaded);
        Loaded->FullyLoad();
        ResetLoaders(Loaded);
    }
    return true;
}

bool RestoreCheckpointMemory(const FJson& Receipt, FString& Error)
{
    TArray<UPackage*> LoadedPackages;
    TMap<FString, TWeakObjectPtr<UPackage>> Previous;
    for (const auto& Value : Rows(Receipt, TEXT("packages")))
    {
        const FJson Package = Value->AsObject();
        if (!Flag(Package, TEXT("existed")))
        {
            continue;
        }
        const FString Name = Text(Package, TEXT("package"));
        UPackage* Loaded = FindPackage(nullptr, *Name);
        Previous.Add(Name, Loaded);
        if (Loaded)
        {
            LoadedPackages.Add(Loaded);
        }
        // Native reload resolves imports by the original package name. Keep the
        // checkpoint there only until memory is loaded, then restore the disk version.
        if (!RestorePackageFiles(Package, true, Error))
        {
            return false;
        }
    }
    if (!LoadedPackages.IsEmpty())
    {
        Receipt->SetBoolField(TEXT("undo_history_reset"), true);
        if (!SaveReceipt(Receipt, TEXT("restoring"), Error))
        {
            return false;
        }
        FText ReloadError;
        // UE fixes asset references, Blueprint instances and loaded worlds here.
        // Its bool result counts asset reloads and is false for a world-only reload.
        UPackageTools::ReloadPackages(LoadedPackages, ReloadError, UPackageTools::EReloadPackagesInteractionMode::AssumePositive);
        if (!ReloadError.IsEmpty())
        {
            Error = TEXT("checkpoint package reload failed: ") + ReloadError.ToString();
            return false;
        }
    }
    for (const auto& Pair : Previous)
    {
        UPackage* Restored = FindPackage(nullptr, *Pair.Key);
        if (Pair.Value.IsExplicitlyNull())
        {
            Restored = LoadPackage(nullptr, *Pair.Key, LOAD_None);
        }
        if (!Restored || TWeakObjectPtr<UPackage>(Restored) == Pair.Value)
        {
            Error = TEXT("checkpoint package was not reloaded: ") + Pair.Key;
            return false;
        }
        Restored->FullyLoad();
    }
    UE_LOG(LogTemp, Display, TEXT("Nexus apply_id=%s restored_memory_packages=%d"), *Text(Receipt, TEXT("apply_id")), Previous.Num());
    return true;
}
}
