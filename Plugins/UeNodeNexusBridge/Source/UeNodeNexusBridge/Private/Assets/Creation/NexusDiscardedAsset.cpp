#include "UeNodeNexusBridgeAssetCreateHelpers.h"

#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
bool IsDiscardedAssetObject(UObject* Object)
{
    return IsValid(Object) && Object->GetOuter() == Object->GetOutermost()
        && !Object->HasAnyFlags(RF_Public | RF_Standalone | RF_ClassDefaultObject | RF_ArchetypeObject);
}

bool ReleaseDiscardedAssetObject(const FString& ObjectPath)
{
    UObject* Existing = FindObject<UObject>(nullptr, *ObjectPath);
    if (!Existing)
    {
        return true;
    }
    if (!IsDiscardedAssetObject(Existing) || FPackageName::DoesPackageExist(Existing->GetOutermost()->GetName()))
    {
        return false;
    }
    // Retire only this deleted object. Existing references may keep it alive;
    // a replacement must get fresh factory defaults and its own identity.
    const FName RetiredName = MakeUniqueObjectName(GetTransientPackage(), Existing->GetClass(), Existing->GetFName());
    if (!Existing->Rename(*RetiredName.ToString(), GetTransientPackage(),
        REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional | REN_DoNotDirty))
    {
        return false;
    }
    return FindObject<UObject>(nullptr, *ObjectPath) == nullptr;
}
}
