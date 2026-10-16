#include "NexusRestoreBlueprintScope.h"

#include "Engine/Blueprint.h"
#include "UObject/Package.h"
#include "UObject/PackageReload.h"

namespace UeNodeNexusBridge::Collaboration
{
FRestoreBlueprintScope::FRestoreBlueprintScope(const TArray<UPackage*>& InPackages)
    : Packages(InPackages)
{
    Handle = FCoreUObjectDelegates::OnPackageReloaded.AddRaw(this, &FRestoreBlueprintScope::OnReload);
}

FRestoreBlueprintScope::~FRestoreBlueprintScope()
{
    FCoreUObjectDelegates::OnPackageReloaded.Remove(Handle);
}

void FRestoreBlueprintScope::OnReload(EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
{
    if (Phase != EPackageReloadPhase::PostPackageFixup || !Event
        || !Packages.Contains(Event->GetOldPackage()))
    {
        return;
    }
    for (const auto& Pair : Event->GetRepointedObjects())
    {
        UBlueprint* Previous = Cast<UBlueprint>(Pair.Key);
        const UBlueprint* Replacement = Cast<UBlueprint>(Pair.Value);
        if (!Previous || !Replacement || Previous == Replacement
            || Previous->GetOutermost() != Event->GetOldPackage()
            || Replacement->GetOutermost() != Event->GetNewPackage())
        {
            continue;
        }
        // Native reload has replaced this identity. Keep its graph and failed
        // status for old references, but only the replacement participates in PIE.
        Previous->bDisplayCompilePIEWarning = false;
        ++RetiredCount;
        UE_LOG(LogTemp, Display, TEXT("Nexus restore retired_blueprint=%s replacement=%s status=%d"),
            *Previous->GetPathName(), *Replacement->GetPathName(), static_cast<int32>(Previous->Status));
    }
}
}
