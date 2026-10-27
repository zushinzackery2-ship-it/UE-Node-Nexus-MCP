#include "UeNodeNexusBridgeTranscode.h"

#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "RenderAssetUpdate.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UeNodeNexusBridgeRequestDispatch.h"

namespace UeNodeNexusBridge::Transcode
{
static const TCHAR* GReadOnlyPrefix = TEXT("read-only on disk");
static const TCHAR* GStreamingSuspendedPrefix = TEXT("asset streaming is suspended");

static FString PackageSaveFailure(const UPackage* Package)
{
    return FString::Printf(TEXT("SavePackage failed for %s (fully_loaded=%d, loaded_path=%s, file_size=%lld)"),
        *Package->GetName(), Package->IsFullyLoaded(), *Package->GetLoadedPath().GetPackageName(), Package->GetFileSize());
}

static bool PackageFilename(const UPackage* Package, FString& OutFilename)
{
    if (Package == nullptr)
    {
        return false;
    }
    const bool bMap = Package->ContainsMap();
    return FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), OutFilename, bMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
}

bool IsPackageFileReadOnly(const UPackage* Package)
{
    FString Filename;
    return PackageFilename(Package, Filename) && IFileManager::Get().FileExists(*Filename) && IFileManager::Get().IsReadOnly(*Filename);
}

FString SaveErrorCode(const FString& Error)
{
    if (Error.StartsWith(GReadOnlyPrefix))
    {
        return TEXT("save_blocked_read_only");
    }
    if (Error.StartsWith(GStreamingSuspendedPrefix))
    {
        return TEXT("save_blocked_asset_streaming_suspended");
    }
    return TEXT("save_failed");
}

bool SavePackageDirect(UPackage* Package, UObject* Base, FString& OutError, FString* OutCode)
{
    const double Started = FPlatformTime::Seconds();
    OutError.Reset();
    if (OutCode != nullptr)
    {
        OutCode->Reset();
    }
    if (Package == nullptr)
    {
        OutError = TEXT("asset has no package");
    }
    FString Filename;
    if (Package != nullptr && !PackageFilename(Package, Filename))
    {
        OutError = FString::Printf(TEXT("cannot map package to a file: %s"), *Package->GetName());
    }
    else if (Package != nullptr && IsPackageFileReadOnly(Package))
    {
        // Never let the editor prompt for a source-control checkout from a bridge call.
        OutError = FString::Printf(TEXT("%s (source control checkout required): %s"), GReadOnlyPrefix, *Filename);
    }
    else if (Package != nullptr && IsAssetStreamingSuspended())
    {
        // SavePackage blocks on render-asset streaming and asserts while another
        // engine operation owns the suspension. Never sleep or resume here:
        // this helper does not own that global suspension.
        OutError = FString::Printf(
            TEXT("%s; retry after the owning operation resumes: %s"),
            GStreamingSuspendedPrefix,
            *Package->GetName());
    }
    else if (Package != nullptr)
    {
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        Args.Error = GLog;
        if (UPackage::SavePackage(Package, Base, *Filename, Args))
        {
            UE_LOG(LogTemp, Display, TEXT("Nexus request=%s phase=package_save package=%s ok=1 duration_ms=%.3f"),
                *ActiveBridgeRequestId(), *Package->GetName(), (FPlatformTime::Seconds() - Started) * 1000.0);
            return true;
        }
        OutError = PackageSaveFailure(Package);
    }
    if (OutCode != nullptr)
    {
        *OutCode = SaveErrorCode(OutError);
    }
    UE_LOG(LogTemp, Warning, TEXT("Nexus request=%s phase=package_save package=%s ok=0 error=%s duration_ms=%.3f"),
        *ActiveBridgeRequestId(), Package ? *Package->GetName() : TEXT("null"), *OutError,
        (FPlatformTime::Seconds() - Started) * 1000.0);
    return false;
}

bool SavePackageDirect(UObject* Asset, FString& OutError)
{
    return SavePackageDirect(Asset ? Asset->GetOutermost() : nullptr, Asset, OutError);
}

bool SavePackageTo(UPackage* Package, UObject* Base, const FString& Filename, bool bKeepDirty, FString& OutError)
{
    if (!Package || IsPackageFileReadOnly(Package) || IsAssetStreamingSuspended())
    {
        OutError = !Package ? TEXT("asset has no package") : IsPackageFileReadOnly(Package)
            ? FString(GReadOnlyPrefix) : FString(GStreamingSuspendedPrefix);
        return false;
    }
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    // Checkpoints and staged copies must retain the source package identity.
    // UE's autosave flag keeps LoadedPath and NewlyCreated unchanged for a copy.
    Args.SaveFlags = SAVE_NoError | (bKeepDirty ? (SAVE_KeepDirty | SAVE_FromAutosave) : 0);
    Args.Error = GLog;
    const bool bWasDirty = Package->IsDirty();
    if (!UPackage::SavePackage(Package, Base, *Filename, Args))
    {
        OutError = PackageSaveFailure(Package);
        return false;
    }
    if (bKeepDirty)
    {
        Package->SetDirtyFlag(bWasDirty);
    }
    return true;
}
}
