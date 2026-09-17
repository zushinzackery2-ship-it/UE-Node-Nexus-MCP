#include "NexusPackageFiles.h"

#include "Editor.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace UeNodeNexusBridge::Collaboration
{
static void ClosePackageEditors(UPackage* Package)
{
    if (GEditor == nullptr)
    {
        return;
    }
    UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (Editors == nullptr)
    {
        return;
    }
    TArray<UObject*> EditedAssets;
    for (UObject* Asset : Editors->GetAllEditedAssets())
    {
        if (Asset != nullptr && Asset->GetOutermost() == Package)
        {
            EditedAssets.Add(Asset);
        }
    }
    for (UObject* Asset : EditedAssets)
    {
        Editors->CloseAllEditorsForAsset(Asset);
    }
}

static bool ReleaseLoadedPackages(const FJson& Receipt, FString& Error)
{
    TArray<UPackage*> Packages;
    TArray<FString> Names;
    for (const auto& Value : Rows(Receipt, TEXT("packages")))
    {
        const FJson Package = Value->AsObject();
        if (Flag(Package, TEXT("map")))
        {
            continue;
        }
        const FString Name = Text(Package, TEXT("package"));
        if (UPackage* Loaded = FindPackage(nullptr, *Name))
        {
            ClosePackageEditors(Loaded);
            ResetLoaders(Loaded);
            Loaded->SetDirtyFlag(false);
            Packages.Add(Loaded);
            Names.Add(Name);
        }
    }
    if (Packages.IsEmpty())
    {
        return true;
    }

    UPackageTools::FUnloadPackageParams Params(Packages);
    Params.bUnloadDirtyPackages = true;
    Params.bResetTransBuffer = false;
    if (!UPackageTools::UnloadPackages(Params))
    {
        Error = TEXT("cannot unload packages before checkpoint restore: ") + Params.OutErrorMessage.ToString();
        return false;
    }
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    DeleteLoaders();
    for (const FString& Name : Names)
    {
        if (FindPackage(nullptr, *Name) != nullptr)
        {
            Error = TEXT("package remains loaded during checkpoint restore: ") + Name;
            return false;
        }
    }
    return true;
}

static bool LoadRestoredPackages(const FJson& Receipt, FString& Error)
{
    for (const auto& Value : Rows(Receipt, TEXT("packages")))
    {
        const FJson Package = Value->AsObject();
        if (!Flag(Package, TEXT("existed")) || Flag(Package, TEXT("map")))
        {
            continue;
        }
        FString Memory;
        for (const auto& FileValue : Rows(Package, TEXT("files")))
        {
            const FJson File = FileValue->AsObject();
            if (Text(File, TEXT("path")) == Text(Package, TEXT("file")))
            {
                Memory = Text(File, TEXT("memory"));
                break;
            }
        }
        const FString Name = Text(Package, TEXT("package"));
        if (Memory.IsEmpty() || LoadPackage(nullptr, *Memory, LOAD_None) == nullptr)
        {
            Error = TEXT("cannot load restored checkpoint package: ") + Name;
            return false;
        }
    }
    return true;
}

bool RestoreCheckpoint(const FJson& Receipt, FString& Error)
{
    const FJson Request = Object(Receipt, TEXT("request"));
    FString Ignored;
    if (!SaveReceipt(Receipt, TEXT("restoring"), Error))
    {
        return false;
    }
    // An asset may have been created before its failing verb returned.
    if (Flag(Request, TEXT("expected_absent")))
    {
        if (UObject* Created = FindObject<UObject>(nullptr, *Text(Request, TEXT("asset_path"))))
        {
            TArray<UObject*> Objects;
            Objects.Add(Created);
            if (ObjectTools::DeleteObjectsUnchecked(Objects) != 1)
            {
                Error = TEXT("cannot remove the newly created object during recovery");
                return false;
            }
        }
    }
    if (!ReleaseLoadedPackages(Receipt, Error))
    {
        return false;
    }
    FString Map;
    for (const auto& Value : Rows(Receipt, TEXT("packages")))
    {
        const FJson Package = Value->AsObject();
        if (Flag(Package, TEXT("map")))
        {
            for (const auto& FileValue : Rows(Package, TEXT("files")))
            {
                const FJson File = FileValue->AsObject();
                if (Text(File, TEXT("path")) == Text(Package, TEXT("file")))
                {
                    Map = Text(File, TEXT("memory"));
                    break;
                }
            }
            continue;
        }
        if (!RestorePackageFiles(Package, false, Error))
        {
            return false;
        }
    }
    if (!LoadRestoredPackages(Receipt, Error))
    {
        return false;
    }
    FText ReloadError;
    bool bRestored = true;
    if (!Map.IsEmpty())
    {
        bRestored = FEditorFileUtils::LoadMap(Map, false, false);
    }
    // Restore map files after loading the checkpoint from its private path;
    // the editor therefore never has to replace an open target file.
    for (const auto& Value : Rows(Receipt, TEXT("packages")))
    {
        const FJson Package = Value->AsObject();
        if (!Flag(Package, TEXT("map")))
        {
            continue;
        }
        if (!RestorePackageFiles(Package, false, Error))
        {
            return false;
        }
    }
    for (const auto& Value : Rows(Receipt, TEXT("packages")))
    {
        const FJson Package = Value->AsObject();
        if (UPackage* Loaded = FindPackage(nullptr, *Text(Package, TEXT("package"))))
        {
            Loaded->SetDirtyFlag(Flag(Package, TEXT("was_dirty")));
        }
    }
    const FJson Current = Observe(Request);
    const FString Expected = Text(Object(Receipt, TEXT("before")), TEXT("content_revision"));
    if (!bRestored || !Current.IsValid() || Expected != Text(Current, TEXT("content_revision")))
    {
        Error = TEXT("checkpoint verification failed: ") + ReloadError.ToString();
        Receipt->SetObjectField(TEXT("recovery_observation"), Current.IsValid() ? Current : MakeShared<FJsonObject>());
        return false;
    }
    Receipt->SetObjectField(TEXT("recovered"), Current);
    return SaveReceipt(Receipt, TEXT("rolled_back"), Error);
}
}
