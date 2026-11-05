#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UPackage;

namespace UeNodeNexusBridge
{
// What one package load produced compared with the package file's export table.
// An export that failed to instantiate (a missing class or outer) is not in
// memory, so saving the package afterwards would erase it from disk.
struct FPackageLoadAudit
{
    FString PackageName;
    UPackage* Package = nullptr;
    // Captured for the whole set before any load can pull in another member.
    bool bLoadedHere = false;
    // Only a fresh load is audited: the linker of a resident package also marks
    // exports the editor replaced or deleted since, which are not load failures.
    bool bAudited = false;
    int32 Exports = 0;
    TArray<FString> FailedExports;
    FString Error;

    bool IsComplete() const
    {
        return Package != nullptr && Error.IsEmpty() && FailedExports.IsEmpty();
    }
    TSharedPtr<FJsonObject> ToJson() const;
};

TArray<FPackageLoadAudit> LoadPackagesAudited(const TArray<FString>& PackageNames);
}
