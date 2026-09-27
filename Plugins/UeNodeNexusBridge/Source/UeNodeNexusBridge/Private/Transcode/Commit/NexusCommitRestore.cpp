#include "Restore/NexusRestore.h"

#include "Misc/ScopeExit.h"
#include "ObjectTools.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge::Collaboration
{
static bool RemoveCreatedAsset(const FJson& Request, FString& Error)
{
    if (!Flag(Request, TEXT("expected_absent")))
    {
        return true;
    }
    UObject* Created = FindObject<UObject>(nullptr, *Text(Request, TEXT("asset_path")));
    if (Created && Created->IsAsset())
    {
        TArray<UObject*> Objects;
        Objects.Add(Created);
        if (ObjectTools::DeleteObjectsUnchecked(Objects) != 1)
        {
            Error = TEXT("cannot remove the newly created object during recovery");
            return false;
        }
    }
    return true;
}

bool RestoreCheckpoint(const FJson& Receipt, FString& Error)
{
    const FJson Request = Object(Receipt, TEXT("request"));
    bool bComplete = false;
    TMap<FString, bool> DirtyBefore;
    for (const auto& Value : Rows(Receipt, TEXT("packages")))
    {
        const FJson Package = Value->AsObject();
        const FString Name = Text(Package, TEXT("package"));
        const UPackage* Loaded = FindPackage(nullptr, *Name);
        DirtyBefore.Add(Name, Flag(Package, TEXT("was_dirty")) || (Loaded && Loaded->IsDirty()));
    }
    ON_SCOPE_EXIT
    {
        if (!bComplete)
        {
            for (const auto& Pair : DirtyBefore)
            {
                if (UPackage* Loaded = FindPackage(nullptr, *Pair.Key))
                {
                    Loaded->SetDirtyFlag(Pair.Value);
                }
            }
        }
    };
    Receipt->SetStringField(TEXT("restore_method"), TEXT("package_reload"));
    if (!SaveReceipt(Receipt, TEXT("restoring"), Error)
        || !ValidateCheckpointFiles(Receipt, Error)
        || !PrepareCheckpointPackages(Receipt, Error)
        || !RemoveCreatedAsset(Request, Error))
    {
        return false;
    }
    const bool bMemoryRestored = RestoreCheckpointMemory(Receipt, Error);
    FString DiskError;
    // Even a partial reload must put back the original on-disk version. A failed
    // restore retains its durable receipt and the applied dirty state for recovery.
    const bool bDiskRestored = RestoreCheckpointDisk(Receipt, DiskError);
    if (!bMemoryRestored || !bDiskRestored)
    {
        if (!DiskError.IsEmpty())
        {
            Error += (Error.IsEmpty() ? TEXT("") : TEXT("; ")) + DiskError;
        }
        return false;
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
    if (!Current.IsValid() || Expected.IsEmpty() || Expected != Text(Current, TEXT("content_revision")))
    {
        Error = TEXT("checkpoint verification failed: content revision differs from the recorded before state");
        Receipt->SetObjectField(TEXT("recovery_observation"), Current.IsValid() ? Current : MakeShared<FJsonObject>());
        return false;
    }
    Receipt->SetObjectField(TEXT("recovered"), Current);
    bComplete = SaveReceipt(Receipt, TEXT("rolled_back"), Error);
    return bComplete;
}
}
