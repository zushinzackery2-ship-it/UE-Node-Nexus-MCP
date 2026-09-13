#include "NexusPackageFiles.h"

#include "FileHelpers.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace UeNodeNexusBridge::Collaboration
{
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
    TArray<UPackage*> Reload;
    FString Map;
    for (const auto& Value : Rows(Receipt, TEXT("packages")))
    {
        const FJson Package = Value->AsObject();
        if (UPackage* Loaded = FindPackage(nullptr, *Text(Package, TEXT("package"))))
        {
            ResetLoaders(Loaded);
            if (Flag(Package, TEXT("existed")) && !Flag(Package, TEXT("map")))
            {
                Loaded->SetDirtyFlag(false);
                Reload.Add(Loaded);
            }
        }
        if (Flag(Package, TEXT("map")))
        {
            const FString Requested = Text(Object(Receipt, TEXT("before")), TEXT("map_path"));
            if (Text(Package, TEXT("package")) == Requested)
            {
                Map = Text(Package, TEXT("file"));
            }
        }
        if (!RestorePackageFiles(Package, true, Error))
        {
            return false;
        }
    }
    FText ReloadError;
    bool bRestored = Reload.IsEmpty() || UPackageTools::ReloadPackages(Reload, ReloadError, EReloadPackagesInteractionMode::AssumePositive);
    if (bRestored && !Map.IsEmpty())
    {
        bRestored = FEditorFileUtils::LoadMap(Map, false, false);
    }
    // Restore the original saved state after loading the in-memory checkpoint.
    // The dirty flag below distinguishes memory from the original disk bytes.
    for (const auto& Value : Rows(Receipt, TEXT("packages")))
    {
        const FJson Package = Value->AsObject();
        if (!RestorePackageFiles(Package, false, Error))
        {
            return false;
        }
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
