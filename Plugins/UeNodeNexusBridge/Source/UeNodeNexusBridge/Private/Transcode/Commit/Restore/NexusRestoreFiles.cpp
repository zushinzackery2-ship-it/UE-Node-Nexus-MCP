#include "NexusRestore.h"

#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/PackagePath.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge::Collaboration
{
static bool ValidateFile(const FJson& File, const TCHAR* Version, FString& Error)
{
    const FString Hash = Text(File, *(FString(Version) + TEXT("_hash")));
    const FString Source = Text(File, Version);
    if (Hash.IsEmpty() || (Hash != TEXT("absent") && (Source.IsEmpty() || FileHash(Source) != Hash)))
    {
        Error = TEXT("checkpoint is missing or corrupt: ") + FString(Version) + TEXT(" ") + Text(File, TEXT("path"));
        return false;
    }
    return true;
}

bool ValidateCheckpointFiles(const FJson& Receipt, FString& Error)
{
    for (const auto& Value : Rows(Receipt, TEXT("packages")))
    {
        const FJson Package = Value->AsObject();
        const FString Name = Text(Package, TEXT("package"));
        const FString MainFile = Text(Package, TEXT("file"));
        const auto Files = Rows(Package, TEXT("files"));
        if (!FPackageName::IsValidLongPackageName(Name) || MainFile.IsEmpty() || Files.IsEmpty())
        {
            Error = TEXT("checkpoint package has incomplete file metadata: ") + Name;
            return false;
        }
        bool bHasMainFile = false;
        for (const auto& FileValue : Files)
        {
            const FJson File = FileValue->AsObject();
            const FString Path = Text(File, TEXT("path"));
            if (Path.IsEmpty() || IFileManager::Get().IsReadOnly(*Path))
            {
                Error = TEXT("checkpoint target is unavailable or read-only: ") + Path;
                return false;
            }
            if (!ValidateFile(File, TEXT("memory"), Error) || !ValidateFile(File, TEXT("disk"), Error))
            {
                return false;
            }
            if (Path == MainFile)
            {
                bHasMainFile = !Flag(Package, TEXT("existed")) || Text(File, TEXT("memory_hash")) != TEXT("absent");
            }
        }
        if (!bHasMainFile)
        {
            Error = TEXT("checkpoint has no serialized package: ") + Name;
            return false;
        }
    }
    return true;
}

bool RestoreCheckpointDisk(const FJson& Receipt, FString& Error)
{
    bool bRestored = true;
    for (const auto& Value : Rows(Receipt, TEXT("packages")))
    {
        const FJson Package = Value->AsObject();
        UPackage* Loaded = FindPackage(nullptr, *Text(Package, TEXT("package")));
        if (Loaded)
        {
            if (Flag(Package, TEXT("existed")))
            {
                Loaded->FullyLoad();
            }
            ResetLoaders(Loaded);
        }
        FString PackageError;
        if (!RestorePackageFiles(Package, false, PackageError))
        {
            Error += (Error.IsEmpty() ? TEXT("") : TEXT("; ")) + PackageError;
            bRestored = false;
        }
        else if (Loaded && !Flag(Package, TEXT("existed")))
        {
            // A retained package shell has no source file after an absent restore.
            Loaded->SetLoadedPath(FPackagePath());
            Loaded->MarkAsNewlyCreated();
        }
    }
    return bRestored;
}
}
